/** @file strategy.c
 *  @author T J Atherton
 *
 *  @brief Local optimization strategies
*/

#include "morphocore.h"
#include "strategy.h"
#include "optimize.h"

/* **********************************************************************
 * Local optimization strategies
 * ********************************************************************** */

/** An empty strategy */
bool strategy_null(optimizer *opt) {
    return false;
}

/* **********************************************************************
 * Strategy definition table
 * ********************************************************************** */

optimizationstrategy strategies[] = {
    { OP_ANY, strategy_null, 0 },
    { OP_END, NULL,           0 }
};

/* **********************************************************************
 * Apply relevant strategies
 * ********************************************************************** */

void strategy_apply(optimizer *opt, int maxlevel) {
    instruction op = DECODE_OP(optimize_getinstruction(opt));
    
    for (int i=0; strategies[i].match!=OP_END; i++) {
        if ((strategies[i].match==op ||
             strategies[i].match==OP_ANY) &&
             strategies[i].level <= maxlevel) {
            
            if ((strategies[i].fn) (opt)) break; // Terminate if the strategy function succeeds
        }
    }
}
