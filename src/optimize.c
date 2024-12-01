/** @file optimize.c
 *  @author T J Atherton
 *
 *  @brief Optimizer for compiled morpho bytecode
*/

#include "optimize.h"
#include "cfgraph.h"

/* **********************************************************************
 * Optimizer data structure
 * ********************************************************************** */

typedef struct {
    program *prog;
    
    cfgraph graph;
} optimizer;

/** Initializes an optimizer data structure */
void optimizer_init(optimizer *opt, program *prog) {
    opt->prog=prog;
    cfgraph_init(&opt->graph);
}

/** Clears an optimizer data structure */
void optimize_clear(optimizer *opt) {
    cfgraph_clear(&opt->graph);
}

/* **********************************************************************
 * Optimizer 
 * ********************************************************************** */

/** Public interface to optimizer */
bool optimize(program *in) {
    optimizer opt;
    
    optimizer_init(&opt, in);
    
    cfgraph_build(in, &opt.graph);
    
    optimize_clear(&opt);
    
    return true;
}

/* **********************************************************************
 * Initialization/Finalization
 * ********************************************************************** */

void bytecodeoptimizer_initialize(void) {
    morpho_setoptimizer(optimize);
}
