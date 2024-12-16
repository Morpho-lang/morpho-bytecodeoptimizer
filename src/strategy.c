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
 * Duplicate load
 * ------------------------------------- */

bool strategy_duplicate_load(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    instruction op = DECODE_OP(instr);
    
    regcontents contents;
    switch (op) {
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
    if (!optimize_evalsubprogram(opt, ilist, 0, &new)) {
        optimize_error(opt, ERROR_ALLOCATIONFAILED);
        return false;
    }
    
    // Replace result with an appropriate LCT
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
    CHECK(op>=OP_ADD && op<=OP_LE || op==OP_LIXL); // Quickly eliminate non-arithmetic instructions
    
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
        optimize_source(opt, r, &iindx) &&
        block_contains(opt->currentblk, iindx)) {
        
        success=optimize_deleteinstruction(opt, iindx);
    }
    return success;
}

/* -------------------------------------
 * Constant Immutable Constructor
 * ------------------------------------- */

bool _isimmutable(value v) {
    return (MORPHO_ISEQUAL(v, typebool) ||
            MORPHO_ISEQUAL(v, typerange) ||
            MORPHO_ISEQUAL(v, typestring) ||
            MORPHO_ISEQUAL(v, typetuple) ||
            MORPHO_ISEQUAL(v, typeint) ||
            MORPHO_ISEQUAL(v, typefloat));
}

bool strategy_constant_immutable(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    
    registerindx rA=DECODE_A(instr);
    int nargs = DECODE_B(instr);
    int nopt = DECODE_C(instr);
    
    // Ensure call target and arguments are all constants
    indx cindx[nargs+1];
    for (int i=0; i<=nargs+nopt; i++) {
        CHECK(optimize_isconstant(opt, rA + i, cindx + i));
    }
    
    // Retrieve the call target
    value fn = optimize_getconstant(opt, cindx[0]);
    
    // Check the function is a constructor
    CHECK(MORPHO_ISBUILTINFUNCTION(fn) &&
          (MORPHO_GETBUILTINFUNCTION(fn)->flags & MORPHO_FN_CONSTRUCTOR));
    
    // Todo: Need a better check for immutability!
    value type = signature_getreturntype(&MORPHO_GETBUILTINFUNCTION(fn)->sig);
    if (!_isimmutable(type)) return false;
        
    // A program that evaluates the required op with the selected constants.
    varray_instruction prog;
    varray_instructioninit(&prog);
    
    for (int i=0; i<nargs+nopt+1; i++) { // Setup load constants incl. the function
        varray_instructionwrite(&prog, ENCODE_LONG(OP_LCT, i, (instruction) cindx[i]));
    }
    
    varray_instructionwrite(&prog, ENCODE(OP_CALL, 0, (instruction) nargs, (instruction) nopt));
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

/* -------------------------------------
 * Constant global
 * ------------------------------------- */

bool strategy_constant_global(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    value konst;
    bool success=false;
    
    globalinfolist *glist = optimize_globalinfolist(opt);
    if (globalinfolist_isconstant(glist, DECODE_Bx(instr), &konst)) {
        optimize_replacewithloadconstant(opt, DECODE_A(instr), konst);
        success=true;
    }
    
    return success;
}

/* -------------------------------------
 * Unused global
 * ------------------------------------- */

bool strategy_unused_global(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    value konst;
    bool success=false;
    
    globalinfolist *glist = optimize_globalinfolist(opt);
    if (globalinfolist_countread(glist, DECODE_Bx(instr))==0) {
        optimize_replaceinstruction(opt, ENCODE_BYTE(OP_NOP));
        success=true;
    }
    
    return success;
}

/* -------------------------------------
 * Load index list
 * ------------------------------------- */

bool strategy_load_index_list(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    bool success=false;
    
    value type = optimize_type(opt, DECODE_A(instr));
    
    if (MORPHO_ISSAME(type, typelist) &&
        DECODE_B(instr)==DECODE_C(instr)) {
        optimize_replaceinstruction(opt, ENCODE(OP_LIXL, DECODE_B(instr), DECODE_A(instr), DECODE_B(instr)));
        success=true; 
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
    { OP_LIX,  strategy_load_index_list,                  0 },
    { OP_CALL, strategy_constant_immutable,               0 },
    { OP_POW,  strategy_power_reduction,                  0 },
    
    { OP_LGL,  strategy_constant_global,                  1 },
    { OP_SGL,  strategy_unused_global,                    1 },
    { OP_END,  NULL,                                      0 }
};

/* **********************************************************************
 * Apply relevant strategies
 * ********************************************************************** */

bool strategy_optimizeinstruction(optimizer *opt, int maxlevel) {
    instruction op = DECODE_OP(optimize_getinstruction(opt));
    
    for (int i=0; strategies[i].match!=OP_END; i++) {
        if ((strategies[i].match==op ||
             strategies[i].match==OP_ANY) &&
             strategies[i].level <= maxlevel) {
            
            if ((strategies[i].fn) (opt)) return true; // Terminate if the strategy function succeeds
        }
    }
    
    return false;
}
