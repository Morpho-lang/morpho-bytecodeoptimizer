/** @file optimize.c
 *  @author T J Atherton
 *
 *  @brief Optimizer for compiled morpho bytecode
*/

#include <stdarg.h>

#include "morphocore.h"
#include "optimize.h"
#include "opcodes.h"
#include "cfgraph.h"
#include "reginfo.h"
#include "info.h"
#include "strategy.h"
#include "layout.h"

DEFINE_VARRAY(functioninputinfo, functioninputinfo)

/* **********************************************************************
 * Optimizer data structure
 * ********************************************************************** */

/** Initializes an optimizer data structure */
void optimizer_init(optimizer *opt, program *prog) {
    opt->prog=prog;
    opt->pass=0;
    
    error_init(&opt->err);
    cfgraph_init(&opt->graph);
    dictionary_init(&opt->reachable);
    opt->reachabledirty=true;
    reginfolist_init(&opt->rlist, MORPHO_MAXREGISTERS);
    globalinfolist_init(&opt->glist, prog->globals.count);
    classinfolist_init(&opt->classinfo);
    functioninfolist_init(&opt->functioninfo);
    varray_functioninputinfoinit(&opt->functioninputs);
    dictionary_init(&opt->functioninputindx);
    varray_instructioninit(&opt->insertions);
    
    opt->v=morpho_newvm();
    opt->temp=morpho_newprogram();
    opt->ipachanged=false;
    
#ifdef OPTIMIZER_VERBOSE
    opt->verbose=true;
#else
    opt->verbose=false;
#endif
}

static void optimize_clearfunctioninputs(optimizer *opt) {
    for (int i=0; i<opt->functioninputs.count; i++) reginfolist_clear(&opt->functioninputs.data[i].input);
    opt->functioninputs.count=0;
    dictionary_clear(&opt->functioninputindx);
    dictionary_init(&opt->functioninputindx);
    opt->ipachanged=false;
}

/** Clears an optimizer data structure */
void optimize_clear(optimizer *opt) {
    error_clear(&opt->err);
    cfgraph_clear(&opt->graph);
    dictionary_clear(&opt->reachable);
    reginfolist_clear(&opt->rlist);
    globalinfolist_clear(&opt->glist);
    classinfolist_clear(&opt->classinfo);
    functioninfolist_clear(&opt->functioninfo);
    optimize_clearfunctioninputs(opt);
    varray_functioninputinfoclear(&opt->functioninputs);
    dictionary_clear(&opt->functioninputindx);
    varray_instructionclear(&opt->insertions);
    
    if (opt->v) morpho_freevm(opt->v);
    if (opt->temp) morpho_freeprogram(opt->temp);
}

/* **********************************************************************
 * Methodinfo
 * ********************************************************************** */

// Adds a function into the functioninfo list
void _processfunction(objectfunction *fn, functioninfolist *out) {
    functioninfolist_incrementowners(out, fn);
}

// Searches a metafunction for methods
void _processmetafunction(objectmetafunction *mfn, functioninfolist *out) {
    for (int i=0; i<mfn->fns.count; i++) {
        value fn = mfn->fns.data[i];
        if (MORPHO_ISFUNCTION(fn)) {
            _processfunction(MORPHO_GETFUNCTION(fn), out);
        }
    }
}

// Searches a class's method table
void _processclass(objectclass *klass, functioninfolist *out) {
    for (int i=0; i<klass->methods.capacity; i++) {
        value label = klass->methods.contents[i].key;
        if (MORPHO_ISNIL(label)) continue;
        
        value method = klass->methods.contents[i].val;
        if (MORPHO_ISFUNCTION(method)) {
            _processfunction(MORPHO_GETFUNCTION(method), out);
        } else if (MORPHO_ISMETAFUNCTION(method)) {
            _processmetafunction(MORPHO_GETMETAFUNCTION(method), out);
        }
    }
}

/* Compute method info */
void optimize_methodinfo(optimizer *opt) {
    varray_value *klasses = &opt->prog->classes;
    
    for (int i=0; i<klasses->count; i++) {
        _processclass(MORPHO_GETCLASS(klasses->data[i]), &opt->functioninfo);
    }
}

/* **********************************************************************
 * Optimize a code block
 * ********************************************************************** */

static void optimize_loadblockinput(optimizer *opt, block *blk);
void optimize_track(optimizer *opt);
static void _optimize_classinfo_processcomponent(optimizer *opt, value comp, dictionary *components, globalinfolist *globals);
static void _pruneunreachableblock(optimizer *opt, blockindx blkindx);

/** Raise an error with the optimizer */
void optimize_error(optimizer *opt, errorid id, ...) {
    va_list args;
    va_start(args, id);
    morpho_writeerrorwithidvalist(&opt->err, id, NULL, 0, ERROR_POSNUNIDENTIFIABLE, args);
    va_end(args);
}

/** Checks whether an error occurred */
bool optimize_checkerror(optimizer *opt) {
    return (opt->err.cat!=ERROR_NONE);
}

/** Fetches the instruction at index i and sets this as the current instruction */
instruction optimize_fetch(optimizer *opt, instructionindx i) {
    opt->pc=i;
    return opt->current=opt->prog->code.data[i];
}

/** Callback function to set the contents of a register */
void optimize_write(optimizer *opt, registerindx r, regcontents contents, indx ix) {
    reginfolist_write(&opt->rlist, opt->pc, r, contents, ix);
}

/** Callback function to copy one register fact into another. */
void optimize_copyregister(optimizer *opt, registerindx dest, registerindx src) {
    reginfolist_copyregister(&opt->rlist, opt->pc, dest, src);
}

/** Callback function to set the contents of a register */
void optimize_writevalue(optimizer *opt, registerindx r) {
    optimize_write(opt, r, REG_VALUE, INSTRUCTIONINDX_EMPTY);
}

/** Callback function to set the type and precision of a register */
void optimize_settype(optimizer *opt, registerindx r, value type, regtypeinfo info) {
    reginfolist_settypeinfo(&opt->rlist, r, type, info);
}

/** Callback function to set an exact type fact for a register. */
void optimize_setexacttype(optimizer *opt, registerindx r, value type) {
    optimize_settype(opt, r, type, REGTYPE_EXACT);
}

/** Infer the strongest safe precision for a type fact. */
regtypeinfo optimize_typeprecision(value type) {
    if (!MORPHO_ISCLASS(type)) return REGTYPE_UNKNOWN;
    return optimize_classisleaf(MORPHO_GETCLASS(type)) ? REGTYPE_EXACT : REGTYPE_SUBTYPE;
}

/** Callback function to get the type of a register */
value optimize_type(optimizer *opt, registerindx r) {
    return reginfolist_type(&opt->rlist, r);
}

/** Callback function to get type precision of a register */
regtypeinfo optimize_typeinfo(optimizer *opt, registerindx r) {
    return reginfolist_typeinfo(&opt->rlist, r);
}

/** Wrapper to get the type information from a value */
bool optimize_typefromvalue(value val, value *type) {
    return value_type(val, type);
}

static void _optimize_initregfact(reginfo *info) {
    info->contents=REG_NOFACT;
    info->indx=0;
    info->usage=REGUSE_NONE;
    info->iindx=INSTRUCTIONINDX_EMPTY;
    info->type=MORPHO_NIL;
    info->typeinfo=REGTYPE_UNKNOWN;
    info->hasalias=false;
    info->alias=0;
}

static bool _optimize_addconstanttofunction(optimizer *opt, objectfunction *func, value val, indx *out) {
    unsigned int k;

    if (varray_valuefindsame(&func->konst, val, &k)) {
        *out = (indx) k;
        return true;
    }

    if (!varray_valueadd(&func->konst, &val, 1)) return false;
    *out=func->konst.count-1;

    if (MORPHO_ISOBJECT(val)) program_bindobject(opt->prog, MORPHO_GETOBJECT(val));
    return true;
}

static functioninputinfo *_optimize_functioninputinfo(optimizer *opt, objectfunction *func) {
    value ix;
    if (dictionary_get(&opt->functioninputindx, MORPHO_OBJECT(func), &ix) && MORPHO_ISINTEGER(ix)) {
        return &opt->functioninputs.data[MORPHO_GETINTEGERVALUE(ix)];
    }

    functioninputinfo info;
    info.func=func;
    reginfolist_init(&info.input, func->nregs);
    if (!varray_functioninputinfoadd(&opt->functioninputs, &info, 1)) {
        reginfolist_clear(&info.input);
        return NULL;
    }

    dictionary_insert(&opt->functioninputindx, MORPHO_OBJECT(func), MORPHO_INTEGER(opt->functioninputs.count-1));
    return &opt->functioninputs.data[opt->functioninputs.count-1];
}

static bool _optimize_functionhasflags(optimizer *opt, objectfunction *func, unsigned int flags) {
    return functioninfolist_hasflags(&opt->functioninfo, func, flags);
}

static bool _optimize_functionisrecursive(optimizer *opt, objectfunction *func) {
    return _optimize_functionhasflags(opt, func, FUNCTIONINFO_RECURSIVE);
}

static bool _optimize_functionescapes(optimizer *opt, objectfunction *func) {
    return _optimize_functionhasflags(opt, func, FUNCTIONINFO_ESCAPES);
}

void optimize_markrecursive(optimizer *opt, objectfunction *func) {
    if (_optimize_functionisrecursive(opt, func)) return;
    if (!functioninfolist_setflags(&opt->functioninfo, func, FUNCTIONINFO_RECURSIVE)) return;
    opt->ipachanged=true;
}

void optimize_markescaped(optimizer *opt, objectfunction *func) {
    functioninputinfo *info;

    if (_optimize_functionescapes(opt, func)) return;
    if (!functioninfolist_setflags(&opt->functioninfo, func, FUNCTIONINFO_ESCAPES)) return;

    info = _optimize_functioninputinfo(opt, func);
    if (info) reginfolist_wipe(&info->input, info->input.nreg);

    opt->ipachanged=true;
}

static bool _optimize_setfunctioninputfact(optimizer *opt, functioninputinfo *info, registerindx dest, reginfo *incoming) {
    if (dest>=info->input.nreg) return false;

    reginfo old = info->input.rinfo[dest];
    if (old.contents==REG_NOFACT) {
        info->input.rinfo[dest]=*incoming;
    } else {
        reginfo_join(&info->input.rinfo[dest], incoming);
    }

    if (!reginfo_equal(&old, &info->input.rinfo[dest])) {
        opt->ipachanged=true;
        return true;
    }
    return false;
}

static bool _optimize_recordcallarg(optimizer *opt, functioninputinfo *info, registerindx dest, registerindx src) {
    reginfo incoming;
    value type;
    regtypeinfo typeinfo;
    indx kindx, calleeindx;

    _optimize_initregfact(&incoming);
    incoming.contents=REG_VALUE;
    incoming.iindx=INSTRUCTIONINDX_EMPTY;
    incoming.usage=REGUSE_WRITTEN;

    if (optimize_isconstant(opt, src, &kindx)) {
        value konst = optimize_getconstant(opt, kindx);
        if (_optimize_addconstanttofunction(opt, info->func, konst, &calleeindx)) {
            incoming.contents=REG_CONSTANT;
            incoming.indx=calleeindx;
            if (optimize_typefromvalue(konst, &type)) {
                incoming.type=type;
                incoming.typeinfo=REGTYPE_EXACT;
            }
        }
    } else {
        type=optimize_type(opt, src);
        typeinfo=optimize_typeinfo(opt, src);
        if (!MORPHO_ISNIL(type)) {
            incoming.contents=REG_TYPEDVALUE;
            incoming.type=type;
            incoming.typeinfo=typeinfo;
        }
    }

    return _optimize_setfunctioninputfact(opt, info, dest, &incoming);
}

bool optimize_classisleaf(objectclass *klass) {
    return (klass && klass->children.count==0);
}

bool optimize_classisderivedfrom(objectclass *klass, objectclass *base) {
    if (!klass || !base) return false;
    if (klass==base) return true;

    for (unsigned int i=0; i<base->children.count; i++) {
        value child = base->children.data[i];
        if (MORPHO_ISCLASS(child) &&
            optimize_classisderivedfrom(klass, MORPHO_GETCLASS(child))) return true;
    }

    return false;
}

static bool _optimize_canspecializeself(optimizer *opt, objectfunction *func, registerindx selfreg) {
    if (selfreg==REGISTER_UNALLOCATED || !func || !func->klass) return false;
    if (!optimize_hasuniquetype(opt, selfreg)) return false;

    value selftype = optimize_type(opt, selfreg);
    if (!MORPHO_ISCLASS(selftype)) return false;

    return optimize_classisderivedfrom(MORPHO_GETCLASS(selftype), func->klass);
}

bool optimize_recordcallsite(optimizer *opt, objectfunction *func, registerindx argstart, int nargs, registerindx selfreg) {
    functioninputinfo *info = _optimize_functioninputinfo(opt, func);
    bool changed=false;

    if (!info || _optimize_functionescapes(opt, func)) return false;

    if (_optimize_canspecializeself(opt, func, selfreg)) changed = _optimize_recordcallarg(opt, info, 0, selfreg) || changed;
    for (registerindx i=0; i<nargs && i<func->nargs; i++) {
        changed = _optimize_recordcallarg(opt, info, i+1, argstart+i) || changed;
    }

    return changed;
}

static void _optimize_applyinputfact(optimizer *opt, objectfunction *func, registerindx rindx, reginfo *incoming) {
    if (func->klass && rindx==0) {
        reginfo_join(&opt->rlist.rinfo[rindx], incoming);
    } else {
        opt->rlist.rinfo[rindx]=*incoming;
    }
}

/** Callback function to get a constant from the current constant table */
value optimize_getconstant(optimizer *opt, indx i) {
    return block_getconstant(opt->currentblk, i);
}

/** Adds a constant to the current constant table */
bool optimize_addconstant(optimizer *opt, value val, indx *out) {
    return _optimize_addconstanttofunction(opt, opt->currentblk->func, val, out);
}

/** Checks if a register has no semantic fact */
bool optimize_isempty(optimizer *opt, registerindx i) {
    regcontents contents=REG_NOFACT;
    reginfolist_contents(&opt->rlist, i, &contents, NULL);
    return (contents==REG_NOFACT);
}

/** Checks if a register holds a constant */
bool optimize_isconstant(optimizer *opt, registerindx i, indx *out) {
    regcontents contents=REG_NOFACT;
    indx ix;
    if (!reginfolist_contents(&opt->rlist, i, &contents, &ix)) return false;
    
    bool success=(contents==REG_CONSTANT);
    if (success && out) *out = ix;
    
    return success;
}

/** Checks if a register holds another register */
bool optimize_isglobal(optimizer *opt, registerindx i, indx *out) {
    regcontents contents=REG_NOFACT;
    indx ix;
    if (!reginfolist_contents(&opt->rlist, i, &contents, &ix)) return false;
    
    bool success=(contents==REG_GLOBAL);
    if (success && out) *out = ix;
    
    return success;
}

/** Checks if a register currently aliases another register */
bool optimize_isregister(optimizer *opt, registerindx i, registerindx *out) {
    return reginfolist_alias(&opt->rlist, i, out);
}

/** Returns the content type of a register */
bool optimize_contents(optimizer *opt, registerindx i, regcontents *contents, indx *indx) {
    return reginfolist_contents(&opt->rlist, i, contents, indx);
}

/** Checks if a register has an exact type or a subtype fact with no subclasses */
bool optimize_hasuniquetype(optimizer *opt, registerindx r) {
    value type = optimize_type(opt, r);
    regtypeinfo info = optimize_typeinfo(opt, r);

    if (info==REGTYPE_EXACT) return !MORPHO_ISNIL(type);
    if (info!=REGTYPE_SUBTYPE || !MORPHO_ISCLASS(type)) return false;

    return optimize_classisleaf(MORPHO_GETCLASS(type));
}

/** Checks if a register has an exact type fact. */
bool optimize_hasexacttype(optimizer *opt, registerindx r) {
    return (optimize_typeinfo(opt, r)==REGTYPE_EXACT &&
            !MORPHO_ISNIL(optimize_type(opt, r)));
}

/** Checks if a register is overwritten between start and the current instruction */
bool optimize_isoverwritten(optimizer *opt, registerindx rindx, instructionindx start) {
    for (instructionindx i=start; i<opt->pc; i++) {
        instruction instr = optimize_getinstructionat(opt, i);
        registerindx overwrites;
        if (opcode_overwritesforinstruction(instr, &overwrites) &&
            overwrites==rindx) {
            return true;
        }
    }
    
    return false;
}

typedef struct {
    registerindx r;
    bool isused;
} _usedstruct;

static void _usagefn(registerindx i, void *ref) { // Set usage
    _usedstruct *s = (_usedstruct *) ref;
    if (i==s->r) s->isused=true;
}

/** Checks if a register is used between the instruction after the current one and the end of the block OR an instruction that overwrites it */
bool optimize_isused(optimizer *opt, registerindx rindx) {
    for (instructionindx i=opt->pc+1; i<=opt->currentblk->end; i++) {
        instruction instr = optimize_getinstructionat(opt, i);
        
        _usedstruct s = { .r = rindx, .isused = false };
        opcode_usageforinstruction(opt->currentblk, instr, _usagefn, &s);
        if (s.isused) return true;
        
        registerindx overwrites;
        if (opcode_overwritesforinstruction(instr, &overwrites) &&
            overwrites==rindx) return false;
    }
    
    return optimize_checkdestusage(opt, opt->currentblk, rindx);
}

bool optimize_highestused(optimizer *opt, registerindx *out) {
    objectfunction *func = optimize_currentblock(opt)->func;
    for (registerindx r=func->nregs; r>0; r--) {
        if (optimize_isused(opt, r-1)) {
            *out=r-1;
            return true;
        }
    }
    return false;
}

/** Trace back through aliases to find an original register. */
registerindx optimize_findoriginalregister(optimizer *opt, registerindx rindx) {
    registerindx out=rindx, next;

    while (optimize_isregister(opt, out, &next)) {
        if (next==rindx) break; // Break cycles
        out=next;
    }

    return out;
}

/** Finds whether a register refers to a constant. */
bool optimize_findconstant(optimizer *opt, registerindx i, indx *out) {
    return optimize_isconstant(opt, i, out);
}

/** Extracts usage information */
int optimize_countuses(optimizer *opt, registerindx i) {
    return reginfolist_countuses(&opt->rlist, i);
}

bool optimize_source(optimizer *opt, registerindx i, instructionindx *indx) {
    return reginfolist_source(&opt->rlist, i, indx);
}

/** Callback function to get the current instruction */
instruction optimize_getinstruction(optimizer *opt) {
    return opt->current;
}

/** Callback function to get the current instruction */
instructionindx optimize_getinstructionindx(optimizer *opt) {
    return opt->pc;
}

/** Gets the instruction at a given index; doesn't set this as the current instruction */
instruction optimize_getinstructionat(optimizer *opt, instructionindx i) {
    return opt->prog->code.data[i];
}

/** Get the current block */
block *optimize_currentblock(optimizer *opt) {
    return opt->currentblk;
}

/** Callback function to replace the current instruction */
void optimize_replaceinstruction(optimizer *opt, instruction instr) {
    optimize_replaceinstructionat(opt, opt->pc, instr);
    opt->current=instr;
    if (opt->verbose) optimize_disassemble(opt);
}

/** Replaces an instruction at a given index */
void optimize_replaceinstructionat(optimizer *opt, instructionindx i, instruction instr) {
    instruction oinstr = opt->prog->code.data[i];
    
    //opcodetrackingfn replacefn=opcode_getreplacefn(DECODE_OP(oinstr));
    //if (replacefn) replacefn(opt);
    
    opt->prog->code.data[i]=instr;
    opt->nchanged++;
}

/** Inserts a sequence of instructions at a given index, replacing the current instruction there */
void optimize_insertinstructions(optimizer *opt, int n, instruction *instr) {
    instructionindx start = opt->insertions.count;
    varray_instructionadd(&opt->insertions, instr, n);
    varray_instructionwrite(&opt->insertions, ENCODE_BYTE(OP_END));
    
    optimize_replaceinstruction(opt, ENCODE_LONG(OP_INSERT, n, start));
    opt->nchanged++;
}

/** Replaces the current instruction with LCT r, and a given constant */
bool optimize_replacewithloadconstant(optimizer *opt, registerindx r, value konst) {
    // Add the constant to the constant table
    indx kindx;
    if (!optimize_addconstant(opt, konst, &kindx)) return false;
    
    // Replace the instruction
    optimize_replaceinstruction(opt, ENCODE_LONG(OP_LCT, r, (unsigned int) kindx));
    
    // Set the contents of the register
    optimize_write(opt, r, REG_CONSTANT, kindx);
    
    value type; // Also set type information
    if (optimize_typefromvalue(konst, &type)) optimize_setexacttype(opt, r, type);
    
    return true;
}

/** Attempts to delete an instruction. Checks to see if the instruction has side effects and ignores the deletion if it does.
    Returns true if the instruction was deleted */
bool optimize_deleteinstruction(optimizer *opt, instructionindx indx) {
    instruction instr = optimize_getinstructionat(opt, indx);
    if (DECODE_OP(instr)==OP_INSERT) return false; // Instruction to be deleted was an insertion point
    if (DECODE_OP(instr)==OP_NOP) return false;
    
    opcodeflags flags = opcode_getflags(DECODE_OP(instr));
    
    // Check for instructions that generic dead-code elimination must not erase.
    if (flags & OPCODE_NODELETE) return false; // Todo: Check whether some protected instructions could still be safe.
    
    optimize_replaceinstructionat(opt, indx, ENCODE_BYTE(OP_NOP));
    return true;
}

/** Gets the global info list */
globalinfolist *optimize_globalinfolist(optimizer *opt) {
    return &opt->glist;
}

/* **********************************************************************
 * Classinfo
 * ********************************************************************** */

static void _optimize_classinfo_mark(value val, classinfolist *out) {
    if (MORPHO_ISCLASS(val)) classinfolist_incrementconstructed(out, MORPHO_GETCLASS(val));
}

static void _optimize_classinfo_markregister(value *regs, registerindx start, int n, classinfolist *out) {
    for (registerindx i=0; i<n; i++) _optimize_classinfo_mark(regs[start+i], out);
}

static void _optimize_classinfo_markconstants(varray_value *konst, classinfolist *out) {
    for (unsigned int i=0; i<konst->count; i++) _optimize_classinfo_mark(konst->data[i], out);
}

typedef struct {
    optimizer *opt;
    objectfunction *func;
    globalinfolist *globals;
    value regs[MORPHO_MAXREGISTERS];
    bool trackglobalstores;
    void (*onglobalstore)(instruction instr, value val, void *ctx);
    void (*onglobalload)(instruction instr, value val, void *ctx);
    void (*oncalllike)(instruction instr, value *regs, void *ctx);
    void (*oninstruction)(instruction instr, value *regs, void *ctx);
    void *ctx;
} _optimize_ipascanstate;

typedef void (*_optimize_ipastepfn) (_optimize_ipascanstate *state, instruction instr);

typedef struct {
    instruction op;
    _optimize_ipastepfn fn;
} _optimize_ipastep;

static void _optimize_ipastoreglobal(_optimize_ipascanstate *state, int gindx, value val) {
    globalinfolist_store(state->globals, gindx);
    if (MORPHO_ISFUNCTION(val) || MORPHO_ISMETAFUNCTION(val) ||
        MORPHO_ISCLASS(val) || MORPHO_ISSTRING(val)) {
        globalinfolist_setconstant(state->globals, gindx, val);
    } else {
        globalinfolist_setvalue(state->globals, gindx);
    }
}

static void _optimize_ipainstruction(_optimize_ipascanstate *state, instruction instr) {
    if (state->oninstruction) state->oninstruction(instr, state->regs, state->ctx);
}

static void _optimize_ipaop_lct(_optimize_ipascanstate *state, instruction instr) {
    indx kindx = DECODE_Bx(instr);
    registerindx a = DECODE_A(instr);

    state->regs[a] = (kindx<state->func->konst.count) ? state->func->konst.data[kindx] : MORPHO_NIL;
}

static void _optimize_ipaop_mov(_optimize_ipascanstate *state, instruction instr) {
    state->regs[DECODE_A(instr)] = state->regs[DECODE_B(instr)];
}

static void _optimize_ipaop_sgl(_optimize_ipascanstate *state, instruction instr) {
    registerindx a = DECODE_A(instr);
    int gindx = DECODE_Bx(instr);

    if (state->trackglobalstores) _optimize_ipastoreglobal(state, gindx, state->regs[a]);
    if (state->onglobalstore) state->onglobalstore(instr, state->regs[a], state->ctx);
}

static void _optimize_ipaop_lgl(_optimize_ipascanstate *state, instruction instr) {
    registerindx a = DECODE_A(instr);
    int gindx = DECODE_Bx(instr);

    globalinfolist_read(state->globals, gindx);
    if (!globalinfolist_isconstant(state->globals, gindx, &state->regs[a])) state->regs[a]=MORPHO_NIL;
    if (state->onglobalload) state->onglobalload(instr, state->regs[a], state->ctx);
}

static void _optimize_ipaop_calllike(_optimize_ipascanstate *state, instruction instr) {
    instruction op = DECODE_OP(instr);
    registerindx a = DECODE_A(instr);
    int nargs = DECODE_B(instr), nopt = DECODE_C(instr);
    registerindx firstoverwritten = a + (op==OP_CALL ? 0 : 1);
    registerindx lastoverwritten = a + nargs + 2*nopt + (op==OP_CALL ? 0 : 1);

    if (state->oncalllike) state->oncalllike(instr, state->regs, state->ctx);

    /* Mirror opcode tracking by dropping all consumed call/result slots to
       generic values so stale callable/class constants do not leak forward. */
    for (registerindx r=firstoverwritten; r<=lastoverwritten; r++) {
        state->regs[r] = MORPHO_NIL;
    }
}

static void _optimize_ipaop_closure(_optimize_ipascanstate *state, instruction instr) {
    _optimize_ipainstruction(state, instr);
}

static void _optimize_ipaop_lpr(_optimize_ipascanstate *state, instruction instr) {
    _optimize_ipainstruction(state, instr);
    state->regs[DECODE_A(instr)] = MORPHO_NIL;
}

static void _optimize_ipaop_sideeffect(_optimize_ipascanstate *state, instruction instr) {
    _optimize_ipainstruction(state, instr);
}

static void _optimize_ipaop_cat(_optimize_ipascanstate *state, instruction instr) {
    _optimize_ipainstruction(state, instr);
    state->regs[DECODE_A(instr)] = MORPHO_NIL;
}

static void _optimize_ipaop_default(_optimize_ipascanstate *state, instruction instr) {
    registerindx overwrites;

    _optimize_ipainstruction(state, instr);
    if (opcode_overwritesforinstruction(instr, &overwrites)) state->regs[overwrites]=MORPHO_NIL;
}

static _optimize_ipastep _optimize_ipasteps[] = {
    { OP_LCT, _optimize_ipaop_lct },
    { OP_MOV, _optimize_ipaop_mov },
    { OP_SGL, _optimize_ipaop_sgl },
    { OP_LGL, _optimize_ipaop_lgl },
    { OP_CALL, _optimize_ipaop_calllike },
    { OP_METHOD, _optimize_ipaop_calllike },
    { OP_INVOKE, _optimize_ipaop_calllike },
    { OP_CLOSURE, _optimize_ipaop_closure },
    { OP_LPR, _optimize_ipaop_lpr },
    { OP_RETURN, _optimize_ipaop_sideeffect },
    { OP_SPR, _optimize_ipaop_sideeffect },
    { OP_SUP, _optimize_ipaop_sideeffect },
    { OP_CAT, _optimize_ipaop_cat },
    { OP_END, NULL }
};

static void _optimize_ipastepinstruction(_optimize_ipascanstate *state, instruction instr) {
    for (_optimize_ipastep *step=_optimize_ipasteps; step->op!=OP_END; step++) {
        if (step->op==DECODE_OP(instr)) {
            step->fn(state, instr);
            return;
        }
    }

    _optimize_ipaop_default(state, instr);
}

static void _optimize_ipascanfunction(_optimize_ipascanstate *state) {
    optimizer *opt = state->opt;
    objectfunction *func = state->func;

    for (int i=0; i<MORPHO_MAXREGISTERS; i++) state->regs[i]=MORPHO_NIL;

    for (blockindx b=0; b<opt->graph.count; b++) {
        block *blk = &opt->graph.data[b];
        if (blk->func!=func) continue;

        for (instructionindx i=blk->start; i<=blk->end; i++) {
            _optimize_ipastepinstruction(state, opt->prog->code.data[i]);
        }
    }
}

typedef struct {
    classinfolist *out;
} _optimize_classscanctx;

typedef void (*_optimize_classscanstepfn) (_optimize_classscanctx *classctx, instruction instr, value *regs);

typedef struct {
    instruction op;
    _optimize_classscanstepfn fn;
} _optimize_classscanstep;

static void _optimize_classscan_onglobalload(instruction instr, value val, void *ctx) {
    _optimize_classscanctx *classctx = (_optimize_classscanctx *) ctx;
    _optimize_classinfo_mark(val, classctx->out);
}

static void _optimize_classscan_opcalllike(_optimize_classscanctx *classctx, instruction instr, value *regs) {
    instruction op = DECODE_OP(instr);
    registerindx a = DECODE_A(instr);
    int nargs = DECODE_B(instr), nopt = DECODE_C(instr);
    registerindx receiver = a + (op==OP_CALL ? 0 : 1);

    _optimize_classinfo_mark(regs[receiver], classctx->out);
    _optimize_classinfo_markregister(regs, receiver+1, nargs+2*nopt, classctx->out);
}

static _optimize_classscanstep _optimize_classscansteps[] = {
    { OP_CALL, _optimize_classscan_opcalllike },
    { OP_METHOD, _optimize_classscan_opcalllike },
    { OP_INVOKE, _optimize_classscan_opcalllike },
    { OP_END, NULL }
};

static void _optimize_classscan_oncalllike(instruction instr, value *regs, void *ctx) {
    _optimize_classscanctx *classctx = (_optimize_classscanctx *) ctx;

    for (_optimize_classscanstep *step=_optimize_classscansteps; step->op!=OP_END; step++) {
        if (step->op==DECODE_OP(instr)) {
            step->fn(classctx, instr, regs);
            return;
        }
    }
}

/* Track direct constructor/class flows through constants, moves, and globals. */
static void _optimize_classinfo_scanfunction(optimizer *opt, objectfunction *func, classinfolist *out, globalinfolist *globals, bool trackglobalstores) {
    _optimize_classscanctx classctx = { .out=out };
    _optimize_ipascanstate state = {
        .opt=opt,
        .func=func,
        .globals=globals,
        .trackglobalstores=trackglobalstores,
        .onglobalload=_optimize_classscan_onglobalload,
        .oncalllike=_optimize_classscan_oncalllike,
        .ctx=&classctx
    };

    _optimize_ipascanfunction(&state);
}

static void _optimize_classinfo_processcomponent(optimizer *opt, value comp, dictionary *components, globalinfolist *globals) {
    if (!MORPHO_ISCALLABLE(comp) || dictionary_get(components, comp, NULL)) return;
    dictionary_insert(components, comp, MORPHO_NIL);

    if (MORPHO_ISFUNCTION(comp)) {
        objectfunction *func = MORPHO_GETFUNCTION(comp);
        bool isglobal = (func==opt->prog->global);

        if (!isglobal) {
            _optimize_classinfo_markconstants(&func->konst, &opt->classinfo);
        }
        _optimize_classinfo_scanfunction(opt, func, &opt->classinfo, globals, isglobal);

        for (unsigned int i=0; i<func->konst.count; i++) {
            _optimize_classinfo_processcomponent(opt, func->konst.data[i], components, globals);
        }
    } else if (MORPHO_ISMETAFUNCTION(comp)) {
        objectmetafunction *mfn = MORPHO_GETMETAFUNCTION(comp);
        for (unsigned int i=0; i<mfn->fns.count; i++) {
            _optimize_classinfo_processcomponent(opt, mfn->fns.data[i], components, globals);
        }
    }
}

static void optimize_classinfo(optimizer *opt) {
    dictionary components;

    classinfolist_startpass(&opt->classinfo);
    globalinfolist_startpass(&opt->glist);
    dictionary_init(&components);
    _optimize_classinfo_processcomponent(opt, MORPHO_OBJECT(opt->prog->global), &components, &opt->glist);
    dictionary_clear(&components);
}

static bool _optimize_marklivefunction(objectfunction *func, dictionary *live, varray_value *worklist) {
    value key = MORPHO_OBJECT(func);

    if (dictionary_get(live, key, NULL)) return false;
    dictionary_insert(live, key, MORPHO_NIL);
    varray_valuewrite(worklist, key);
    return true;
}

static void _optimize_marklivecallable(optimizer *opt, value val, dictionary *live, varray_value *worklist) {
    if (MORPHO_ISFUNCTION(val)) {
        _optimize_marklivefunction(MORPHO_GETFUNCTION(val), live, worklist);
    } else if (MORPHO_ISMETAFUNCTION(val)) {
        objectmetafunction *mfn = MORPHO_GETMETAFUNCTION(val);
        for (unsigned int i=0; i<mfn->fns.count; i++) {
            value fn = mfn->fns.data[i];
            if (MORPHO_ISFUNCTION(fn)) _optimize_marklivefunction(MORPHO_GETFUNCTION(fn), live, worklist);
        }
    } else if (MORPHO_ISCLASS(val)) {
        objectstring initlabel = MORPHO_STATICSTRING("init");
        value method;

        if (morpho_lookupmethod(val, MORPHO_OBJECT(&initlabel), &method)) {
            _optimize_marklivecallable(opt, method, live, worklist);
        }
    }
}

static void _optimize_markallmethods_live(optimizer *opt, dictionary *live, varray_value *worklist) {
    for (unsigned int i=0; i<opt->prog->classes.count; i++) {
        objectclass *klass = MORPHO_GETCLASS(opt->prog->classes.data[i]);

        for (int j=0; j<klass->methods.capacity; j++) {
            value label = klass->methods.contents[j].key;
            if (MORPHO_ISNIL(label)) continue;

            _optimize_marklivecallable(opt, klass->methods.contents[j].val, live, worklist);
        }
    }
}

static void _optimize_markmethodsforlabel_live(optimizer *opt, value label, dictionary *live, varray_value *worklist);

static void _optimize_marklabelmethods_live(optimizer *opt, value label, dictionary *live, varray_value *worklist, bool *disablemethodpruning) {
    if (!MORPHO_ISSTRING(label)) return;

    if (strcmp(MORPHO_GETCSTRING(label), "invoke")==0) {
        if (disablemethodpruning) *disablemethodpruning=true;
        _optimize_markallmethods_live(opt, live, worklist);
    } else {
        _optimize_markmethodsforlabel_live(opt, label, live, worklist);
    }
}

static void _optimize_markmethodsforlabel_live(optimizer *opt, value label, dictionary *live, varray_value *worklist) {
    for (unsigned int i=0; i<opt->prog->classes.count; i++) {
        objectclass *klass = MORPHO_GETCLASS(opt->prog->classes.data[i]);
        value method;
        bool found = morpho_lookupmethod(MORPHO_OBJECT(klass), label, &method);

        if (found) {
            _optimize_marklivecallable(opt, method, live, worklist);
        }
    }
}

static void _optimize_markescapedcallables_live(optimizer *opt, value *regs, registerindx start, int count, dictionary *live, varray_value *worklist) {
    for (registerindx i=0; i<count; i++) {
        _optimize_marklivecallable(opt, regs[start+i], live, worklist);
    }
}

static bool _optimize_isuntrustedbuiltin(value val) {
    if (!MORPHO_ISBUILTINFUNCTION(val)) return false;

    objectbuiltinfunction *builtin = MORPHO_GETBUILTINFUNCTION(val);
    return ((builtin->flags & MORPHO_FN_REENTRANT) ||
            (builtin->flags == BUILTIN_FLAGSEMPTY));
}

static bool _optimize_labelmatchesuntrustedbuiltin(optimizer *opt, value label) {
    if (!MORPHO_ISSTRING(label)) return false;

    for (unsigned int i=0; i<opt->prog->classes.count; i++) {
        objectclass *klass = MORPHO_GETCLASS(opt->prog->classes.data[i]);
        value method;

        if (morpho_lookupmethod(MORPHO_OBJECT(klass), label, &method) &&
            _optimize_isuntrustedbuiltin(method)) {
            return true;
        }
    }

    return false;
}

typedef struct {
    optimizer *opt;
    dictionary *live;
    varray_value *worklist;
    bool *disablemethodpruning;
} _optimize_functionscanctx;

typedef void (*_optimize_functionscanstepfn) (_optimize_functionscanctx *fnctx, instruction instr, value *regs);

typedef struct {
    instruction op;
    _optimize_functionscanstepfn fn;
    const char *leftlabel;
    const char *rightlabel;
} _optimize_functionscanstep;

static void _optimize_preserveuntrustedbuiltinmethods(_optimize_functionscanctx *fnctx, value callee) {
    if (!fnctx->disablemethodpruning) return;

    if (_optimize_isuntrustedbuiltin(callee) ||
        _optimize_labelmatchesuntrustedbuiltin(fnctx->opt, callee)) {
        if (!*fnctx->disablemethodpruning) {
            *fnctx->disablemethodpruning = true;
            _optimize_markallmethods_live(fnctx->opt, fnctx->live, fnctx->worklist);
        }
    }
}

static void _optimize_functionscan_onglobalstore(instruction instr, value val, void *ctx) {
    _optimize_functionscanctx *fnctx = (_optimize_functionscanctx *) ctx;
    _optimize_marklivecallable(fnctx->opt, val, fnctx->live, fnctx->worklist);
}

static void _optimize_functionscan_opcall(_optimize_functionscanctx *fnctx, instruction instr, value *regs) {
    registerindx a = DECODE_A(instr);
    int nargs = DECODE_B(instr), nopt = DECODE_C(instr);

    _optimize_preserveuntrustedbuiltinmethods(fnctx, regs[a]);
    _optimize_marklivecallable(fnctx->opt, regs[a], fnctx->live, fnctx->worklist);
    _optimize_markescapedcallables_live(fnctx->opt, regs, a+1, nargs+2*nopt, fnctx->live, fnctx->worklist);
}

static void _optimize_functionscan_opmethod(_optimize_functionscanctx *fnctx, instruction instr, value *regs) {
    registerindx a = DECODE_A(instr);
    int nargs = DECODE_B(instr), nopt = DECODE_C(instr);

    _optimize_preserveuntrustedbuiltinmethods(fnctx, regs[a]);
    if (MORPHO_ISSTRING(regs[a])) {
        _optimize_marklabelmethods_live(fnctx->opt, regs[a], fnctx->live, fnctx->worklist, fnctx->disablemethodpruning);
    } else {
        _optimize_marklivecallable(fnctx->opt, regs[a], fnctx->live, fnctx->worklist);
    }
    _optimize_markescapedcallables_live(fnctx->opt, regs, a+2, nargs+2*nopt, fnctx->live, fnctx->worklist);
}

static void _optimize_functionscan_opinvoke(_optimize_functionscanctx *fnctx, instruction instr, value *regs) {
    registerindx a = DECODE_A(instr);
    int nargs = DECODE_B(instr), nopt = DECODE_C(instr);

    _optimize_preserveuntrustedbuiltinmethods(fnctx, regs[a]);
    _optimize_marklabelmethods_live(fnctx->opt, regs[a], fnctx->live, fnctx->worklist, fnctx->disablemethodpruning);
    _optimize_markescapedcallables_live(fnctx->opt, regs, a+2, nargs+2*nopt, fnctx->live, fnctx->worklist);
}

static void _optimize_functionscan_opreturn(_optimize_functionscanctx *fnctx, instruction instr, value *regs) {
    registerindx a = DECODE_A(instr);

    if (a>0) _optimize_marklivecallable(fnctx->opt, regs[DECODE_B(instr)], fnctx->live, fnctx->worklist);
}

static void _optimize_functionscan_opclosure(_optimize_functionscanctx *fnctx, instruction instr, value *regs) {
    _optimize_marklivecallable(fnctx->opt, regs[DECODE_A(instr)], fnctx->live, fnctx->worklist);
}

static void _optimize_functionscan_oplpr(_optimize_functionscanctx *fnctx, instruction instr, value *regs) {
    _optimize_marklabelmethods_live(fnctx->opt, regs[DECODE_C(instr)], fnctx->live, fnctx->worklist, fnctx->disablemethodpruning);
}

static void _optimize_functionscan_opspr(_optimize_functionscanctx *fnctx, instruction instr, value *regs) {
    _optimize_marklivecallable(fnctx->opt, regs[DECODE_C(instr)], fnctx->live, fnctx->worklist);
}

static void _optimize_functionscan_opsup(_optimize_functionscanctx *fnctx, instruction instr, value *regs) {
    _optimize_marklivecallable(fnctx->opt, regs[DECODE_B(instr)], fnctx->live, fnctx->worklist);
}

static void _optimize_functionscan_opcat(_optimize_functionscanctx *fnctx, instruction instr, value *regs) {
    objectstring tostringlabel = MORPHO_STATICSTRING("tostring");

    (void) instr;
    (void) regs;

    /* `cat` may stringify objects via `tostring`, so keep those methods live. */
    _optimize_markmethodsforlabel_live(fnctx->opt, MORPHO_OBJECT(&tostringlabel), fnctx->live, fnctx->worklist);
}

static _optimize_functionscanstep _optimize_functionscansteps[] = {
    { OP_CALL, _optimize_functionscan_opcall, NULL, NULL },
    { OP_METHOD, _optimize_functionscan_opmethod, NULL, NULL },
    { OP_INVOKE, _optimize_functionscan_opinvoke, NULL, NULL },
    { OP_RETURN, _optimize_functionscan_opreturn, NULL, NULL },
    { OP_CLOSURE, _optimize_functionscan_opclosure, NULL, NULL },
    { OP_LPR, _optimize_functionscan_oplpr, NULL, NULL },
    { OP_SPR, _optimize_functionscan_opspr, NULL, NULL },
    { OP_SUP, _optimize_functionscan_opsup, NULL, NULL },
    { OP_CAT, _optimize_functionscan_opcat, NULL, NULL },
    { OP_ADD, NULL, MORPHO_ADD_METHOD, MORPHO_ADDR_METHOD },
    { OP_SUB, NULL, MORPHO_SUB_METHOD, MORPHO_SUBR_METHOD },
    { OP_MUL, NULL, MORPHO_MUL_METHOD, MORPHO_MULR_METHOD },
    { OP_DIV, NULL, MORPHO_DIV_METHOD, MORPHO_DIVR_METHOD },
    { OP_POW, NULL, MORPHO_POW_METHOD, MORPHO_POWR_METHOD },
    { OP_END, NULL, NULL, NULL }
};

static void _optimize_functionscan_step(instruction instr, value *regs, void *ctx) {
    _optimize_functionscanctx *fnctx = (_optimize_functionscanctx *) ctx;
    instruction op = DECODE_OP(instr);

    for (int i=0; _optimize_functionscansteps[i].op!=OP_END; i++) {
        if (_optimize_functionscansteps[i].op==op) {
            if (_optimize_functionscansteps[i].fn) {
                _optimize_functionscansteps[i].fn(fnctx, instr, regs);
            } else {
                objectstring left = MORPHO_STATICSTRING((char *) _optimize_functionscansteps[i].leftlabel);
                objectstring right = MORPHO_STATICSTRING((char *) _optimize_functionscansteps[i].rightlabel);

                _optimize_markmethodsforlabel_live(fnctx->opt, MORPHO_OBJECT(&left), fnctx->live, fnctx->worklist);
                _optimize_markmethodsforlabel_live(fnctx->opt, MORPHO_OBJECT(&right), fnctx->live, fnctx->worklist);
            }
            return;
        }
    }
}

static void optimize_functionliveness(optimizer *opt, dictionary *live, bool *disablemethodpruning) {
    varray_value worklist;
    _optimize_functionscanctx fnctx = {
        .opt=opt,
        .live=live,
        .worklist=&worklist,
        .disablemethodpruning=disablemethodpruning
    };

    *disablemethodpruning=false;
    dictionary_init(live);
    varray_valueinit(&worklist);
    globalinfolist_startpass(&opt->glist);

    _optimize_marklivefunction(opt->prog->global, live, &worklist);
    for (blockindx i=0; i<opt->graph.count; i++) {
        block *blk = &opt->graph.data[i];

        if (!block_isentry(blk) || blk->func==opt->prog->global) continue;
        if (_optimize_functionescapes(opt, blk->func)) {
            _optimize_marklivefunction(blk->func, live, &worklist);
        }
    }

    while (worklist.count>0) {
        value fnval;
        varray_valuepop(&worklist, &fnval);
        if (MORPHO_ISFUNCTION(fnval)) {
            _optimize_ipascanstate state = {
                .opt=opt,
                .func=MORPHO_GETFUNCTION(fnval),
                .globals=&opt->glist,
                .trackglobalstores=true,
                .onglobalstore=_optimize_functionscan_onglobalstore,
                .oncalllike=_optimize_functionscan_step,
                .oninstruction=_optimize_functionscan_step,
                .ctx=&fnctx
            };
            _optimize_ipascanfunction(&state);
        }
    }

    varray_valueclear(&worklist);
}

static void optimize_prunedeadfunctionblocks(optimizer *opt, dictionary *live, bool disablemethodpruning) {
    for (blockindx i=0; i<opt->graph.count; i++) {
        block *blk = &opt->graph.data[i];
        value key;
        bool islive, escapes, keepmethods;

        if (!block_isentry(blk) || blk->func==opt->prog->global) continue;

        key = MORPHO_OBJECT(blk->func);
        islive = dictionary_get(live, key, NULL);
        escapes = _optimize_functionescapes(opt, blk->func);
        keepmethods = (disablemethodpruning && blk->func->klass);

        if (islive) continue;
        if (escapes) continue;
        if (keepmethods) continue;

        blk->isentry=false;
        opt->reachabledirty=true;
        _pruneunreachableblock(opt, i);
    }
}

static bool _optimize_isdeadclass(optimizer *opt, objectclass *klass, dictionary *cache) {
    value cached;
    if (dictionary_get(cache, MORPHO_OBJECT(klass), &cached) && MORPHO_ISINTEGER(cached)) {
        return (MORPHO_GETINTEGERVALUE(cached)!=0);
    }

    if (classinfolist_countconstructed(&opt->classinfo, klass)>0) return false;

    for (unsigned int i=0; i<klass->children.count; i++) {
        value child = klass->children.data[i];

        if (MORPHO_ISCLASS(child) &&
            !_optimize_isdeadclass(opt, MORPHO_GETCLASS(child), cache)) {
            dictionary_insert(cache, MORPHO_OBJECT(klass), MORPHO_INTEGER(0));
            return false;
        }
    }

    dictionary_insert(cache, MORPHO_OBJECT(klass), MORPHO_INTEGER(1));
    return true;
}

static bool _optimize_isdeadclassmethod(optimizer *opt, block *blk, dictionary *cache) {
    return (blk->func->klass &&
            _optimize_isdeadclass(opt, blk->func->klass, cache));
}

static void optimize_prunedeadclassblocks(optimizer *opt) {
    dictionary cache;
    dictionary_init(&cache);

    for (blockindx i=0; i<opt->graph.count; i++) {
        block *blk = &opt->graph.data[i];

        if (!block_isentry(blk) || !_optimize_isdeadclassmethod(opt, blk, &cache)) continue;

        blk->isentry=false;
        opt->reachabledirty=true;
        _pruneunreachableblock(opt, i);
    }

    dictionary_clear(&cache);
}

void _optusagefn(registerindx r, void *ref) {
    reginfolist *rinfo = (reginfolist *) ref;
    
    reginfolist_incread(rinfo, r);
}

/** Updates reginfo usage information for an insertion */
void optimize_usageforinsertion(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    int n=DECODE_A(instr);
    instructionindx start=DECODE_Bx(instr);
    
    for (int i=0; i<n; i++) {
        instr = opt->insertions.data[start+i];
        opcode_usageforinstruction(opt->currentblk, instr, _optusagefn, &opt->rlist);
    }
}

/** Updates reginfo usage information based on the opcode */
void optimize_usage(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    if (DECODE_OP(instr)!=OP_INSERT) {
        opcode_usageforinstruction(opt->currentblk, instr, _optusagefn, &opt->rlist);
    } else optimize_usageforinsertion(opt);
}

/** Updates reginfo tracking information for an insertion */
void optimize_trackforinsertion(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    int n=DECODE_A(instr);
    instructionindx start=DECODE_Bx(instr);
    
    for (int i=0; i<n; i++) {
        instruction iinstr = opt->insertions.data[start+i];
        opt->current=iinstr; // Patch in the inserted instruction so that the tracking fn gets it
        opcodetrackingfn trackingfn = opcode_gettrackingfn(DECODE_OP(iinstr));
        if (trackingfn) trackingfn(opt);
    }
    
    opt->current=instr; // Restore current instruction
}

/** Tracks register content for the current instruction */
void optimize_track(optimizer *opt) {
    instruction op=DECODE_OP(opt->current);
    if (op!=OP_INSERT) {
        opcodetrackingfn trackingfn = opcode_gettrackingfn(DECODE_OP(opt->current));
        if (trackingfn) trackingfn(opt);
    } else {
        optimize_trackforinsertion(opt);
    }
}

/** Checks if a block contains any insertions*/
bool optimize_hasinsertions(optimizer *opt, block *blk) {
    for (instructionindx i=blk->start; i<=blk->end; i++) {
        if (DECODE_OP(optimize_getinstructionat(opt, i))==OP_INSERT) return true;
    }
    return false;
}

/** Disassembles the current instruction */
void optimize_disassemble(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    debugger_disassembleinstruction(NULL, instr, opt->pc, NULL, NULL);
    printf("\n");
}

static void _optimize_markreachable(optimizer *opt, blockindx blkindx, dictionary *reachable) {
    block *blk;
    value indx = MORPHO_INTEGER(blkindx);

    if (dictionary_get(reachable, indx, NULL)) return;
    dictionary_insert(reachable, indx, MORPHO_NIL);

    if (!cfgraph_indx(&opt->graph, blkindx, &blk)) return;

    for (int i=0; i<blk->dest.capacity; i++) {
        value key = blk->dest.contents[i].key;
        if (!MORPHO_ISINTEGER(key)) continue;
        _optimize_markreachable(opt, MORPHO_GETINTEGERVALUE(key), reachable);
    }
}

static void _optimize_refreshreachable(optimizer *opt) {
    if (!opt->reachabledirty) return;

    dictionary_clear(&opt->reachable);
    dictionary_init(&opt->reachable);
    if (opt->graph.count==0) {
        opt->reachabledirty=false;
        return;
    }

    for (blockindx i=0; i<opt->graph.count; i++) {
        block *entry;
        if (cfgraph_indx(&opt->graph, i, &entry) && block_isentry(entry)) {
            _optimize_markreachable(opt, i, &opt->reachable);
        }
    }

    opt->reachabledirty=false;
}

bool optimize_blockisreachable(optimizer *opt, block *blk) {
    blockindx blkindx;

    if (!cfgraph_findindx(&opt->graph, blk, &blkindx)) return false;
    _optimize_refreshreachable(opt);
    return dictionary_get(&opt->reachable, MORPHO_INTEGER(blkindx), NULL);
}

static void _pruneunreachableblock(optimizer *opt, blockindx blkindx) {
    block *blk;
    if (!cfgraph_indx(&opt->graph, blkindx, &blk) ||
        optimize_blockisreachable(opt, blk)) return;

    for (int i=0; i<blk->dest.capacity; i++) {
        value key = blk->dest.contents[i].key;
        if (!MORPHO_ISINTEGER(key)) continue;

        blockindx destindx = MORPHO_GETINTEGERVALUE(key);
        if (cfgraph_disconnect(blk, destindx, &opt->graph)) {
            _pruneunreachableblock(opt, destindx);
        }
    }
}

static void _repairconditionalbranch(optimizer *opt, instruction instr, bool removetargetedge) {
    blockindx targetindx = opt->currentblk->branch;
    blockindx fallthroughindx = opt->currentblk->fallthrough;
    blockindx removeindx;

    if (targetindx==BLOCKINDX_EMPTY || fallthroughindx==BLOCKINDX_EMPTY) return;

    /* If the branch target is the fallthrough block, rewriting or erasing the
       conditional does not change CFG connectivity. */
    if (fallthroughindx==targetindx) return;

    removeindx = (removetargetedge ? targetindx : fallthroughindx);
    if (cfgraph_disconnect(opt->currentblk, removeindx, &opt->graph)) {
        opt->reachabledirty=true;
        _pruneunreachableblock(opt, removeindx);
    }
}

void optimize_repairerasedconditionalbranch(optimizer *opt, instruction instr) {
    _repairconditionalbranch(opt, instr, true);
}

void optimize_repairtakenconditionalbranch(optimizer *opt, instruction instr) {
    _repairconditionalbranch(opt, instr, false);
}

bool _checkdestusage(optimizer *opt, block *blk, registerindx rindx, dictionary *checked) {
    blockindx blkindx;
    if (!cfgraph_findindx(&opt->graph, blk, &blkindx)) return false;
    
    dictionary_insert(checked, MORPHO_INTEGER(blkindx), MORPHO_NIL); // Mark this dictionary as checked
    
    for (int i=0; i<blk->dest.capacity; i++) {
        value key = blk->dest.contents[i].key;
        block *dest;
        
        if (MORPHO_ISINTEGER(key) &&
            !dictionary_get(checked, key, NULL) && // Ensure the block hasn't been checked
            cfgraph_indx(&opt->graph, MORPHO_GETINTEGERVALUE(key), &dest)) {
            
            if (block_uses(dest, rindx)) return true;
            
            if (!block_writes(dest, rindx) &&
                _checkdestusage(opt, dest, rindx, checked)) return true;
        }
    }
    
    return false;
}

/** Checks usage of a register by subsequent blocks; returns true if it's used */
bool optimize_checkdestusage(optimizer *opt, block *blk, registerindx rindx) {
    dictionary checked;
    dictionary_init(&checked);
    bool success=_checkdestusage(opt, blk, rindx, &checked);
    dictionary_clear(&checked);
    return success; 
}

static bool _isdeadstoresafearithmetictype(value type) {
    return (MORPHO_ISEQUAL(type, typeint) ||
            MORPHO_ISEQUAL(type, typefloat));
}

bool optimize_candeletedeadstore(optimizer *opt, instruction instr, registerindx r) {
    instruction op = DECODE_OP(instr);
    
    if (op==OP_NOP || op==OP_MOV || op==OP_LCT) return true;
    if (op<OP_ADD || op>OP_POW) return false;
    
    return _isdeadstoresafearithmetictype(optimize_type(opt, r));
}

static bool _ispreservedentryregister(objectfunction *func, registerindx r) {
    if (func->klass) return (r<=func->nargs);
    return (r>0 && r<=func->nargs);
}

/** Optimizations performed at the end of a code block */
void optimize_dead_store_elimination(optimizer *opt, block *blk) {
    if (opt->verbose) printf("Ending block\n");
    
    for (int i=0; i<opt->rlist.nreg; i++) {
        instructionindx src;
        
        if (!optimize_isempty(opt, i) &&                // Does the register contain something?
            reginfolist_countuses(&opt->rlist, i)==0 && // Is it being used in the block?
            !_ispreservedentryregister(blk->func, i) && // It's not a preserved entry slot
            !optimize_checkdestusage(opt, blk, i) &&    // Is it being used elsewhere?
            reginfolist_source(&opt->rlist, i, &src) && // Identify the instruction that wrote it
            block_contains(blk, src)) { // Ensure instruction is in this block
            instruction instr = optimize_getinstructionat(opt, src);
            if (!optimize_candeletedeadstore(opt, instr, i)) continue;
            
            bool deleted = optimize_deleteinstruction(opt, src); // Deletes the instruction, checking for side effects
            
            if (deleted && opt->verbose) {
                printf("Deleted instruction: ");
                debugger_disassembleinstruction(NULL, instr, src, NULL, NULL);
                printf("\n");
            }
        }
    }
}

int optimize_countinsertions(optimizer *opt, block *blk) {
    int n=0;
    for (instructionindx i=blk->start; i<=blk->end; i++) {
        instruction instr = optimize_getinstructionat(opt, i);
        if (DECODE_OP(instr)==OP_INSERT) n+=DECODE_A(instr)-1; // Less one to account for the OP_INSERT instruction
    }
    return n;
}

void _fixannotation(optimizer *opt, instructionindx iindx, int ninsert) {
    varray_debugannotation *alist = &opt->prog->annotations;
    instructionindx i=0;
    
    for (unsigned int j=0; j<alist->count; j++) {
        debugannotation *ann = &alist->data[j];
        if (ann->type!=DEBUG_ELEMENT) continue;
            
        if (iindx>=i && iindx<i+ann->content.element.ninstr) {
            ann->content.element.ninstr+=ninsert;
            return;
        } else i+=ann->content.element.ninstr;
    }
}

/** Rebuilds a block inserting inserted code */
bool optimize_processinsertions(optimizer *opt, block *blk) {
    varray_instruction *code = &opt->prog->code;
    
    int ninsert = optimize_countinsertions(opt, blk);
    
    if (!varray_instructionresize(code, ninsert)) return false;
    
    // Move instructions after the block end
    instructionindx nmove = code->count - (blk->end+1);
    if (nmove) memmove(&code->data[blk->end+1+ninsert], &code->data[blk->end+1], sizeof(instruction)*nmove);
    
    instructionindx k=blk->end+ninsert; // Destination index
    // Loop backwards over block copying in insertions
    for (instructionindx i=blk->end; i>=blk->start; i--, k--) {
        instruction instr = optimize_getinstructionat(opt, i);
        if (DECODE_OP(instr)==OP_INSERT) {
            int n=DECODE_A(instr);
            k-=(n-1);
            memcpy(code->data+k, opt->insertions.data+DECODE_Bx(instr), n*sizeof(instruction));
            _fixannotation(opt, i, n-1);
        } else {
            code->data[k]=code->data[i];
        }
    }
    
    code->count+=ninsert;
    blk->end+=ninsert;
    opt->insertions.count=0; // Clear insertions
    
    // Fix starting and ending indices for subsequent blocks
    for (int i=0; i<opt->graph.count; i++) {
        block *b = &opt->graph.data[i];
        if (b->start>blk->start) { b->start+=ninsert; b->end+=ninsert; } 
    }
    
    if (opt->verbose) {
        printf("Expanded block [%ti - %ti]\n", blk->start, blk->end);
        for (instructionindx i=blk->start; i<=blk->end; i++) {
            instruction instr = optimize_getinstructionat(opt, i);
            debugger_disassembleinstruction(NULL, instr, i, NULL, NULL);
            printf("\n");
        }
    }
    
    return true;
}

/** Sets the contents of registers from knowledge of the function signature */
void optimize_signature(optimizer *opt) {
    objectfunction *func = optimize_currentblock(opt)->func;

    reginfolist_write(&opt->rlist, func->entry, 0, REG_VALUE, 0);
    opt->rlist.rinfo[0].iindx=INSTRUCTIONINDX_EMPTY;

    if (func->klass) {
        reginfolist_settypeinfo(&opt->rlist, 0, MORPHO_OBJECT(func->klass),
                                optimize_classisleaf(func->klass) ? REGTYPE_EXACT : REGTYPE_SUBTYPE);
    } else {
        reginfolist_settypeinfo(&opt->rlist, 0, typecallable, REGTYPE_SUBTYPE);
    }
    
    value type;
    for (registerindx i=0; i<func->nargs; i++) {
        reginfolist_write(&opt->rlist, func->entry, i+1, REG_VALUE, 0);
        opt->rlist.rinfo[i+1].iindx=INSTRUCTIONINDX_EMPTY;
        if (signature_getparamtype(&func->sig, i, &type)) {
            reginfolist_settypeinfo(&opt->rlist, i+1, type, REGTYPE_SUBTYPE);
        }
    }

    /* Optional parameters are initialized before function entry, but we model
       them conservatively as unknown values for now. */
    for (registerindx i=0; i<func->nopt; i++) {
        registerindx r = func->nargs + 1 + i;
        reginfolist_write(&opt->rlist, func->entry, r, REG_VALUE, 0);
        opt->rlist.rinfo[r].iindx=INSTRUCTIONINDX_EMPTY;
    }
}

static void optimize_joinblockinput(optimizer *opt, block *blk);
static void optimize_loadblockinput(optimizer *opt, block *blk);

/** Optimize a given block */
bool optimize_block(optimizer *opt, block *blk) {
    opt->currentblk=blk;
    
    do {
        opt->nchanged=0;
        
        if (opt->verbose) printf("Optimizing block [%ti - %ti]:\n", blk->start, blk->end);
        
        optimize_loadblockinput(opt, blk);
        
        for (instructionindx i=blk->start; i<=blk->end; i++) {
            instruction instr = optimize_fetch(opt, i);
            if (opt->verbose) optimize_disassemble(opt);
            
            // Update usage. @warning: This MUST be before optimization strategies so that usage information from this instruction is correct
            optimize_usage(opt);
            
            // Apply relevant optimization strategies given the pass number
            if (strategy_optimizeinstruction(opt, opt->pass)) {
                optimize_usage(opt); // Conservatively mark anything new as used
            }

            // Abort if we generated an insertion
            if (DECODE_OP(optimize_getinstruction(opt))==OP_INSERT) break;
            
            // Check if an error occurred and quit if it did
            if (optimize_checkerror(opt)) return false;
            
            // Perform tracking to track register contents from the optimized instruction
            optimize_track(opt);
            
            if (opt->verbose) reginfolist_show(&opt->rlist);
        }

        if (optimize_hasinsertions(opt, blk)) {
            optimize_processinsertions(opt, blk);
        } else optimize_dead_store_elimination(opt, blk);
    } while (opt->nchanged>0);
    
    // Finalize block information
    block_computeusage(blk, opt->prog->code.data); // Recompute usage
    
    return true;
}

/* **********************************************************************
 * Prepasses
 * ********************************************************************** */

/** Function type to initialize an optimizer prepass. */
typedef void (*prepassinitfn) (optimizer *opt);

/** Function type to visit an instruction during an optimizer prepass. */
typedef void (*prepassinstructionvisitfn) (optimizer *opt, block *blk, instruction instr);

/** Function type to visit a block during an optimizer prepass. */
typedef void (*prepassblockvisitfn) (optimizer *opt, block *blk);

/** Function type to finalize an optimizer prepass. */
typedef void (*prepassfinalizefn) (optimizer *opt);

typedef struct {
    instruction match;
    prepassinstructionvisitfn visit;
} prepassinstructionvisittable;

typedef struct {
    prepassinitfn init;
    prepassblockvisitfn visitblock;
    prepassinstructionvisittable *visitinstructiontable;
    prepassfinalizefn finalize;
} prepass;

/* -------------------------------------
 * Function structure metadata
 * ------------------------------------- */

/** Initializes per-pass function structure metadata. */
void optimize_functionstructure_init(optimizer *opt) {
    functioninfolist_startpass(&opt->functioninfo);
}

/** Records reachable blocks and instruction counts for each function. */
void optimize_functionstructure_visitblock(optimizer *opt, block *blk) {
    functioninfolist_addblock(&opt->functioninfo, blk->func, (int) (blk->end - blk->start + 1));
}

/* -------------------------------------
 * Interprocedural inputs
 * ------------------------------------- */

/** Clears derived call-site input facts before each analysis pass. */
void optimize_functioninputs_init(optimizer *opt) {
    optimize_clearfunctioninputs(opt);
}

/* -------------------------------------
 * Global usage
 * ------------------------------------- */

/** Initializes the global-usage prepass. */
void optimize_globalusage_init(optimizer *opt) {
    globalinfolist_startpass(&opt->glist);
}

/** Records a global read during prepass scanning. */
void optimize_globalread_visit(optimizer *opt, block *blk, instruction instr) {
    globalinfolist_read(&opt->glist, DECODE_Bx(instr));
}

/** Records a global store during prepass scanning. */
void optimize_globalstore_visit(optimizer *opt, block *blk, instruction instr) {
    globalinfolist_store(&opt->glist, DECODE_Bx(instr));
}

prepassinstructionvisittable globalusagevisitors[] = {
    { OP_LGL, optimize_globalread_visit },
    { OP_SGL, optimize_globalstore_visit },
    { OP_END, NULL }
};

/* -------------------------------------
 * Loop candidate marking
 * ------------------------------------- */

/** Initializes loop metadata before structural scanning. */
void optimize_loopcandidates_init(optimizer *opt) {
    for (int i=0; i<opt->graph.count; i++) {
        block_clearloopinfo(&opt->graph.data[i]);
    }
}

/** Marks structural back-edges from a block after the CFG has been built and sorted. */
void optimize_loopcandidates_visitblock(optimizer *opt, block *src) {
    blockindx i;

    if (!cfgraph_findindx(&opt->graph, src, &i)) return;

    for (int j=0; j<src->dest.capacity; j++) {
        value key = src->dest.contents[j].key;
        block *dest;
        blockindx dst;

        if (!MORPHO_ISINTEGER(key)) continue;
        dst = MORPHO_GETINTEGERVALUE(key);
        if (!cfgraph_indx(&opt->graph, dst, &dest)) continue;

        /* A backward edge is a cheap loop candidate that later passes can refine. */
        if (dest->func==src->func && dest->start<=src->start) {
            block_setloopsource(dest, i);
        }
    }
}

/* Walk backward from a structural backedge until we reach the header, marking loop blocks. */
static void _optimize_markloopblocks(optimizer *opt, block *header, blockindx curindx) {
    block *cur;

    if (block_inloop(header, curindx) ||
        !cfgraph_indx(&opt->graph, curindx, &cur) ||
        cur->func!=header->func) return;

    block_setloopblock(header, curindx);
    if (cur==header) return;

    for (int i=0; i<cur->src.capacity; i++) {
        value key = cur->src.contents[i].key;

        if (MORPHO_ISINTEGER(key)) {
            _optimize_markloopblocks(opt, header, (blockindx) MORPHO_GETINTEGERVALUE(key));
        }
    }
}

static bool _anyblockwrites(int nblk, block **blk, registerindx r) {
    for (int i=0; i<nblk; i++) {
        if (block_writes(blk[i], r)) return true;
    }

    return false;
}

static bool _loopwrites(optimizer *opt, block *header, registerindx r) {
    for (int i=0; i<header->loopblocks.capacity; i++) {
        value key = header->loopblocks.contents[i].key;
        block *blk;

        if (!MORPHO_ISINTEGER(key)) continue;
        if (cfgraph_indx(&opt->graph, (blockindx) MORPHO_GETINTEGERVALUE(key), &blk) &&
            block_writes(blk, r)) return true;
    }

    return false;
}

static bool _isintfact(block *blk, reginfo *info) {
    if (info->typeinfo==REGTYPE_EXACT && MORPHO_ISEQUAL(info->type, typeint)) return true;
    if (info->contents!=REG_CONSTANT) return false;

    value konst = block_getconstant(blk, info->indx);
    return MORPHO_ISINTEGER(konst);
}

/* Recognize loop-carried integer updates of the form `r = r +/- k` or `r = k + r`,
   where `k` is itself known to be an integer fact. */
static bool _isintpreservingloopupdate(optimizer *opt, block *blk, registerindx r) {
    reginfo *info = &blk->rout.rinfo[r];
    instruction op, write;

    if (info->iindx==INSTRUCTIONINDX_EMPTY) return false;

    write = optimize_getinstructionat(opt, info->iindx);
    op = DECODE_OP(write);
    if ((op!=OP_ADD && op!=OP_SUB) || DECODE_A(write)!=r) return false;

    registerindx other;
    if (DECODE_B(write)==r) {
        other=DECODE_C(write);
    } else if (op==OP_ADD && DECODE_C(write)==r) {
        other=DECODE_B(write);
    } else return false;

    if (other>=blk->rout.nreg) return false;

    return _isintfact(blk, &blk->rout.rinfo[other]);
}

static void _preserveexactintfact(reginfo *info) {
    info->contents=REG_TYPEDVALUE;
    info->indx=0;
    info->iindx=INSTRUCTIONINDX_EMPTY;
    info->hasalias=false;
    info->alias=0;
}

/** Builds loop-body metadata for each structural loop header. */
void optimize_loopcandidates_finalize(optimizer *opt) {
    for (int i=0; i<opt->graph.count; i++) {
        block *header = &opt->graph.data[i];

        if (!block_isloopheader(header)) continue;
        /* Seed the backward walk from each structural backedge predecessor. */
        for (int j=0; j<header->loopsrc.capacity; j++) {
            value key = header->loopsrc.contents[j].key;

            if (!MORPHO_ISINTEGER(key)) continue;
            _optimize_markloopblocks(opt, header, (blockindx) MORPHO_GETINTEGERVALUE(key));
        }
    }
}

/* -------------------------------------
 * Pre-pass table
 * ------------------------------------- */

prepass prepasses[] = {
    { optimize_functionstructure_init, optimize_functionstructure_visitblock, NULL, NULL },
    { optimize_functioninputs_init, NULL, NULL, NULL },
    { optimize_globalusage_init, NULL, globalusagevisitors, NULL },
    { optimize_loopcandidates_init, optimize_loopcandidates_visitblock, NULL, optimize_loopcandidates_finalize },
    { NULL, NULL, NULL, NULL }
};

/** Run all optimizer prepasses for the current pass. */
void optimize_runprepasses(optimizer *opt) {
    int nprepasses=0;
    while (prepasses[nprepasses].init) nprepasses++;
    
    for (int i=0; i<nprepasses; i++) prepasses[i].init(opt);

    for (int i=0; i<opt->graph.count && !optimize_checkerror(opt); i++) {
        block *blk = &opt->graph.data[i];
        if (!optimize_blockisreachable(opt, blk)) continue;

        for (int k=0; k<nprepasses && !optimize_checkerror(opt); k++) {
            if (prepasses[k].visitblock) prepasses[k].visitblock(opt, blk);
        }

        for (instructionindx j=blk->start; j<=blk->end && !optimize_checkerror(opt); j++) {
            instruction instr = optimize_getinstructionat(opt, j);
            instruction op = DECODE_OP(instr);

            for (int k=0; k<nprepasses && !optimize_checkerror(opt); k++) {
                prepassinstructionvisittable *visitinstructiontable = prepasses[k].visitinstructiontable;

                if (!visitinstructiontable) continue;
                for (int l=0; visitinstructiontable[l].visit; l++) {
                    if (visitinstructiontable[l].match==op || visitinstructiontable[l].match==OP_ANY) visitinstructiontable[l].visit(opt, blk, instr);
                }
            }
        }
    }

    for (int i=0; i<nprepasses; i++) if (prepasses[i].finalize) prepasses[i].finalize(opt);
}

/* **********************************************************************
 * Dataflow analysis
 * ********************************************************************** */

static void _prepareboundaryfact(reginfo *info) {
    info->usage=REGUSE_NONE;
    info->hasalias=false;
    info->alias=0;

    if (info->contents==REG_NOFACT || info->contents==REG_TYPEDVALUE || info->contents==REG_VALUE) {
        info->indx=0;
        info->iindx=INSTRUCTIONINDX_EMPTY;
    }
    if (MORPHO_ISNIL(info->type)) info->typeinfo=REGTYPE_UNKNOWN;
}

static reginfo _resolvejoinfact(int n, block **src, int rindx) {
    reginfo joined = src[0]->rout.rinfo[rindx];

    _prepareboundaryfact(&joined);
    for (int k=1; k<n; k++) {
        reginfo_join(&joined, &src[k]->rout.rinfo[rindx]);
    }

    // Register aliases are local facts; drop them conservatively at block boundaries.
    joined.hasalias=false;
    joined.alias=0;
    return joined;
}

static void _resolve(int n, block **src, reginfolist *dest) {
    for (int i=0; i<dest->nreg; i++) {
        if (_ispreservedentryregister(src[0]->func, i)) continue;
        dest->rinfo[i]=_resolvejoinfact(n, src, i);
    }
}

static bool _isloopbackedgepred(block *blk, blockindx srcindx) {
    return dictionary_get(&blk->loopsrc, MORPHO_INTEGER((int) srcindx), NULL);
}

static void _resolveloopheader(optimizer *opt, block *blk, int nsrc, block **src, reginfolist *dest) {
    block *entrypred[nsrc];
    block *backpred[nsrc];
    int nentry=0, nback=0;

    for (int i=0; i<nsrc; i++) {
        blockindx srcindx;

        if (!cfgraph_findindx(&opt->graph, src[i], &srcindx)) continue;
        if (_isloopbackedgepred(blk, srcindx)) {
            backpred[nback++]=src[i];
        } else {
            entrypred[nentry++]=src[i];
        }
    }

    if (nentry==0 || nback==0) {
        _resolve(nsrc, src, dest);
        return;
    }

    _resolve(nentry, entrypred, dest);

    for (int i=0; i<dest->nreg; i++) {
        reginfo baseline, joined;
        bool preserve=true;

        if (_ispreservedentryregister(blk->func, i)) continue;

        baseline=dest->rinfo[i];
        _prepareboundaryfact(&baseline);

        preserve = !_loopwrites(opt, blk, i);

        if (preserve) {
            dest->rinfo[i]=baseline;
            continue;
        }

        preserve = _isintfact(blk, &baseline);
        for (int k=0; preserve && k<nback; k++) {
            preserve=_isintpreservingloopupdate(opt, backpred[k], i);
        }

        if (preserve) {
            _preserveexactintfact(&baseline);
            dest->rinfo[i]=baseline;
            continue;
        }

        joined=baseline;
        for (int k=0; k<nback; k++) {
            reginfo_join(&joined, &backpred[k]->rout.rinfo[i]);
        }
        joined.hasalias=false;
        joined.alias=0;
        dest->rinfo[i]=joined;
    }
}

static void optimize_applyfunctioninput(optimizer *opt, block *blk) {
    value ix;
    bool recursive;
    functioninputinfo *info;

    if (!dictionary_get(&opt->functioninputindx, MORPHO_OBJECT(blk->func), &ix) || !MORPHO_ISINTEGER(ix)) return;

    info = &opt->functioninputs.data[MORPHO_GETINTEGERVALUE(ix)];
    recursive = _optimize_functionisrecursive(opt, blk->func);

    for (registerindx i=0; i<info->input.nreg && i<opt->rlist.nreg; i++) {
        if (info->input.rinfo[i].contents!=REG_NOFACT) {
            reginfo incoming = info->input.rinfo[i];

            if (i>0 && recursive) {
                reginfo_weaken(&incoming);
            }

            _optimize_applyinputfact(opt, blk->func, i, &incoming);
        }
    }
}

static void optimize_joinblockinput(optimizer *opt, block *blk) {
    reginfolist_wipe(&opt->rlist, blk->func->nregs);
    
    optimize_signature(opt); // Restore function parameters
    optimize_applyfunctioninput(opt, blk);
    
    int nentry = blk->src.count;
    if (!block_isentry(blk) &&
        nentry>0) {
        block *srcblk[nentry]; // Unpack and find source blocks from the dictionary
        
        for (int i=0, k=0; i<blk->src.capacity; i++) {
            value key = blk->src.contents[i].key;
            blockindx srcindx;
            if (MORPHO_ISNIL(key)) continue;
            
            srcindx = MORPHO_GETINTEGERVALUE(key);
            if (!cfgraph_indx(&opt->graph, srcindx, &srcblk[k])) return;
            k++;
        }
        
        if (block_isloopheader(blk)) {
            _resolveloopheader(opt, blk, blk->src.count, srcblk, &opt->rlist);
        } else {
            _resolve(blk->src.count, srcblk, &opt->rlist);
        }
    }
}

static void optimize_loadblockinput(optimizer *opt, block *blk) {
    reginfolist_wipe(&opt->rlist, blk->func->nregs);
    reginfolist_copy(&blk->rin, &opt->rlist);
}

/** Simulates a block without applying rewrites to compute output facts from input facts. */
static void optimize_transferblock(optimizer *opt, block *blk) {
    opt->currentblk=blk;
    optimize_joinblockinput(opt, blk);
    reginfolist_copy(&opt->rlist, &blk->rin);

    for (instructionindx i=blk->start; i<=blk->end && !optimize_checkerror(opt); i++) {
        optimize_fetch(opt, i);
        optimize_usage(opt);
        optimize_track(opt);
    }

    reginfolist_copy(&opt->rlist, &blk->rout);
}

static void optimize_queueentryblocks(optimizer *opt, varray_instructionindx *worklist) {
    for (blockindx i=0; i<opt->graph.count; i++) {
        if (block_isentry(&opt->graph.data[i]) && optimize_blockisreachable(opt, &opt->graph.data[i])) {
            varray_instructionindxwrite(worklist, i);
        }
    }
}

/** Enqueues reachable successor blocks when a block's output facts change. */
static void optimize_queuesuccessors(optimizer *opt, block *blk, varray_instructionindx *worklist) {
    bool printed=false;

    for (int i=0; i<blk->dest.capacity; i++) {
        value key = blk->dest.contents[i].key;
        block *dest;
        blockindx bindx;

        if (!MORPHO_ISINTEGER(key)) continue;
        bindx = MORPHO_GETINTEGERVALUE(key);
        if (cfgraph_indx(&opt->graph, bindx, &dest) && optimize_blockisreachable(opt, dest)) {
            if (opt->verbose) {
                if (!printed) {
                    printf("  queue [%ti, %ti] ->", blk->start, blk->end);
                    printed=true;
                }
                printf(" [%ti, %ti]", dest->start, dest->end);
            }
            varray_instructionindxwrite(worklist, bindx);
        }
    }

    if (printed) printf("\n");
}

/** Runs inter-block dataflow until block input and output facts converge. */
static void optimize_dataflow(optimizer *opt) {
    varray_instructionindx worklist;
    bool visited[opt->graph.count];
    varray_instructionindxinit(&worklist);

    if (opt->verbose) printf("===Dataflow===\n");

    for (blockindx i=0; i<opt->graph.count; i++) {
        visited[i]=false;
    }

    for (blockindx i=0; i<opt->graph.count; i++) { // Add entry points
        if (block_isentry(&opt->graph.data[i])) {
            if (opt->verbose) {
                block *blk = &opt->graph.data[i];
                printf("Seed block [%ti, %ti]\n", blk->start, blk->end);
            }
            varray_instructionindxwrite(&worklist, i);
        }
    }

    while (worklist.count>0 && !optimize_checkerror(opt)) {
        instructionindx indx;
        varray_instructionindxpop(&worklist, &indx);
        block *blk;
        reginfolist oldrin, oldrout;
        bool firstvisit;
        bool rinchanged, routchanged;

        if (!cfgraph_indx(&opt->graph, indx, &blk)) continue;
        if (!optimize_blockisreachable(opt, blk)) continue;

        firstvisit = !visited[indx];
        visited[indx]=true;

        reginfolist_init(&oldrin, blk->func->nregs);
        reginfolist_init(&oldrout, blk->func->nregs);
        reginfolist_copy(&blk->rin, &oldrin);
        reginfolist_copy(&blk->rout, &oldrout);

        opt->ipachanged=false;
        optimize_transferblock(opt, blk);
        if (opt->ipachanged) optimize_queueentryblocks(opt, &worklist);
        rinchanged = !reginfolist_equal(&oldrin, &blk->rin);
        routchanged = !reginfolist_equal(&oldrout, &blk->rout);

        if (opt->verbose) {
            printf("Visit block [%ti, %ti] rin=%s rout=%s\n", blk->start, blk->end,
                   rinchanged ? "changed" : "same",
                   routchanged ? "changed" : "same");
            if (rinchanged) {
                printf("  rin old/new:\n");
                reginfolist_show(&oldrin);
                reginfolist_show(&blk->rin);
            }
            if (routchanged) {
                printf("  rout old/new:\n");
                reginfolist_show(&oldrout);
                reginfolist_show(&blk->rout);
            }
        }

        reginfolist_clear(&oldrin);
        reginfolist_clear(&oldrout);

        if (firstvisit || routchanged) optimize_queuesuccessors(opt, blk, &worklist);
    }

    varray_instructionindxclear(&worklist);
}

/* **********************************************************************
 * Optimization pass
 * ********************************************************************** */

/** Run an optimization pass */
void optimize_pass(optimizer *opt, int n) {
    optimize_runprepasses(opt);
    optimize_dataflow(opt);
    
    opt->pass=n;
    if (opt->verbose) printf("===Optimization pass %i===\n", n);
    for (int i=0; i<opt->graph.count && !optimize_checkerror(opt); i++) {
        block *blk = &opt->graph.data[i];
        if (!optimize_blockisreachable(opt, blk)) continue;
        optimize_block(opt, blk);
    }
}

/* **********************************************************************
 * Optimizer 
 * ********************************************************************** */

/** Public interface to optimizer */
bool optimize(program *in) {
    optimizer opt;
    dictionary livefunctions;
    bool disablemethodpruning=false;
    
    optimizer_init(&opt, in);
    
    optimize_methodinfo(&opt);
    
    if (opt.verbose) morpho_disassemble(NULL, in, NULL);
    
    // Build control flow graph
    cfgraph_build(in, &opt.graph, opt.verbose);
    optimize_classinfo(&opt);
    optimize_prunedeadclassblocks(&opt);
    
    // Perform optimization passes
    for (int i=0; i<3 && !optimize_checkerror(&opt); i++) {
        optimize_pass(&opt, i);
    }

    if (!optimize_checkerror(&opt)) {
        optimize_functionliveness(&opt, &livefunctions, &disablemethodpruning);
        optimize_prunedeadfunctionblocks(&opt, &livefunctions, disablemethodpruning);
        dictionary_clear(&livefunctions);
    }
    
    if (opt.verbose) globalinfolist_show(&opt.glist);
    
    bool success=!optimize_checkerror(&opt);
    
    // Layout final code and repair associated data structures
    if (success) layout(&opt);
    
    optimize_clear(&opt);
    
    return success;
}

/* **********************************************************************
 * Initialization/Finalization
 * ********************************************************************** */

value typeint, typelist, typefloat, typestring, typebool, typeclosure, typerange, typetuple, typeclass, typecallable;

void bytecodeoptimizer_initialize(void) {
    morpho_setoptimizer(optimize);
    opcode_initialize();

    objectstring boollabel = MORPHO_STATICSTRING(BOOL_CLASSNAME);
    typebool = builtin_findclass(MORPHO_OBJECT(&boollabel));
    
    objectstring intlabel = MORPHO_STATICSTRING(INT_CLASSNAME);
    typeint = builtin_findclass(MORPHO_OBJECT(&intlabel));
    
    objectstring floatlabel = MORPHO_STATICSTRING(FLOAT_CLASSNAME);
    typefloat = builtin_findclass(MORPHO_OBJECT(&floatlabel));
    
    objectstring stringlabel = MORPHO_STATICSTRING(STRING_CLASSNAME);
    typestring = builtin_findclass(MORPHO_OBJECT(&stringlabel));
    
    objectstring closurelabel = MORPHO_STATICSTRING(CLOSURE_CLASSNAME);
    typeclosure = builtin_findclass(MORPHO_OBJECT(&closurelabel));
    
    objectstring rangelabel = MORPHO_STATICSTRING(RANGE_CLASSNAME);
    typerange = builtin_findclass(MORPHO_OBJECT(&rangelabel));
    
    objectstring listlabel = MORPHO_STATICSTRING(LIST_CLASSNAME);
    typelist = builtin_findclass(MORPHO_OBJECT(&listlabel));
    
    objectstring tuplelabel = MORPHO_STATICSTRING(TUPLE_CLASSNAME);
    typetuple = builtin_findclass(MORPHO_OBJECT(&tuplelabel));
    
    objectstring classlabel = MORPHO_STATICSTRING(CLASS_CLASSNAME);
    typeclass = builtin_findclass(MORPHO_OBJECT(&classlabel));

    objectstring callablelabel = MORPHO_STATICSTRING(CALLABLE_CLASSNAME);
    typecallable = builtin_findclass(MORPHO_OBJECT(&callablelabel));

}

void bytecodeoptimizer_finalize(void) {
}
