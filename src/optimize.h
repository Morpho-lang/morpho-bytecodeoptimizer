/** @file optimize.h
 *  @author T J Atherton
 *
 *  @brief Optimizer for compiled morpho bytecode
*/

#ifndef optimize_h
#define optimize_h

#include "morphocore.h"
#include "reginfo.h"
#include "cfgraph.h"

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

/** Function that will be called by the optimizer to set the contents of the register info file */
typedef void (*opcodeprocessfn) (optimizer *opt);

/* **********************************************************************
 * Interface for optimization strategies
 * ********************************************************************** */

void optimize_write(optimizer *opt, registerindx r, regcontents contents, indx indx);
instruction optimize_getinstruction(optimizer *opt);

#endif
