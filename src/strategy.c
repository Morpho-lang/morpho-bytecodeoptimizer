/** @file strategy.c
 *  @author T J Atherton
 *
 *  @brief Local optimization strategies
*/

#include "morphocore.h"
#include "strategy.h"
#include "optimize.h"
#include "eval.h"
#include "opcodes.h"

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
    value new = MORPHO_NIL;
    if (!optimize_evalsubprogram(opt, ilist, 0, &new)) return false;
    
    // Add the constant to the constant table
    indx nkonst;
    if (!optimize_addconstant(opt, new, &nkonst)) {
        morpho_freeobject(new);
        return false;
    }
    
    // Replace the instruction
    optimize_replaceinstruction(opt, ENCODE_LONG(OP_LCT, DECODE_A(instr), (unsigned int) nkonst));
    
    // Set the contents of the register
    optimize_write(opt, DECODE_A(instr), REG_CONSTANT, nkonst);
    
    value type;
    if (optimize_typefromvalue(new, &type)) {
        optimize_settype(opt, DECODE_A(instr), type);
    }
    
    return true;
}

/* -------------------------------------
 * Dead store elimination
 * ------------------------------------- */

bool strategy_dead_store_elimination(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    opcodeflags flags = opcode_getflags(DECODE_OP(instr));
    
    // Return quickly if this instruction doesn't overrwrite
    if (!(flags & (OPCODE_OVERWRITES_A | OPCODE_OVERWRITES_B))) return false;
    
    registerindx r = (flags & OPCODE_OVERWRITES_A ? DECODE_A(instr) : DECODE_Bx(instr));
    bool success=false;
    
    instructionindx iindx;
    if (!optimize_isempty(opt, r) &&
        optimize_countuses(opt, r)==0 &&
        optimize_source(opt, r, &iindx)) {
        success=optimize_deleteinstruction(opt, iindx);
    }
    return success;
}

/* **********************************************************************
 * Strategy definition table
 * ********************************************************************** */

optimizationstrategy strategies[] = {
    { OP_ANY, strategy_constant_folding,       0 },
    { OP_ANY, strategy_dead_store_elimination, 0 },
    { OP_POW, strategy_power_reduction,        0 },
    { OP_END, NULL,                            0 }
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
