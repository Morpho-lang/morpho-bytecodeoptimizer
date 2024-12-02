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

/* -------------------------------------
 * Reduce power to multiplication
 * ------------------------------------- */

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

/* -------------------------------------
 * Constant folding
 * ------------------------------------- */

bool strategy_constant_folding(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    instruction op = DECODE_OP(instr);
    
    if (op<OP_ADD || op>OP_LE) return false; // Quickly eliminate non-arithmetic instructions
    
    indx left, right; // Check both operands are constants
    if (!(optimize_isconstant(opt, DECODE_B(instr), &left) &&
         optimize_isconstant(opt, DECODE_C(instr), &right))) return false;
        
    // A program that evaluates the required op with the selected constants.
    instruction ilist[] = {
        ENCODE_LONG(OP_LCT, 0, (instruction) left),
        ENCODE_LONG(OP_LCT, 1, (instruction) right),
        ENCODE(op, 0, 0, 1),
        ENCODE_BYTE(OP_END)
    };
    
    // Evaluate the program
    value new = MORPHO_INTEGER(255);
    
    // Add the constant to the constant table
    indx nkonst;
    if (!optimize_addconstant(opt, new, &nkonst)) {
        morpho_freeobject(new);
        return false;
    }
    
    // Replace the instruction
    optimize_replaceinstruction(opt, ENCODE_LONG(OP_LCT, DECODE_A(instr), (unsigned int) nkonst));
    
    return true;
}

/* **********************************************************************
 * Strategy definition table
 * ********************************************************************** */

optimizationstrategy strategies[] = {
    { OP_ANY, strategy_constant_folding, 0 },
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
