/** @file opcodes.c
 *  @author T J Atherton
 *
 *  @brief Database of opcodes and their properties
*/

#include "morphocore.h"
#include "opcodes.h"
#include "reginfo.h"
#include "cfgraph.h"

/* **********************************************************************
 * Opcodes
 * ********************************************************************** */

typedef struct {
    instruction code;
    char *label;
    opcodeflags flags;
    opcodeprocessfn processfn;
} opcodeinfo;

/* **********************************************************************
 * Opcode process functions
 * ********************************************************************** */

void optimize_write(void *in, registerindx r, regcontents contents, indx indx);
instruction optimize_getinstruction(void *in);

void lct_processfn(void *opt) {
    instruction instr = optimize_getinstruction(opt);
    optimize_write(opt, DECODE_A(instr), REG_CONSTANT, DECODE_Bx(instr));
}

void lgl_processfn(void *opt) {
    instruction instr = optimize_getinstruction(opt);
    optimize_write(opt, DECODE_A(instr), REG_GLOBAL, DECODE_Bx(instr));
}

/* **********************************************************************
 * Opcode definition table
 * ********************************************************************** */

int nopcodes;

opcodeinfo opcodetable[] = {
    { OP_NOP, "nop", OPCODE_BLANK, NULL },
    
    { OP_MOV, "mov", OPCODE_OVERWRITES_A | OPCODE_USES_B, NULL },
    
    { OP_ADD, "add", OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C, NULL },
    { OP_SUB, "sub", OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C, NULL },
    { OP_MUL, "mul", OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C, NULL },
    { OP_DIV, "div", OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C, NULL },
    { OP_POW, "pow", OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C, NULL },
    
    { OP_NOT, "not", OPCODE_OVERWRITES_A | OPCODE_USES_B, NULL },
    
    { OP_EQ, "eq",   OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C, NULL },
    { OP_NEQ, "neq", OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C, NULL },
    { OP_LT, "lt",   OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C, NULL },
    { OP_LE, "le",   OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C, NULL },
    
    { OP_PUSHERR, "pusherr", OPCODE_BLANK, NULL },
    { OP_POPERR,  "poperr",  OPCODE_ENDSBLOCK | OPCODE_BRANCH, NULL },
    
    { OP_B,    "b",    OPCODE_ENDSBLOCK | OPCODE_BRANCH, NULL },
    { OP_BIF,  "bif",  OPCODE_ENDSBLOCK | OPCODE_BRANCH | OPCODE_CONDITIONAL | OPCODE_USES_A, NULL },
    { OP_BIFF, "biff", OPCODE_ENDSBLOCK | OPCODE_BRANCH | OPCODE_CONDITIONAL | OPCODE_USES_A, NULL },
    
    { OP_CALL,    "call",    OPCODE_USES_A, NULL },
    { OP_INVOKE,  "invoke",  OPCODE_USES_A | OPCODE_USES_B, NULL },
    { OP_RETURN,  "return",  OPCODE_USES_B | OPCODE_ENDSBLOCK | OPCODE_TERMINATING, NULL }, // Cond on A
    
    { OP_CLOSEUP, "closeup", OPCODE_UNSUPPORTED, NULL },
    
    { OP_LCT, "lct", OPCODE_OVERWRITES_A, lct_processfn },
    { OP_LGL, "lgl", OPCODE_OVERWRITES_A, lgl_processfn },
    { OP_SGL, "sgl", OPCODE_USES_A, NULL },
    { OP_LPR, "lpr", OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C, NULL },
    { OP_SPR, "spr", OPCODE_USES_A | OPCODE_USES_B | OPCODE_USES_C, NULL },
    { OP_LUP, "lup", OPCODE_OVERWRITES_A, NULL },
    { OP_SUP, "sup", OPCODE_USES_B, NULL },
    { OP_LIX, "lix", OPCODE_OVERWRITES_B | OPCODE_USES_A | OPCODE_USES_RANGEBC, NULL },
    { OP_SIX, "six", OPCODE_USES_A | OPCODE_USES_RANGEBC, NULL },
    
    { OP_CLOSURE, "closure", OPCODE_UNSUPPORTED, NULL },
    
    { OP_PRINT, "print", OPCODE_USES_A, NULL },
    
    { OP_BREAK, "break", OPCODE_BLANK, NULL },
    
    { OP_CAT, "cat", OPCODE_OVERWRITES_A | OPCODE_USES_RANGEBC, NULL },
    
    { OP_END, "end", OPCODE_ENDSBLOCK | OPCODE_TERMINATING, NULL }
};

/** Get the flags associated with a given opcode */
opcodeflags opcode_getflags(instruction opcode) {
    if (opcode>nopcodes) return OPCODE_BLANK;
    return opcodetable[opcode].flags;
}

/** Gets the processfn associated with a given opcode */
opcodeprocessfn opcode_getprocessfn(instruction opcode) {
    if (opcode>nopcodes) return NULL;
    return opcodetable[opcode].processfn;
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

