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
    opcodetrackingfn replacefn;
} opcodeinfo;

/* **********************************************************************
 * Opcode usage functions
 * ********************************************************************** */

void call_usagefn(instruction instr, block *blk, usagecallbackfn fn, void *ref) {
    registerindx rA = DECODE_A(instr);
    int nargs = DECODE_B(instr), nopt = DECODE_C(instr);
    
    for (registerindx i=0; i<nargs+2*nopt+1; i++) fn(rA+i, ref);
}

void invoke_usagefn(instruction instr, block *blk, usagecallbackfn fn, void *ref) {
    registerindx rA = DECODE_A(instr);
    int nargs = DECODE_B(instr), nopt = DECODE_C(instr);
    
    for (registerindx i=0; i<nargs+2*nopt+2; i++) fn(rA+i, ref);
}

void return_usagefn(instruction instr, block *blk, usagecallbackfn fn, void *ref) {
    registerindx rA = DECODE_A(instr);
    if (rA>0) fn(DECODE_B(instr), ref);
}

void closure_usagefn(instruction instr, block *blk, usagecallbackfn fn, void *ref) {
    registerindx B = DECODE_B(instr); // Get which registers are used from the upvalue prototype

    varray_upvalue *prototype = &blk->func->prototype.data[B];
    for (unsigned int i=0; i<prototype->count; i++) {
        upvalue *up = &prototype->data[i];
        if (up->islocal) fn((registerindx) up->reg, ref);
    }
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
    registerindx rindx = DECODE_A(instr);
    optimize_write(opt, rindx, REG_GLOBAL, DECODE_Bx(instr));
    
    value type = MORPHO_NIL;
    type=globalinfolist_type(optimize_globalinfolist(opt), DECODE_Bx(instr));
    optimize_settype(opt, rindx, type);
}

void sgl_trackingfn(optimizer *opt) {
    globalinfolist *glist = optimize_globalinfolist(opt);

    instruction instr = optimize_getinstruction(opt);
    
    registerindx rindx=DECODE_A(instr);
    int gindx=DECODE_Bx(instr);
    
    reginfolist_invalidate(&opt->rlist, REG_GLOBAL, gindx); // Wipe registers that mirror the global
    
    indx kindx;
    if (optimize_isconstant(opt, rindx, &kindx)) {
        value konst = optimize_getconstant(opt, kindx);
        globalinfolist_setconstant(glist, gindx, konst);
    } else globalinfolist_setvalue(glist, gindx);
    
    value type = optimize_type(opt, rindx);
    globalinfolist_settype(glist, gindx, type);
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
    optimize_writevalue(opt, DECODE_A(instr));
    optimize_settype(opt, DECODE_A(instr), typebool);
}

void call_trackingfn(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    registerindx a = DECODE_A(instr);
    
    value type=MORPHO_NIL;
    value content=MORPHO_NIL;
    indx indx;
    if (optimize_isconstant(opt, a, &indx)) {
        content = optimize_getconstant(opt, indx);
    } else if (optimize_isglobal(opt, a, &indx)) {
        content = globalinfolist_type(optimize_globalinfolist(opt), (int) indx);
    }
    
    if (MORPHO_ISCLASS(content)) {
        type=content;
    } else if (MORPHO_ISFUNCTION(content)) {
        type=signature_getreturntype(&MORPHO_GETFUNCTION(content)->sig);
    } else if (MORPHO_ISBUILTINFUNCTION(content)) {
        type=signature_getreturntype(&MORPHO_GETBUILTINFUNCTION(content)->sig);
    }
    
    optimize_writevalue(opt, a);
    if (!MORPHO_ISNIL(type)) optimize_settype(opt, a, type);
}

void invoke_trackingfn(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    registerindx a = DECODE_A(instr);
    
    optimize_writevalue(opt, a+1);
}

void lup_trackingfn(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    optimize_write(opt, DECODE_A(instr), REG_UPVALUE, DECODE_B(instr));
}

void lpr_trackingfn(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    optimize_writevalue(opt, DECODE_A(instr));
}

void lix_trackingfn(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    optimize_writevalue(opt, DECODE_B(instr));
}

void lixl_trackingfn(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    optimize_writevalue(opt, DECODE_A(instr));
}

void closure_trackingfn(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    optimize_writevalue(opt, DECODE_A(instr));
    optimize_settype(opt, DECODE_A(instr), typeclosure);
}

void cat_trackingfn(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    optimize_writevalue(opt, DECODE_A(instr));
    optimize_settype(opt, DECODE_A(instr), typestring);
}

/* **********************************************************************
 * Opcode definition table
 * ********************************************************************** */

int nopcodes;

opcodeinfo opcodetable[] = {
    { OP_NOP, "nop", OPCODE_BLANK, NULL, NULL, NULL },
    
    { OP_MOV, "mov", OPCODE_OVERWRITES_A | OPCODE_USES_B, mov_trackingfn, NULL, NULL },
    
    { OP_ADD, "add", OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C, arith_trackingfn, NULL, NULL },
    { OP_SUB, "sub", OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C, arith_trackingfn, NULL, NULL },
    { OP_MUL, "mul", OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C, arith_trackingfn, NULL, NULL },
    { OP_DIV, "div", OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C, arith_trackingfn, NULL, NULL },
    { OP_POW, "pow", OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C, arith_trackingfn, NULL, NULL },
    
    { OP_NOT, "not", OPCODE_OVERWRITES_A | OPCODE_USES_B, cmp_trackingfn, NULL, NULL },
    
    { OP_EQ, "eq",   OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C, cmp_trackingfn, NULL, NULL },
    { OP_NEQ, "neq", OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C, cmp_trackingfn, NULL, NULL },
    { OP_LT, "lt",   OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C, cmp_trackingfn, NULL, NULL },
    { OP_LE, "le",   OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C, cmp_trackingfn, NULL, NULL },
    
    { OP_PUSHERR, "pusherr",  OPCODE_ENDSBLOCK | OPCODE_NEWBLOCKAFTER | OPCODE_BRANCH_TABLE, NULL, NULL, NULL },
    { OP_POPERR,  "poperr",   OPCODE_ENDSBLOCK | OPCODE_BRANCH, NULL, NULL, NULL },
    
    { OP_B,    "b",    OPCODE_ENDSBLOCK | OPCODE_BRANCH, NULL, NULL, NULL },
    { OP_BIF,  "bif",  OPCODE_ENDSBLOCK | OPCODE_BRANCH | OPCODE_NEWBLOCKAFTER | OPCODE_USES_A, NULL, NULL, NULL },
    { OP_BIFF, "biff", OPCODE_ENDSBLOCK | OPCODE_BRANCH | OPCODE_NEWBLOCKAFTER | OPCODE_USES_A, NULL, NULL, NULL },
    
    { OP_CALL,    "call",    OPCODE_USES_A | OPCODE_OVERWRITES_A | OPCODE_SIDEEFFECTS, call_trackingfn, call_usagefn, NULL },
    { OP_INVOKE,  "invoke",  OPCODE_USES_A | OPCODE_OVERWRITES_AP1 | OPCODE_SIDEEFFECTS, invoke_trackingfn, invoke_usagefn, NULL },
    { OP_METHOD,  "method",  OPCODE_USES_A | OPCODE_OVERWRITES_AP1 | OPCODE_SIDEEFFECTS, invoke_trackingfn, invoke_usagefn, NULL },
    { OP_RETURN,  "return",  OPCODE_ENDSBLOCK | OPCODE_TERMINATING, NULL, return_usagefn, NULL },
    
    { OP_CLOSEUP, "closeup", OPCODE_BLANK, NULL, NULL, NULL },
    
    { OP_LCT, "lct", OPCODE_OVERWRITES_A, lct_trackingfn, NULL, NULL },
    { OP_LGL, "lgl", OPCODE_OVERWRITES_A, lgl_trackingfn, NULL, NULL },
    { OP_SGL, "sgl", OPCODE_USES_A, sgl_trackingfn, NULL, NULL },
    { OP_LPR, "lpr", OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C | OPCODE_SIDEEFFECTS, lpr_trackingfn, NULL, NULL },
    { OP_SPR, "spr", OPCODE_USES_A | OPCODE_USES_B | OPCODE_USES_C, NULL, NULL, NULL },
    { OP_LUP, "lup", OPCODE_OVERWRITES_A, lup_trackingfn, NULL, NULL },
    { OP_SUP, "sup", OPCODE_USES_B, NULL, NULL, NULL },
    { OP_LIX, "lix", OPCODE_OVERWRITES_B | OPCODE_USES_A | OPCODE_USES_RANGEBC | OPCODE_SIDEEFFECTS, lix_trackingfn, NULL, NULL },
    { OP_LIXL, "lixl", OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C | OPCODE_SIDEEFFECTS, lixl_trackingfn, NULL, NULL },
    { OP_SIX, "six", OPCODE_USES_A | OPCODE_USES_RANGEBC, NULL, NULL, NULL },
    
    { OP_CLOSURE, "closure", OPCODE_OVERWRITES_A | OPCODE_USES_A | OPCODE_SIDEEFFECTS, closure_trackingfn, closure_usagefn, NULL },
    
    { OP_PRINT, "print", OPCODE_USES_A, NULL, NULL, NULL },
    
    { OP_BREAK, "break", OPCODE_BLANK, NULL, NULL, NULL },
    
    { OP_CAT, "cat", OPCODE_OVERWRITES_A | OPCODE_USES_RANGEBC, cat_trackingfn, NULL, NULL },
    
    { OP_END, "end", OPCODE_ENDSBLOCK | OPCODE_TERMINATING, NULL, NULL, NULL }
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

opcodetrackingfn opcode_getreplacefn(instruction opcode) {
    if (opcode>nopcodes) return NULL;
    return opcodetable[opcode].replacefn;
}

/* **********************************************************************
 * Track usage and overwrites
 * ********************************************************************** */

/** Facilitates instruction usage */
void opcode_usageforinstruction(block *blk, instruction instr, usagecallbackfn usagefn, void *ref) {
    opcodeflags flags = opcode_getflags(DECODE_OP(instr));
    
    // Process usage
    if (flags & OPCODE_USES_A) usagefn(DECODE_A(instr), ref);
    if (flags & OPCODE_USES_B) usagefn(DECODE_B(instr), ref);
    if (flags & OPCODE_USES_C) usagefn(DECODE_C(instr), ref);
    
    if (flags & OPCODE_USES_RANGEBC) {
        for (int i=DECODE_B(instr); i<=DECODE_C(instr); i++) usagefn(i, ref);
    }
    
    // A few opcodes have unusual usage and provide a tracking function
    opcodeusagefn ufn=opcode_getusagefn(DECODE_OP(instr));
    if (ufn) ufn(instr, blk, usagefn, ref);
}

bool opcode_overwritesforinstruction(instruction instr, registerindx *out) {
    opcodeflags flags = opcode_getflags(DECODE_OP(instr));
    registerindx r = REGISTER_UNALLOCATED;
    
    if (flags & OPCODE_OVERWRITES_A) r=DECODE_A(instr);
    if (flags & OPCODE_OVERWRITES_AP1) r=DECODE_A(instr)+1;
    if (flags & OPCODE_OVERWRITES_B) r=DECODE_B(instr);
    
    bool overwrites = (r!=REGISTER_UNALLOCATED);
    if (overwrites && out) *out = r;
    
    return overwrites;
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

