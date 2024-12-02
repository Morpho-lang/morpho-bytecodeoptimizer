/** @file optimize.c
 *  @author T J Atherton
 *
 *  @brief Optimizer for compiled morpho bytecode
*/

#include "optimize.h"
#include "opcodes.h"
#include "cfgraph.h"
#include "reginfo.h"

/* **********************************************************************
 * Optimizer data structure
 * ********************************************************************** */

typedef struct {
    program *prog;
    
    cfgraph graph;

    reginfolist rlist;
    
    instructionindx pc;
    instruction current;
} optimizer;

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
void optimize_write(void *in, registerindx r, regcontents contents, indx indx) {
    optimizer *opt = (optimizer *) in;
    reginfolist_write(&opt->rlist, opt->pc, r, contents, indx);
}

/** Callback function to get the current instruction */
instruction optimize_getinstruction(void *in) {
    optimizer *opt = (optimizer *) in;
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
        
        // Perform processing to track register contents
        opcodeprocessfn processfn = opcode_getprocessfn(DECODE_OP(opt->current));
        if (processfn) processfn(opt);
        
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

void bytecodeoptimizer_initialize(void) {
    morpho_setoptimizer(optimize);
    opcode_initialize();
}

void bytecodeoptimizer_finalize(void) {
}
