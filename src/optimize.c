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

/** Optimize a given block */
bool optimize_block(optimizer *opt, block *blk) {
    reginfolist_init(&opt->rlist, blk->func->nregs);
    
    for (instructionindx i=blk->start; i<=blk->end; i++) {
        
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
    
    optimize_block(&opt, &opt.graph.data[0]);
    
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
