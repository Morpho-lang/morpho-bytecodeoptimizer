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

/** Checks if a register holds a constant */
bool optimize_isconstant(optimizer *opt, registerindx i, indx *out) {
    regcontents contents=REG_EMPTY;
    indx ix;
    if (!reginfolist_contents(&opt->rlist, i, &contents, &ix)) return false;
    
    bool success=(contents==REG_CONSTANT);
    if (success) *out = ix;
    
    return success;
}

/** Callback function to get the current instruction */
instruction optimize_getinstruction(optimizer *opt) {
    return opt->current;
}

/** Callback function to get the current instruction */
void optimize_replaceinstruction(optimizer *opt, instruction instr) {
    opt->current=opt->prog->code.data[opt->pc]=instr;
    opt->nchanged++;
    optimize_disassemble(opt);
}

void optimize_usage(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    opcodeflags flags = opcode_getflags(DECODE_OP(instr));
    
    if (flags & OPCODE_USES_A) reginfolist_uses(&opt->rlist, DECODE_A(instr));
    if (flags & OPCODE_USES_B) reginfolist_uses(&opt->rlist, DECODE_B(instr));
    if (flags & OPCODE_USES_C) reginfolist_uses(&opt->rlist, DECODE_C(instr));
    if (flags & OPCODE_USES_RANGEBC) {
        for (int i=DECODE_B(instr); i<=DECODE_C(instr); i++) reginfolist_uses(&opt->rlist, i);
    }
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
    
    optimizer_init(&opt, in);
    
    cfgraph_build(in, &opt.graph);
    
    for (int i=0; i<opt.graph.count; i++) optimize_block(&opt, &opt.graph.data[i]);
    
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

void bytecodeoptimizer_initialize(void) {
    morpho_setoptimizer(optimize);
    opcode_initialize();
    
    //objectstring boollabel = MORPHO_STATICSTRING("Bool");
    //typebool = builtin_findclass(MORPHO_OBJECT(&boollabel));
    typebool = builtin_addclass("Bool", MORPHO_GETCLASSDEFINITION(Bool), MORPHO_NIL);
    value_setveneerclass(MORPHO_TRUE, typebool);

    objectstring stringlabel = MORPHO_STATICSTRING("String");
    typestring = builtin_findclass(MORPHO_OBJECT(&stringlabel));
}

void bytecodeoptimizer_finalize(void) {
}
