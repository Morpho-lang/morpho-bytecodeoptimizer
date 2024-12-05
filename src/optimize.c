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
    
    opt->v=morpho_newvm();
    opt->temp=morpho_newprogram();
}

/** Clears an optimizer data structure */
void optimize_clear(optimizer *opt) {
    cfgraph_clear(&opt->graph);
    
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
void optimize_write(optimizer *opt, registerindx r, regcontents contents, indx indx) {
    reginfolist_write(&opt->rlist, opt->pc, r, contents, indx);
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

/** Callback function to get the current instruction */
void optimize_replaceinstruction(optimizer *opt, instruction instr) {
    opt->current=opt->prog->code.data[opt->pc]=instr;
    opt->nchanged++;
    optimize_disassemble(opt);
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
    if (usagefn) usagefn(instr, _optusagefn, &opt->rlist);
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
        
        printf("Optimizing block [%ti - %ti]:\n", blk->start, blk->end);
        reginfolist_init(&opt->rlist, blk->func->nregs);
        
        for (instructionindx i=blk->start; i<=blk->end; i++) {
            instruction instr = optimize_fetch(opt, i);
            optimize_disassemble(opt);
            
            // Apply relevant optimization strategies
            strategy_optimizeinstruction(opt, 0);
            
            // Perform tracking to track register contents
            opcodetrackingfn trackingfn = opcode_gettrackingfn(DECODE_OP(instr));
            if (trackingfn) trackingfn(opt);
            
            // Update usage
            optimize_usage(opt);
            
            reginfolist_show(&opt->rlist);
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
    
    morpho_disassemble(NULL, in, NULL);
    
    optimizer_init(&opt, in);
    
    cfgraph_build(in, &opt.graph);
    
    for (int i=0; i<opt.graph.count; i++) optimize_block(&opt, &opt.graph.data[i]);
    
    layout_build(&opt);
    
    for (instructionindx i=0; i<opt.prog->code.count; i++) {
        optimize_fetch(&opt, i);
        optimize_disassemble(&opt);
    }
    
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

value typestring;
value typebool;
value typeclosure;

void bytecodeoptimizer_initialize(void) {
    morpho_setoptimizer(optimize);
    opcode_initialize();
    
    //objectstring boollabel = MORPHO_STATICSTRING("Bool");
    //typebool = builtin_findclass(MORPHO_OBJECT(&boollabel));
    typebool = builtin_addclass("Bool", MORPHO_GETCLASSDEFINITION(Bool), MORPHO_NIL);
    value_setveneerclass(MORPHO_TRUE, typebool);

    objectstring stringlabel = MORPHO_STATICSTRING("String");
    typestring = builtin_findclass(MORPHO_OBJECT(&stringlabel));
    
    objectstring closurelabel = MORPHO_STATICSTRING("Closure");
    typeclosure = builtin_findclass(MORPHO_OBJECT(&closurelabel));
}

void bytecodeoptimizer_finalize(void) {
}
