/** @file opcodes.c
 *  @author T J Atherton
 *
 *  @brief Database of opcodes and their properties
*/

#include "morphocore.h"
#include "opcodes.h"
#include "reginfo.h"
#include "cfgraph.h"
#include "optimize.h"

/* **********************************************************************
 * Opcodes
 * ********************************************************************** */

typedef struct {
    instruction code;
    char *label;
    opcodeflags flags;
    opcodetrackingfn trackingfn;
    opcodeusagefn usagefn;
} opcodeinfo;

/* **********************************************************************
 * Opcode usage functions
 * ********************************************************************** */

void call_usagefn(instruction instr, block *blk, usagecallbackfn fn, void *ref) {
    registerindx rA = DECODE_A(instr);
    int nargs = DECODE_B(instr);
    
    for (registerindx i=rA+1; i<=rA+nargs; i++) fn(i, ref);
}

void return_usagefn(instruction instr, block *blk, usagecallbackfn fn, void *ref) {
    registerindx rA = DECODE_A(instr);
    if (rA>0) fn(DECODE_B(instr), ref);
}

void invoke_usagefn(instruction instr, block *blk, usagecallbackfn fn, void *ref) {
    registerindx rA = DECODE_A(instr);
    int nargs = DECODE_C(instr);
    
    for (registerindx i=rA+1; i<=rA+nargs; i++) fn(i, ref);
}

void closure_usagefn(instruction instr, block *blk, usagecallbackfn fn, void *ref) {
    registerindx B = DECODE_B(instr); // Get which registers are used from the upvalue prototype

    varray_upvalue *v = &blk->func->prototype.data[B];
    for (unsigned int i=0; i<v->count; i++) fn((registerindx) v->data[i].reg, ref);
}

/* **********************************************************************
 * Opcode tracking functions
 * ********************************************************************** */

void mov_trackingfn(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    optimize_write(opt, DECODE_A(instr), REG_REGISTER, DECODE_B(instr));
    optimize_settype(opt, DECODE_A(instr), optimize_type(opt, DECODE_B(instr)));
}

void lct_trackingfn(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    optimize_write(opt, DECODE_A(instr), REG_CONSTANT, DECODE_Bx(instr));
    
    value konst, type; // Get the type of the constant
    konst = optimize_getconstant(opt, DECODE_Bx(instr));
    if (optimize_typefromvalue(konst, &type)) {
        optimize_settype(opt, DECODE_A(instr), type);
    }
}

void lgl_trackingfn(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    optimize_write(opt, DECODE_A(instr), REG_GLOBAL, DECODE_Bx(instr));
}

void arith_trackingfn(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    registerindx a=DECODE_A(instr);
    registerindx b=DECODE_B(instr);
    registerindx c=DECODE_C(instr);
    
    optimize_writevalue(opt, a);
    
    value ta = MORPHO_NIL,
          tb = optimize_type(opt, b),
          tc = optimize_type(opt, c);
    
    if (tb==typeint && tc==typeint) {
        ta = typeint;
    } else if ((tb==typefloat && tc==typefloat) ||
               (tb==typeint   && tc==typefloat) ||
               (tb==typefloat && tc==typeint  )) {
        ta = typefloat;
    }
    
    if (!MORPHO_ISNIL(ta)) optimize_settype(opt, a, ta);
}

void cmp_trackingfn(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    optimize_settype(opt, DECODE_A(instr), typebool);
}

void lup_trackingfn(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    optimize_write(opt, DECODE_A(instr), REG_UPVALUE, DECODE_B(instr));
}

void closure_trackingfn(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    optimize_settype(opt, DECODE_A(instr), typeclosure);
}

void cat_trackingfn(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    optimize_settype(opt, DECODE_A(instr), typestring);
}

/* **********************************************************************
 * Opcode definition table
 * ********************************************************************** */

int nopcodes;

opcodeinfo opcodetable[] = {
    { OP_NOP, "nop", OPCODE_BLANK, NULL, NULL },
    
    { OP_MOV, "mov", OPCODE_OVERWRITES_A | OPCODE_USES_B, mov_trackingfn, NULL },
    
    { OP_ADD, "add", OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C, arith_trackingfn, NULL },
    { OP_SUB, "sub", OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C, arith_trackingfn, NULL },
    { OP_MUL, "mul", OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C, arith_trackingfn, NULL },
    { OP_DIV, "div", OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C, arith_trackingfn, NULL },
    { OP_POW, "pow", OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C, arith_trackingfn, NULL },
    
    { OP_NOT, "not", OPCODE_OVERWRITES_A | OPCODE_USES_B, cmp_trackingfn, NULL },
    
    { OP_EQ, "eq",   OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C, cmp_trackingfn, NULL },
    { OP_NEQ, "neq", OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C, cmp_trackingfn, NULL },
    { OP_LT, "lt",   OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C, cmp_trackingfn, NULL },
    { OP_LE, "le",   OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C, cmp_trackingfn, NULL },
    
    { OP_PUSHERR, "pusherr", OPCODE_BLANK, NULL, NULL },
    { OP_POPERR,  "poperr",  OPCODE_ENDSBLOCK | OPCODE_BRANCH, NULL, NULL },
    
    { OP_B,    "b",    OPCODE_ENDSBLOCK | OPCODE_BRANCH, NULL, NULL },
    { OP_BIF,  "bif",  OPCODE_ENDSBLOCK | OPCODE_BRANCH | OPCODE_CONDITIONAL | OPCODE_USES_A, NULL, NULL },
    { OP_BIFF, "biff", OPCODE_ENDSBLOCK | OPCODE_BRANCH | OPCODE_CONDITIONAL | OPCODE_USES_A, NULL, NULL },
    
    { OP_CALL,    "call",    OPCODE_USES_A | OPCODE_SIDEEFFECTS, NULL, call_usagefn },
    { OP_INVOKE,  "invoke",  OPCODE_USES_A | OPCODE_USES_B | OPCODE_SIDEEFFECTS, NULL, invoke_usagefn },
    { OP_RETURN,  "return",  OPCODE_ENDSBLOCK | OPCODE_TERMINATING, NULL, return_usagefn },
    
    { OP_CLOSEUP, "closeup", OPCODE_BLANK, NULL, NULL },
    
    { OP_LCT, "lct", OPCODE_OVERWRITES_A, lct_trackingfn, NULL },
    { OP_LGL, "lgl", OPCODE_OVERWRITES_A, lgl_trackingfn, NULL },
    { OP_SGL, "sgl", OPCODE_USES_A, NULL, NULL },
    { OP_LPR, "lpr", OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C, NULL, NULL },
    { OP_SPR, "spr", OPCODE_USES_A | OPCODE_USES_B | OPCODE_USES_C, NULL, NULL },
    { OP_LUP, "lup", OPCODE_OVERWRITES_A, lup_trackingfn, NULL },
    { OP_SUP, "sup", OPCODE_USES_B, NULL, NULL },
    { OP_LIX, "lix", OPCODE_OVERWRITES_B | OPCODE_USES_A | OPCODE_USES_RANGEBC, NULL, NULL },
    { OP_SIX, "six", OPCODE_USES_A | OPCODE_USES_RANGEBC, NULL, NULL },
    
    { OP_CLOSURE, "closure", OPCODE_OVERWRITES_A | OPCODE_USES_A, closure_trackingfn, NULL },
    
    { OP_PRINT, "print", OPCODE_USES_A, NULL, NULL },
    
    { OP_BREAK, "break", OPCODE_BLANK, NULL, NULL },
    
    { OP_CAT, "cat", OPCODE_OVERWRITES_A | OPCODE_USES_RANGEBC, cat_trackingfn, NULL },
    
    { OP_END, "end", OPCODE_ENDSBLOCK | OPCODE_TERMINATING, NULL, NULL }
};

/** Get the flags associated with a given opcode */
opcodeflags opcode_getflags(instruction opcode) {
    if (opcode>nopcodes) return OPCODE_BLANK;
    return opcodetable[opcode].flags;
}

/** Gets the trackingfn associated with a given opcode */
opcodetrackingfn opcode_gettrackingfn(instruction opcode) {
    if (opcode>nopcodes) return NULL;
    return opcodetable[opcode].trackingfn;
}

/** Gets the trackingfn associated with a given opcode */
opcodeusagefn opcode_getusagefn(instruction opcode) {
    if (opcode>nopcodes) return NULL;
    return opcodetable[opcode].usagefn;
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

