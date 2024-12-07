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
    
    if (optimize_findconstant(opt, DECODE_C(instr), &kindx)) {
        value konst = optimize_getconstant(opt, kindx);
        if (MORPHO_ISINTEGER(konst) && MORPHO_GETINTEGERVALUE(konst)==2) {
            optimize_replaceinstruction(opt, ENCODE(OP_MUL, DECODE_A(instr), DECODE_B(instr), DECODE_B(instr)));
            return true;
        }
    }
    
    return false;
}

/* -------------------------------------
 * Duplicate load constant
 * ------------------------------------- */

bool strategy_duplicate_load_constant(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    
    registerindx a = DECODE_A(instr);
    indx cindx = DECODE_Bx(instr);
    
    for (registerindx i=0; i<opt->rlist.nreg; i++) {
        indx oindx;
        if (optimize_isconstant(opt, i, &oindx) &&
            cindx==oindx) {
         
            if (i!=a) { // Replace with a move instruction and note the duplication
                optimize_replaceinstruction(opt, ENCODE_DOUBLE(OP_MOV, a, i));
            } else { // Register already contains this constant
                optimize_replaceinstruction(opt, ENCODE_BYTE(OP_NOP));
            }
            
            return true;
        }
    }
    
    return false;
}


bool strategy_duplicate_load(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    
    regcontents contents;
    switch (DECODE_OP(instr)) {
        case OP_LGL: contents = REG_GLOBAL; break;
        case OP_LCT: contents = REG_CONSTANT; break;
        case OP_LUP: contents = REG_UPVALUE; break;
        default: return false;
    }
    
    registerindx a = DECODE_A(instr);
    indx cindx = DECODE_Bx(instr);
    
    for (registerindx i=0; i<opt->rlist.nreg; i++) {
        regcontents icontents;
        indx iindx;
        
        if (optimize_contents(opt, i, &icontents, &iindx) &&
            icontents==contents &&
            cindx==iindx) {
         
            if (i!=a) { // Replace with a move instruction and note the duplication
                optimize_replaceinstruction(opt, ENCODE_DOUBLE(OP_MOV, a, i));
            } else { // Register already contains this constant
                optimize_replaceinstruction(opt, ENCODE_BYTE(OP_NOP));
            }
            
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
    CHECK(optimize_findconstant(opt, DECODE_B(instr), &left) &&
          optimize_findconstant(opt, DECODE_C(instr), &right));
        
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
 * Common subexpression elimination
 * ------------------------------------- */

bool strategy_common_subexpression_elimination(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    instruction op = DECODE_OP(instr);
    CHECK(op>=OP_ADD && op<=OP_LE); // Quickly eliminate non-arithmetic instructions
    
    bool success=false;
    
    static instruction mask = ( MASK_OP | MASK_B | MASK_C );
    registerindx reg[] = { DECODE_B(instr), DECODE_C(instr) };
    
    block *blk = optimize_currentblock(opt);
    
    // Find if another register contains the same calculated value.
    for (registerindx i=0; i<opt->rlist.nreg; i++) {
        instructionindx src;
        
        if (reginfolist_regcontents(&opt->rlist, i)==REG_VALUE && // Must contain a value
            reginfolist_source(&opt->rlist, i, &src) && // Obtain the source
            src>=blk->start && // Check source is in this block
            src<=blk->end) {
            instruction prev = optimize_getinstructionat(opt, src);
            
            if ((prev & mask)==(opt->current & mask) && // Is instruction the same?
                !optimize_isoverwritten(opt, DECODE_A(prev), src)) {
                /* Todo: Also need to check if an instruction between the previous one and the
                   current one overwrites any operands */
                
                optimize_replaceinstruction(opt, ENCODE_DOUBLE(OP_MOV, DECODE_A(instr), DECODE_A(prev)));
                success=true;
            }
        }
    }
    
    return success;
}

/* -------------------------------------
 * Dead store elimination
 * ------------------------------------- */

bool strategy_register_replacement(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    instruction op = DECODE_OP(instr);
    CHECK(op>=OP_ADD && op<=OP_LE); // Quickly eliminate non-arithmetic instructions
    
    bool success=false;
    
    registerindx a=DECODE_A(instr),
                 b=DECODE_B(instr),
                 c=DECODE_C(instr);
    
    registerindx ob=optimize_findoriginalregister(opt, b);
    registerindx oc=optimize_findoriginalregister(opt, c);
    
    if (ob!=b || oc!=c) {
        optimize_replaceinstruction(opt, ENCODE(op, a, ob, oc));
        success=true;
    }
    
    return success;
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
    { OP_ANY,  strategy_constant_folding,                 0 },
    { OP_ANY,  strategy_dead_store_elimination,           0 },
    { OP_ANY,  strategy_register_replacement,             0 },
    //{ OP_ANY,  strategy_common_subexpression_elimination, 0 },
    { OP_LCT,  strategy_duplicate_load,                   0 },
    { OP_LGL,  strategy_duplicate_load,                   0 },
    { OP_LUP,  strategy_duplicate_load,                   0 },
    { OP_CALL, strategy_constant_immutable,               0 },
    { OP_POW,  strategy_power_reduction,                  0 },
    { OP_END,  NULL,                                      0 }
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
