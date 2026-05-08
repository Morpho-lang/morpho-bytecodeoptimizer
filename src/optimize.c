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
    methodinfolist_init(&opt->methodinfo);
    varray_instructioninit(&opt->insertions);
    
    opt->v=morpho_newvm();
    opt->temp=morpho_newprogram();
    
#ifdef OPTIMIZER_VERBOSE
    opt->verbose=true;
#else
    opt->verbose=false;
#endif
}

/** Clears an optimizer data structure */
void optimize_clear(optimizer *opt) {
    error_clear(&opt->err);
    cfgraph_clear(&opt->graph);
    dictionary_clear(&opt->reachable);
    reginfolist_clear(&opt->rlist);
    globalinfolist_clear(&opt->glist);
    classinfolist_clear(&opt->classinfo);
    methodinfolist_clear(&opt->methodinfo);
    varray_instructionclear(&opt->insertions);
    
    if (opt->v) morpho_freevm(opt->v);
    if (opt->temp) morpho_freeprogram(opt->temp);
}

/* **********************************************************************
 * Methodinfo
 * ********************************************************************** */

// Adds a method into the methodinfo list
void _processfunction(objectfunction *fn, methodinfolist *out) {
    methodinfolist_incrementowners(out, fn);
}

// Searches a metafunction for methods
void _processmetafunction(objectmetafunction *mfn, methodinfolist *out) {
    for (int i=0; i<mfn->fns.count; i++) {
        value fn = mfn->fns.data[i];
        if (MORPHO_ISFUNCTION(fn)) {
            _processfunction(MORPHO_GETFUNCTION(fn), out);
        }
    }
}

// Searches a class's method table
void _processclass(objectclass *klass, methodinfolist *out) {
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
        _processclass(MORPHO_GETCLASS(klasses->data[i]), &opt->methodinfo);
    }
}

/* Retrieve the count of method owners */
int optimize_methodcountowners(optimizer *opt, objectfunction *method) {
    return methodinfolist_countowners(&opt->methodinfo, method);
}

/* Retrieve the count of constructor calls for a class. */
int optimize_classcountconstructed(optimizer *opt, objectclass *klass) {
    return classinfolist_countconstructed(&opt->classinfo, klass);
}

/* **********************************************************************
 * Optimize a code block
 * ********************************************************************** */

static void optimize_loadblockinput(optimizer *opt, block *blk);
void optimize_track(optimizer *opt);
static void _optimize_classinfo_processcomponent(optimizer *opt, value comp, dictionary *components);

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

/** Callback function to set the type of a register */
void optimize_settype(optimizer *opt, registerindx r, value type) {
    reginfolist_settype(&opt->rlist, r, type);
}

/** Callback function to set the type and precision of a register */
void optimize_settypeinfo(optimizer *opt, registerindx r, value type, regtypeinfo info) {
    reginfolist_settypeinfo(&opt->rlist, r, type, info);
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

/** Callback function to get a constant from the current constant table */
value optimize_getconstant(optimizer *opt, indx i) {
    return block_getconstant(opt->currentblk, i);
}

/** Adds a constant to the current constant table */
bool optimize_addconstant(optimizer *opt, value val, indx *out) {
    objectfunction *func = opt->currentblk->func;
    
    // Does the constant already exist?
    unsigned int k;
    if (varray_valuefindsame(&func->konst, val, &k)) {
        *out = (indx) k;
        return true;
    }
    
    // If not add it
    if (!varray_valueadd(&func->konst, &val, 1)) return false;
        
    *out=func->konst.count-1;
    
    if (MORPHO_ISOBJECT(val)) { // Bind the object to the program
        program_bindobject(opt->prog, MORPHO_GETOBJECT(val));
    }
    
    return true;
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

    return (MORPHO_GETCLASS(type)->children.count==0);
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
    if (optimize_typefromvalue(konst, &type)) optimize_settype(opt, r, type);
    
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

/* Track direct top-level constructor flows through constants, moves, and globals. */
static void _optimize_classinfo_scanfunction(optimizer *opt, objectfunction *func, classinfolist *out) {
    value regs[MORPHO_MAXREGISTERS];
    value *globals = MORPHO_MALLOC(sizeof(value)*opt->prog->globals.count);

    for (int i=0; i<MORPHO_MAXREGISTERS; i++) regs[i]=MORPHO_NIL;
    for (int i=0; i<opt->prog->globals.count; i++) globals[i]=MORPHO_NIL;

    for (instructionindx i=func->entry; i<opt->prog->code.count; i++) {
        instruction instr = opt->prog->code.data[i];
        instruction op = DECODE_OP(instr);
        registerindx a = DECODE_A(instr);
        int nargs = DECODE_B(instr), nopt = DECODE_C(instr);

        switch (op) {
            case OP_LCT: regs[a] = func->konst.data[DECODE_Bx(instr)]; break;
            case OP_MOV: regs[a] = regs[DECODE_B(instr)]; break;
            case OP_SGL: globals[DECODE_Bx(instr)] = regs[DECODE_A(instr)]; break;
            case OP_LGL: regs[a] = globals[DECODE_Bx(instr)]; break;
            case OP_INVOKE:
            case OP_METHOD:
            case OP_CALL: {
                registerindx receiver = a + (op==OP_CALL ? 0 : 1);

                _optimize_classinfo_mark(regs[receiver], out);
                _optimize_classinfo_markregister(regs, receiver+1, nargs+2*nopt, out);
                regs[receiver] = MORPHO_NIL;
                break;
            }
            default: {
                registerindx overwrites;
                if (opcode_overwritesforinstruction(instr, &overwrites)) regs[overwrites]=MORPHO_NIL;
                break;
            }
        }
    }

    MORPHO_FREE(globals);
}

static void _optimize_classinfo_processcomponent(optimizer *opt, value comp, dictionary *components) {
    if (!MORPHO_ISCALLABLE(comp) || dictionary_get(components, comp, NULL)) return;
    dictionary_insert(components, comp, MORPHO_NIL);

    if (MORPHO_ISFUNCTION(comp)) {
        objectfunction *func = MORPHO_GETFUNCTION(comp);

        if (func!=opt->prog->global) {
            /* Any class captured by a non-global function can be used in
               arbitrary ways, so conservatively keep it live. */
            _optimize_classinfo_markconstants(&func->konst, &opt->classinfo);
        } else {
            _optimize_classinfo_scanfunction(opt, func, &opt->classinfo);
        }

        for (unsigned int i=0; i<func->konst.count; i++) {
            _optimize_classinfo_processcomponent(opt, func->konst.data[i], components);
        }
    } else if (MORPHO_ISMETAFUNCTION(comp)) {
        objectmetafunction *mfn = MORPHO_GETMETAFUNCTION(comp);
        for (unsigned int i=0; i<mfn->fns.count; i++) {
            _optimize_classinfo_processcomponent(opt, mfn->fns.data[i], components);
        }
    } else if (MORPHO_ISCLASS(comp)) {
        objectclass *klass = MORPHO_GETCLASS(comp);
        for (unsigned int i=0; i<klass->methods.capacity; i++) {
            if (!MORPHO_ISNIL(klass->methods.contents[i].key)) {
                _optimize_classinfo_processcomponent(opt, klass->methods.contents[i].val, components);
            }
        }
    }
}

static void optimize_classinfo(optimizer *opt) {
    dictionary components;

    classinfolist_startpass(&opt->classinfo);
    dictionary_init(&components);
    _optimize_classinfo_processcomponent(opt, MORPHO_OBJECT(opt->prog->global), &components);
    dictionary_clear(&components);
}

static bool _optimize_isdeadclass(optimizer *opt, objectclass *klass) {
    if (optimize_classcountconstructed(opt, klass)>0) return false;

    for (unsigned int i=0; i<klass->children.count; i++) {
        value child = klass->children.data[i];

        if (MORPHO_ISCLASS(child) &&
            !_optimize_isdeadclass(opt, MORPHO_GETCLASS(child))) return false;
    }

    return true;
}

static bool _optimize_isdeadclassmethod(optimizer *opt, block *blk) {
    return (blk->func->klass &&
            _optimize_isdeadclass(opt, blk->func->klass));
}

static void optimize_prunedeadclassblocks(optimizer *opt) {
    for (blockindx i=0; i<opt->graph.count; i++) {
        block *blk = &opt->graph.data[i];

        if (block_isentry(blk) && _optimize_isdeadclassmethod(opt, blk)) blk->isentry=false;
    }
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

/** Optimizations performed at the end of a code block */
void optimize_dead_store_elimination(optimizer *opt, block *blk) {
    if (opt->verbose) printf("Ending block\n");
    
    for (int i=0; i<opt->rlist.nreg; i++) {
        instructionindx src;
        
        if (!optimize_isempty(opt, i) &&                // Does the register contain something?
            reginfolist_countuses(&opt->rlist, i)==0 && // Is it being used in the block?
            reginfolist_regcontents(&opt->rlist, i)!=REG_PARAMETER && // It's not a parameter
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
    
    // Set object type if the method isn't used by multiple classes 
    if (func->klass &&
        optimize_methodcountowners(opt, func)==1) {
        reginfolist_write(&opt->rlist, func->entry, 0, REG_PARAMETER, 0);
        reginfolist_settypeinfo(&opt->rlist, 0, MORPHO_OBJECT(func->klass), REGTYPE_SUBTYPE);
    }
    
    value type;
    for (registerindx i=0; i<func->nargs; i++) {
        reginfolist_write(&opt->rlist, func->entry, i+1, REG_PARAMETER, 0);
        if (signature_getparamtype(&func->sig, i, &type)) {
            reginfolist_settypeinfo(&opt->rlist, i+1, type, REGTYPE_SUBTYPE);
        }
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

static bool _loopwrites(optimizer *opt, block *header, registerindx r) {
    for (int i=0; i<header->loopblocks.capacity; i++) {
        value key = header->loopblocks.contents[i].key;
        block *blk;

        if (!MORPHO_ISINTEGER(key)) continue;
        if (!cfgraph_indx(&opt->graph, (blockindx) MORPHO_GETINTEGERVALUE(key), &blk)) continue;
        if (block_writes(blk, r)) return true;
    }

    return false;
}

static bool _isintfact(block *blk, reginfo *info) {
    if (info->typeinfo==REGTYPE_EXACT && MORPHO_ISEQUAL(info->type, typeint)) return true;
    if (info->contents!=REG_CONSTANT) return false;

    value konst = block_getconstant(blk, info->indx);
    return MORPHO_ISINTEGER(konst);
}

/* Recognize loop-carried integer updates of the form `r = r + k` or `r = k + r`,
   where `k` is itself known to be an integer fact. */
static bool _isintpreservingloopadd(optimizer *opt, block *blk, registerindx r) {
    reginfo *info = &blk->rout.rinfo[r];
    if (info->iindx==INSTRUCTIONINDX_EMPTY) return false;

    instruction write = optimize_getinstructionat(opt, info->iindx);
    if (DECODE_OP(write)!=OP_ADD || DECODE_A(write)!=r) return false;

    registerindx other;
    if (DECODE_B(write)==r) {
        other=DECODE_C(write);
    } else if (DECODE_C(write)==r) {
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

static bool _isparam(int n, block **src, int i) {
    for (int k=0; k<n; k++) if (src[k]->rout.rinfo[i].contents==REG_PARAMETER) return true;
    return false;
}

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
        if (_isparam(n, src, i)) continue;
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

        if (_isparam(nentry, entrypred, i) || _isparam(nback, backpred, i)) continue;

        baseline=dest->rinfo[i];
        _prepareboundaryfact(&baseline);

        preserve = !_loopwrites(opt, blk, i);

        if (preserve) {
            dest->rinfo[i]=baseline;
            continue;
        }

        preserve = _isintfact(blk, &baseline);
        for (int k=0; preserve && k<nback; k++) {
            preserve=_isintpreservingloopadd(opt, backpred[k], i);
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

static void optimize_joinblockinput(optimizer *opt, block *blk) {
    reginfolist_wipe(&opt->rlist, blk->func->nregs);
    
    optimize_signature(opt); // Restore function parameters
    
    int nentry = blk->src.count;
    if (!block_isentry(blk) &&
        nentry>0) {
        block *srcblk[nentry]; // Unpack and find source blocks from the dictionary
        
        for (int i=0, k=0; i<blk->src.capacity; i++) {
            value key = blk->src.contents[i].key;
            if (MORPHO_ISNIL(key)) continue;
            
            if (!cfgraph_indx(&opt->graph, MORPHO_GETINTEGERVALUE(key), &srcblk[k])) return;
            
            if (opt->verbose) {
                printf("Restoring from block %ti\n", srcblk[k]->start);
                reginfolist_show(&srcblk[k]->rout);
            }
            
            k++;
        }
        
        if (block_isloopheader(blk)) {
            _resolveloopheader(opt, blk, blk->src.count, srcblk, &opt->rlist);
        } else {
            _resolve(blk->src.count, srcblk, &opt->rlist);
        }
    }
    
    if (opt->verbose) {
        printf("Restored registers\n");
        reginfolist_show(&opt->rlist);
    }
}

static void optimize_loadblockinput(optimizer *opt, block *blk) {
    reginfolist_wipe(&opt->rlist, blk->func->nregs);
    reginfolist_copy(&blk->rin, &opt->rlist);

    if (opt->verbose) {
        printf("Loaded block input\n");
        reginfolist_show(&opt->rlist);
    }
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
    varray_instructionindxinit(&worklist);

    if (opt->verbose) printf("===Dataflow===\n");

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
        bool rinchanged, routchanged;

        if (!cfgraph_indx(&opt->graph, indx, &blk)) continue;
        if (!optimize_blockisreachable(opt, blk)) continue;

        reginfolist_init(&oldrin, blk->func->nregs);
        reginfolist_init(&oldrout, blk->func->nregs);
        reginfolist_copy(&blk->rin, &oldrin);
        reginfolist_copy(&blk->rout, &oldrout);

        optimize_transferblock(opt, blk);
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

        if (routchanged) optimize_queuesuccessors(opt, blk, &worklist);
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
    
    optimizer_init(&opt, in);
    
    optimize_methodinfo(&opt);
    optimize_classinfo(&opt);
    
    if (opt.verbose) morpho_disassemble(NULL, in, NULL);
    
    // Build control flow graph
    cfgraph_build(in, &opt.graph, opt.verbose);
    optimize_prunedeadclassblocks(&opt);
    
    // Perform optimization passes
    for (int i=0; i<3 && !optimize_checkerror(&opt); i++) {
        optimize_pass(&opt, i);
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

value typeint, typelist, typefloat, typestring, typebool, typeclosure, typerange, typetuple, typeclass;

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
}

void bytecodeoptimizer_finalize(void) {
}
