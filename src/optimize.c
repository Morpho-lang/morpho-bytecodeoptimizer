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

/* **********************************************************************
 * Optimizer data structure
 * ********************************************************************** */

/** Initializes an optimizer data structure */
void optimizer_init(optimizer *opt, program *prog) {
    opt->prog=prog;
    cfgraph_init(&opt->graph);
    reginfolist_init(&opt->rlist, 0);
}

/** Clears an optimizer data structure */
void optimize_clear(optimizer *opt) {
    cfgraph_clear(&opt->graph);
}

/* **********************************************************************
 * Optimize a code block
 * ********************************************************************** */

typedef bool (*optimizationstrategyfn) (optimizer *opt);

typedef struct {
    instruction match;
    optimizationstrategyfn fn;
} optimizationstrategy;

bool optimize_dummy(optimizer *opt) {
    return false;
}

#define OP_ANY (OP_END + 1)

optimizationstrategy strategies[] = {
    { OP_ANY, optimize_dummy },
    { OP_END, NULL }
};

/** Fetches the instruction at index i */
void optimize_fetch(optimizer *opt, instructionindx i) {
    opt->pc=i;
    opt->current=opt->prog->code.data[i];
}

/** Callback function to set the contents of a register */
void optimize_write(optimizer *opt, registerindx r, regcontents contents, indx indx) {
    reginfolist_write(&opt->rlist, opt->pc, r, contents, indx);
}

/** Callback function to set the type of a register */
void optimize_settype(optimizer *opt, registerindx r, value type) {
    reginfolist_settype(&opt->rlist, r, type);
}

/** Callback function to get the current instruction */
instruction optimize_getinstruction(optimizer *opt) {
    return opt->current;
}

/** Optimize a given block */
bool optimize_block(optimizer *opt, block *blk) {
    printf("Optimizing block [%ti - %ti]:\n", blk->start, blk->end);
    reginfolist_init(&opt->rlist, blk->func->nregs);
    
    for (instructionindx i=blk->start; i<=blk->end; i++) {
        optimize_fetch(opt, i);
        debugger_disassembleinstruction(NULL, opt->current, i, NULL, NULL);
        printf("\n");
        
        // Perform trackinging to track register contents
        opcodetrackingfn trackingfn = opcode_gettrackingfn(DECODE_OP(opt->current));
        if (trackingfn) trackingfn(opt);
        
        reginfolist_show(&opt->rlist);
    }
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
