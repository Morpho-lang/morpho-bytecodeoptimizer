/** @file optimize.c
 *  @author T J Atherton
 *
 *  @brief Optimizer for compiled morpho bytecode
*/

#include "optimize.h"

/** Public interface to optimizer */
bool optimize(program *in) {
    printf("Optimizing\n");
    return true;
}

/* **********************************************************************
 * Initialization/Finalization
 * ********************************************************************** */

void bytecodeoptimizer_initialize(void) {
    morpho_setoptimizer(optimize);
}
