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

/** Reduces power to a multiply */
bool strategy_power_reduction(optimizer *opt) {
    indx kindx;
    instruction instr = optimize_getinstruction(opt);
    
    if (optimize_isconstant(opt, DECODE_C(instr), &kindx)) {
        value konst = optimize_getconstant(opt, kindx);
        if (MORPHO_ISINTEGER(konst) && MORPHO_GETINTEGERVALUE(konst)==2) {
            optimize_replaceinstruction(opt, ENCODE(OP_MUL, DECODE_A(instr), DECODE_B(instr), DECODE_B(instr)));
            return true;
        }
    }
    
    return false;
}

/* **********************************************************************
 * Strategy definition table
 * ********************************************************************** */

optimizationstrategy strategies[] = {
    { OP_ANY, strategy_null,             0 },
    { OP_POW, strategy_power_reduction,  0 },
    { OP_END, NULL,                      0 }
};

/* **********************************************************************
 * Apply relevant strategies
 * ********************************************************************** */

void strategy_optimizeinstruction(optimizer *opt, int maxlevel) {
    instruction op = DECODE_OP(optimize_getinstruction(opt));
    
    for (int i=0; strategies[i].match!=OP_END; i++) {
        if ((strategies[i].match==op ||
             strategies[i].match==OP_ANY) &&
             strategies[i].level <= maxlevel) {
            
            if ((strategies[i].fn) (opt)) break; // Terminate if the strategy function succeeds
        }
    }
}
