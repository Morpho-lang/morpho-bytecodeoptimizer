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

#define CHECK(f) if (!(f)) return false;

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
    
    CHECK(op>=OP_ADD && op<=OP_LE); // Quickly eliminate non-arithmetic instructions
    
    indx left, right; // Check both operands are constants
    CHECK(optimize_isconstant(opt, DECODE_B(instr), &left) &&
         optimize_isconstant(opt, DECODE_C(instr), &right));
        
    // A program that evaluates the required op with the selected constants.
    instruction ilist[] = {
        ENCODE_LONG(OP_LCT, 0, (instruction) left),
        ENCODE_LONG(OP_LCT, 1, (instruction) right),
        ENCODE(op, 0, 0, 1),
        ENCODE_BYTE(OP_END)
    };
    
    // Evaluate the program
    value new = MORPHO_NIL;
    CHECK(optimize_evalsubprogram(opt, ilist, 0, &new));
    
    // Replace CALL with an appropriate LCT
    if (!optimize_replacewithloadconstant(opt, DECODE_A(instr), new)) {
        morpho_freeobject(new);
        return false;
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
    CHECK(flags & (OPCODE_OVERWRITES_A | OPCODE_OVERWRITES_B));
    
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

/* -------------------------------------
 * Constant Immutable Constructor
 * ------------------------------------- */

bool strategy_constant_immutable(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    
    registerindx rA=DECODE_A(instr);
    int nargs = DECODE_B(instr);
    
    // Ensure call target and arguments are all constants
    indx cindx[nargs+1];
    for (registerindx r=rA; r<rA + nargs + 1; r++) {
        CHECK(optimize_isconstant(opt, r, cindx + r - rA));
    }
    
    // Retrieve the call target
    value fn = optimize_getconstant(opt, cindx[0]);
    
    // Check the function is a constructor
    CHECK(MORPHO_ISBUILTINFUNCTION(fn) &&
          (MORPHO_GETBUILTINFUNCTION(fn)->flags & MORPHO_FN_CONSTRUCTOR));
    
    // Todo: Should check for immutability! 
        
    // A program that evaluates the required op with the selected constants.
    varray_instruction prog;
    varray_instructioninit(&prog);
    
    for (int i=0; i<nargs+1; i++) { // Setup load constants incl. the function
        varray_instructionwrite(&prog, ENCODE_LONG(OP_LCT, i, (instruction) cindx[i]));
    }
    
    varray_instructionwrite(&prog, ENCODE_DOUBLE(OP_CALL, 0, (instruction) nargs));
    varray_instructionwrite(&prog, ENCODE_BYTE(OP_END));
    
    // Evaluate the program
    value new = MORPHO_NIL;
    bool success=optimize_evalsubprogram(opt, prog.data, 0, &new);
    varray_instructionclear(&prog);
    
    // Replace CALL with an appropriate LCT
    if (success) {
        if (!optimize_replacewithloadconstant(opt, DECODE_A(instr), new)) {
            morpho_freeobject(new);
        }
    }
    
    return success;
}

/* **********************************************************************
 * Strategy definition table
 * ********************************************************************** */

optimizationstrategy strategies[] = {
    { OP_ANY,  strategy_constant_folding,       0 },
    { OP_ANY,  strategy_dead_store_elimination, 0 },
    { OP_CALL, strategy_constant_immutable,     0 },
    { OP_POW,  strategy_power_reduction,        0 },
    { OP_END,  NULL,                            0 }
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
