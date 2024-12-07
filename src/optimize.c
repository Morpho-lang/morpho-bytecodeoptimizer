/** @file optimize.c
 *  @author T J Atherton
 *
 *  @brief Optimizer for compiled morpho bytecode
*/

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
    cfgraph_init(&opt->graph);
    reginfolist_init(&opt->rlist, 0);
    globalinfolist_init(&opt->glist, prog->globals.count);
    
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
    cfgraph_clear(&opt->graph);
    globalinfolist_clear(&opt->glist);
    
    if (opt->v) morpho_freevm(opt->v);
    if (opt->temp) morpho_freeprogram(opt->temp);
}

/* **********************************************************************
 * Optimize a code block
 * ********************************************************************** */

/** Fetches the instruction at index i and sets this as the current instruction */
instruction optimize_fetch(optimizer *opt, instructionindx i) {
    opt->pc=i;
    return opt->current=opt->prog->code.data[i];
}

/** Callback function to set the contents of a register */
void optimize_write(optimizer *opt, registerindx r, regcontents contents, indx ix) {
    reginfolist_write(&opt->rlist, opt->pc, r, contents, ix);
}

/** Callback function to set the contents of a register */
void optimize_writevalue(optimizer *opt, registerindx r) {
    optimize_write(opt, r, REG_VALUE, INSTRUCTIONINDX_EMPTY);
}

/** Callback function to set the type of a register */
void optimize_settype(optimizer *opt, registerindx r, value type) {
    reginfolist_settype(&opt->rlist, r, type);
}

/** Callback function to set the type of a register */
value optimize_type(optimizer *opt, registerindx r) {
    return reginfolist_type(&opt->rlist, r);
}

/** Wrapper to get the type information from a value */
bool optimize_typefromvalue(value val, value *type) {
    return metafunction_typefromvalue(val, type);
}

/** Callback function to get a constant from the current constant table */
value optimize_getconstant(optimizer *opt, indx i) {
    if (i>opt->currentblk->func->konst.count) return MORPHO_NIL;
    return opt->currentblk->func->konst.data[i];
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

/** Checks if a register is empty */
bool optimize_isempty(optimizer *opt, registerindx i) {
    regcontents contents=REG_EMPTY;
    reginfolist_contents(&opt->rlist, i, &contents, NULL);
    return (contents==REG_EMPTY);
}

/** Checks if a register holds a constant */
bool optimize_isconstant(optimizer *opt, registerindx i, indx *out) {
    regcontents contents=REG_EMPTY;
    indx ix;
    if (!reginfolist_contents(&opt->rlist, i, &contents, &ix)) return false;
    
    bool success=(contents==REG_CONSTANT);
    if (success && out) *out = ix;
    
    return success;
}

/** Checks if a register holds another register */
bool optimize_isregister(optimizer *opt, registerindx i, registerindx *out) {
    regcontents contents=REG_EMPTY;
    indx ix;
    if (!reginfolist_contents(&opt->rlist, i, &contents, &ix)) return false;
    
    bool success=(contents==REG_REGISTER);
    if (success && out) *out = (registerindx) ix;
    
    return success;
}

/** Returns the content type of a register */
bool optimize_contents(optimizer *opt, registerindx i, regcontents *contents, indx *indx) {
    return reginfolist_contents(&opt->rlist, i, contents, indx);
}

/** Checks if a register is overwritten between start and the current instruction */
bool optimize_isoverwritten(optimizer *opt, registerindx rindx, instructionindx start) {
    for (instructionindx i=start; i<opt->pc; i++) {
        instruction instr = optimize_getinstructionat(opt, i);
        opcodeflags flags=opcode_getflags(instr);
        if ( ((flags & OPCODE_OVERWRITES_A) && (DECODE_A(instr)==rindx)) ||
            ((flags & OPCODE_OVERWRITES_B) && (DECODE_B(instr)==rindx)) ) return true;
    }
    
    return false;
}

/** Trace back through duplicate registers */
registerindx optimize_findoriginalregister(optimizer *opt, registerindx rindx) {
    registerindx out=rindx;
    
    while (optimize_isregister(opt, out, &out)) {
        if (out==rindx) return out; // Break cycles
    }
    
    return out;
}

/** Finds whether a register refers to a constant, searching back through other registers */
bool optimize_findconstant(optimizer *opt, registerindx i, indx *out) {
    registerindx orig = optimize_findoriginalregister(opt, i);
    return optimize_isconstant(opt, orig, out);
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
    opt->current=opt->prog->code.data[opt->pc]=instr;
    opt->nchanged++;
    if (opt->verbose) optimize_disassemble(opt);
}

/** Callback function to get the current instruction */
void optimize_replaceinstructionat(optimizer *opt, instructionindx i, instruction instr) {
    opt->prog->code.data[i]=instr;
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
    opcodeflags flags = opcode_getflags(DECODE_OP(instr));
    
    // Check for side effects
    if (flags & OPCODE_SIDEEFFECTS) return false; // Todo: Check whether an instruction with potential side effects could still be safe.
    
    optimize_replaceinstructionat(opt, indx, ENCODE_BYTE(OP_NOP));
    return true;
}

/** Gets the global info list */
globalinfolist *optimize_globalinfolist(optimizer *opt) {
    return &opt->glist;
}

void _optusagefn(registerindx r, void *ref) {
    reginfolist *rinfo = (reginfolist *) ref;
    
    reginfolist_uses(rinfo, r);
}

/** Updates reginfo usage information based on the opcode */
void optimize_usage(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    opcodeflags flags = opcode_getflags(DECODE_OP(instr));
    
    if (flags & OPCODE_USES_A) reginfolist_uses(&opt->rlist, DECODE_A(instr));
    if (flags & OPCODE_USES_B) reginfolist_uses(&opt->rlist, DECODE_B(instr));
    if (flags & OPCODE_USES_C) reginfolist_uses(&opt->rlist, DECODE_C(instr));
    if (flags & OPCODE_USES_RANGEBC) {
        for (int i=DECODE_B(instr); i<=DECODE_C(instr); i++) reginfolist_uses(&opt->rlist, i);
    }
    
    opcodeusagefn usagefn = opcode_getusagefn(DECODE_OP(instr));
    if (usagefn) usagefn(instr, opt->currentblk, _optusagefn, &opt->rlist);
}

/** Disassembles the current instruction */
void optimize_disassemble(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    debugger_disassembleinstruction(NULL, instr, opt->pc, NULL, NULL);
    printf("\n");
}

/** Optimize a given block */
bool optimize_block(optimizer *opt, block *blk) {
    opt->currentblk=blk;
    
    do {
        opt->nchanged=0;
        
        if (opt->verbose) printf("Optimizing block [%ti - %ti]:\n", blk->start, blk->end);
        reginfolist_init(&opt->rlist, blk->func->nregs);
        
        for (instructionindx i=blk->start; i<=blk->end; i++) {
            instruction instr = optimize_fetch(opt, i);
            if (opt->verbose) optimize_disassemble(opt);
            
            // Apply relevant optimization strategies
            strategy_optimizeinstruction(opt, 0);
            
            // Perform tracking to track register contents
            opcodetrackingfn trackingfn = opcode_gettrackingfn(DECODE_OP(opt->current));
            if (trackingfn) trackingfn(opt);
            
            // Update usage
            optimize_usage(opt);
            
            if (opt->verbose) reginfolist_show(&opt->rlist);
        }
    } while (opt->nchanged>0);
    
    return true;
}

/* **********************************************************************
 * Optimizer 
 * ********************************************************************** */

/** Public interface to optimizer */
bool optimize(program *in) {
    optimizer opt;
    
    optimizer_init(&opt, in);
    
    if (opt.verbose) morpho_disassemble(NULL, in, NULL);
    
    cfgraph_build(in, &opt.graph, opt.verbose);
    
    for (int i=0; i<opt.graph.count; i++) optimize_block(&opt, &opt.graph.data[i]);
    
    if (opt.verbose) globalinfolist_show(&opt.glist);
    
    layout(&opt);
    
    if (opt.verbose) morpho_disassemble(NULL, in, NULL);
    
    optimize_clear(&opt);
    return true;
}

/* **********************************************************************
 * Initialization/Finalization
 * ********************************************************************** */

value Bool_prnt(vm *v, int nargs, value *args) {
    object_print(v, MORPHO_GETARG(args, 0));
    return MORPHO_SELF(args); 
}

MORPHO_BEGINCLASS(Bool)
MORPHO_METHOD(MORPHO_PRINT_METHOD, Bool_prnt, MORPHO_FN_FLAGSEMPTY)
MORPHO_ENDCLASS

value typeint, typefloat, typestring, typebool, typeclosure;

void bytecodeoptimizer_initialize(void) {
    morpho_setoptimizer(optimize);
    opcode_initialize();
    
    //objectstring boollabel = MORPHO_STATICSTRING("Bool");
    //typebool = builtin_findclass(MORPHO_OBJECT(&boollabel));
    typebool = builtin_addclass("Bool", MORPHO_GETCLASSDEFINITION(Bool), MORPHO_NIL);
    value_setveneerclass(MORPHO_TRUE, typebool);

    objectstring intlabel = MORPHO_STATICSTRING("Int");
    typeint = builtin_findclass(MORPHO_OBJECT(&intlabel));
    
    objectstring floatlabel = MORPHO_STATICSTRING("Float");
    typefloat = builtin_findclass(MORPHO_OBJECT(&floatlabel));
    
    objectstring stringlabel = MORPHO_STATICSTRING("String");
    typestring = builtin_findclass(MORPHO_OBJECT(&stringlabel));
    
    objectstring closurelabel = MORPHO_STATICSTRING("Closure");
    typeclosure = builtin_findclass(MORPHO_OBJECT(&closurelabel));
}

void bytecodeoptimizer_finalize(void) {
}
