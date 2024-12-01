/** @file opcodes.c
 *  @author T J Atherton
 *
 *  @brief Database of opcodes and their properties
*/

#include "optimize.h"
#include "opcodes.h"

/* **********************************************************************
 * Opcodes
 * ********************************************************************** */

typedef struct {
    instruction code;
    char *label;
    opcodeflags flags;
} opcodeinfo;

int nopcodes;

opcodeinfo opcodetable[] = {
    { OP_NOP, "nop", OPCODE_BLANK },
    
    { OP_MOV, "mov", OPCODE_OVERWRITES_A | OPCODE_USES_B },
    
    { OP_ADD, "add", OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C },
    { OP_SUB, "sub", OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C },
    { OP_MUL, "mul", OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C },
    { OP_DIV, "div", OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C },
    { OP_POW, "pow", OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C },
    
    { OP_EQ, "eq", OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C },
    { OP_NEQ, "neq", OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C },
    { OP_LT, "lt", OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C },
    { OP_LE, "le", OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C },
    
    { OP_NOT, "not", OPCODE_OVERWRITES_A | OPCODE_USES_B },
    
    { OP_PUSHERR, "pusherr", OPCODE_BLANK },
    { OP_POPERR, "poperr", OPCODE_ENDSBLOCK | OPCODE_BRANCH  },
    { OP_B, "b", OPCODE_ENDSBLOCK | OPCODE_BRANCH },
    { OP_BIF, "bif", OPCODE_ENDSBLOCK | OPCODE_BRANCH | OPCODE_CONDITIONAL | OPCODE_USES_A },
    { OP_BIFF, "biff", OPCODE_ENDSBLOCK | OPCODE_BRANCH | OPCODE_CONDITIONAL | OPCODE_USES_A },
    
    { OP_CALL, "call", OPCODE_UNSUPPORTED },
    { OP_INVOKE, "invoke", OPCODE_UNSUPPORTED },
    { OP_RETURN, "return", OPCODE_UNSUPPORTED },
    
    { OP_CLOSEUP, "closeup", OPCODE_UNSUPPORTED },
    
    { OP_LCT, "lct", OPCODE_OVERWRITES_A },
    { OP_LGL, "lgl", OPCODE_OVERWRITES_A },
    { OP_SGL, "sgl", OPCODE_USES_A },
    { OP_LPR, "lpr", OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C },
    { OP_SPR, "spr", OPCODE_USES_A | OPCODE_USES_B | OPCODE_USES_C },
    { OP_LUP, "lup", OPCODE_OVERWRITES_A },
    { OP_SUP, "sup", OPCODE_USES_B },
    { OP_LIX, "lix", OPCODE_OVERWRITES_B | OPCODE_USES_A | OPCODE_USES_RANGEBC },
    { OP_SIX, "six", OPCODE_USES_A | OPCODE_USES_RANGEBC },
    
    { OP_CLOSURE, "closure", OPCODE_UNSUPPORTED },
    
    { OP_PRINT, "print", OPCODE_USES_A },
    
    { OP_BREAK, "break", OPCODE_BLANK },
    
    { OP_CAT, "cat", OPCODE_UNSUPPORTED },
    
    { OP_END, "end", OPCODE_ENDSBLOCK }
};

/** Get the flags associated with a given opcode */
opcodeflags opcode_getflags(instruction opcode) {
    if (opcode>nopcodes) return OPCODE_BLANK;
    return opcodetable[opcode].flags;
}

/* **********************************************************************
 * Initialization
 * ********************************************************************** */

int _opcodecmp(const void *a, const void *b) {
    opcodeinfo *aa = (opcodeinfo *) a;
    opcodeinfo *bb = (opcodeinfo *) b;
    return ((int) aa->code) - ((int) bb->code);
}

void opcode_initialize(void) {
    // Establish number of opcodes
    for (nopcodes=0; opcodetable[nopcodes].code!=OP_END; nopcodes++);
    
    // Ensure opcode table is sorted
    qsort(opcodetable, nopcodes, sizeof(opcodeinfo), _opcodecmp);
    
    if (opcodetable[nopcodes].code!=nopcodes) UNREACHABLE("Error in opcode definition table.");
}

