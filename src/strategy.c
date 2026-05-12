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

#define STRATEGY_INLINE_MAXINSTRUCTIONS 12

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
    block *blk = optimize_currentblock(opt);
    
    for (registerindx i=0; i<opt->rlist.nreg; i++) {
        regcontents icontents;
        indx iindx;
        instructionindx srcindx;
        
        if (optimize_contents(opt, i, &icontents, &iindx) &&
            optimize_source(opt, i, &srcindx) &&
            block_contains(blk, srcindx) &&
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
 * Algebraic identities
 * ------------------------------------- */

static bool _issafearithmetictype(value type) {
    return (MORPHO_ISEQUAL(type, typeint) || MORPHO_ISEQUAL(type, typefloat));
}

static bool _iszero(value v) {
    return ((MORPHO_ISINTEGER(v) && MORPHO_GETINTEGERVALUE(v)==0) || (MORPHO_ISFLOAT(v) && MORPHO_GETFLOATVALUE(v)==0.0));
}

static bool _isone(value v) {
    return ((MORPHO_ISINTEGER(v) && MORPHO_GETINTEGERVALUE(v)==1) || (MORPHO_ISFLOAT(v) && MORPHO_GETFLOATVALUE(v)==1.0));
}

static bool _matchconstantregister(optimizer *opt, registerindx r, bool (*pred) (value)) {
    indx kindx; if (!optimize_findconstant(opt, r, &kindx)) return false; return pred(optimize_getconstant(opt, kindx));
}

static bool _replacewithmoveifnumeric(optimizer *opt, registerindx dst, registerindx src) {
    if (!_issafearithmetictype(optimize_type(opt, src))) return false; optimize_replaceinstruction(opt, ENCODE_DOUBLE(OP_MOV, dst, src)); return true;
}

bool strategy_add_identity(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    registerindx a=DECODE_A(instr), b=DECODE_B(instr), c=DECODE_C(instr);
    if (_matchconstantregister(opt, c, _iszero)) return _replacewithmoveifnumeric(opt, a, b);
    if (_matchconstantregister(opt, b, _iszero)) return _replacewithmoveifnumeric(opt, a, c);

    return false;
}

bool strategy_sub_identity(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);

    if (!_matchconstantregister(opt, DECODE_C(instr), _iszero)) return false;
    return _replacewithmoveifnumeric(opt, DECODE_A(instr), DECODE_B(instr));
}

bool strategy_mul_identity(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    registerindx a=DECODE_A(instr), b=DECODE_B(instr), c=DECODE_C(instr);
    if (_matchconstantregister(opt, c, _isone)) return _replacewithmoveifnumeric(opt, a, b);
    if (_matchconstantregister(opt, b, _isone)) return _replacewithmoveifnumeric(opt, a, c);

    return false;
}

bool strategy_div_identity(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);

    if (!_matchconstantregister(opt, DECODE_C(instr), _isone)) return false;
    return _replacewithmoveifnumeric(opt, DECODE_A(instr), DECODE_B(instr));
}

bool strategy_pow_identity(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);

    if (!_matchconstantregister(opt, DECODE_C(instr), _isone)) return false;
    return _replacewithmoveifnumeric(opt, DECODE_A(instr), DECODE_B(instr));
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
        
        if ((reginfolist_regcontents(&opt->rlist, i)==REG_TYPEDVALUE ||
             reginfolist_regcontents(&opt->rlist, i)==REG_VALUE) && // Must contain a value
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
 * Register replacement
 * ------------------------------------- */

typedef struct {
    registerindx target;
    bool used;
} _strategyusedregister;

static void _strategy_findusedregister(registerindx r, void *ref) {
    _strategyusedregister *used = (_strategyusedregister *) ref;
    if (r==used->target) used->used=true;
}

static bool _strategy_registerusedsince(optimizer *opt, instructionindx start, registerindx r) {
    _strategyusedregister used = { .target = r, .used = false };

    for (instructionindx i=start; i<=optimize_getinstructionindx(opt); i++) {
        instruction instr = optimize_getinstructionat(opt, i);
        opcode_usageforinstruction(optimize_currentblock(opt), instr, _strategy_findusedregister, &used);
        if (used.used) return true;
    }

    return false;
}

static bool _strategy_cleanupaliassource(optimizer *opt, registerindx oldreg, registerindx newreg) {
    instructionindx src;
    instruction source;

    if (oldreg==newreg ||
        !optimize_source(opt, oldreg, &src) ||
        !block_contains(optimize_currentblock(opt), src)) return false;

    source = optimize_getinstructionat(opt, src);
    if (DECODE_OP(source)!=OP_MOV || DECODE_A(source)!=oldreg) return false;

    if (_strategy_registerusedsince(opt, src+1, oldreg) ||
        optimize_isused(opt, oldreg)) return false;

    if (!optimize_deleteinstruction(opt, src)) return false;

    /* The deleted instruction may still be reflected in the current register facts.
       Clear the alias register now so later rewrites in this pass don't reuse stale data. */
    optimize_write(opt, oldreg, REG_NOFACT, 0);

    return true;
}

bool strategy_register_replacement(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    instruction op = DECODE_OP(instr);
    opcodeflags flags = opcode_getflags(op);
    CHECK(flags & OPCODE_PROPAGATE);

    /* Property and indexing operations interact with mutable object state.
       Rewriting their operand registers through alias chains is not
       semantics-preserving in general, and it matches the reduced failing
       Delaunay/Circumsphere repro. */
    if (op==OP_LPR || op==OP_SPR || op==OP_LIX || op==OP_LIXL || op==OP_SIX) return false;
    
    registerindx a=DECODE_A(instr), b=DECODE_B(instr), c=DECODE_C(instr);
    registerindx na=a, nb=b, nc=c;
    
    if ((flags & OPCODE_USES_A) &&
        !(flags & OPCODE_OVERWRITES_A) &&
        !(flags & OPCODE_OVERWRITES_AP1)) na=optimize_findoriginalregister(opt, a);
    if (flags & OPCODE_USES_B) nb=optimize_findoriginalregister(opt, b);
    if (flags & OPCODE_USES_C) nc=optimize_findoriginalregister(opt, c);
    
    if (op==OP_MOV && a==nb) {
        optimize_replaceinstruction(opt, ENCODE_BYTE(OP_NOP));
        return true;
    }
    
    if (na==a && nb==b && nc==c) return false;
    optimize_replaceinstruction(opt, ENCODE(op, na, nb, nc));

    if ((flags & OPCODE_USES_A) &&
        !(flags & OPCODE_OVERWRITES_A) &&
        !(flags & OPCODE_OVERWRITES_AP1)) _strategy_cleanupaliassource(opt, a, na);
    if (flags & OPCODE_USES_B) _strategy_cleanupaliassource(opt, b, nb);
    if (flags & OPCODE_USES_C) _strategy_cleanupaliassource(opt, c, nc);

    return true;
}

/* -------------------------------------
 * Self-copy elimination
 * ------------------------------------- */

bool strategy_self_copy_elimination(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    
    if (DECODE_A(instr)!=DECODE_B(instr)) return false;
    optimize_replaceinstruction(opt, ENCODE_BYTE(OP_NOP));
    
    return true;
}

/* -------------------------------------
 * Dead store elimination
 * ------------------------------------- */

bool strategy_dead_store_elimination(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    
    // Return quickly if this instruction doesn't overrwrite
    registerindx r;
    if (!opcode_overwritesforinstruction(instr, &r)) return false;
    bool success=false;
    
    instructionindx iindx;
    if (!optimize_isempty(opt, r) &&
        optimize_countuses(opt, r)==0 &&
        optimize_source(opt, r, &iindx) &&
        block_contains(opt->currentblk, iindx) &&
        optimize_candeletedeadstore(opt, optimize_getinstructionat(opt, iindx), r)) {
        
        success=optimize_deleteinstruction(opt, iindx);
    }
    return success;
}

/* -------------------------------------
 * Constant branch elimination
 * ------------------------------------- */

static bool _constantbranchcondition(optimizer *opt, instruction instr, bool *out) {
    indx kindx;
    CHECK(optimize_findconstant(opt, DECODE_A(instr), &kindx));

    value konst = optimize_getconstant(opt, kindx);
    CHECK(MORPHO_ISBOOL(konst));

    *out = MORPHO_GETBOOLVALUE(konst);

    return true;
}

bool strategy_constant_branch_elimination(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    instruction op = DECODE_OP(instr);
    bool condition;

    CHECK(op==OP_BIF || op==OP_BIFF);
    CHECK(_constantbranchcondition(opt, instr, &condition));

    bool branchtaken = ((op==OP_BIF) ? condition : !condition);
    if (!branchtaken) {
        optimize_repairerasedconditionalbranch(opt, instr);
        optimize_replaceinstruction(opt, ENCODE_BYTE(OP_NOP));
        return true;
    }

    optimize_repairtakenconditionalbranch(opt, instr);
    optimize_replaceinstruction(opt, ENCODE_LONG(OP_B, 0, DECODE_sBx(instr)));
    return true;
}

/* -------------------------------------
 * Redundant branch elimination
 * ------------------------------------- */

bool strategy_redundant_branch_elimination(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);

    CHECK(DECODE_OP(instr)==OP_B);
    CHECK(DECODE_sBx(instr)==0);

    optimize_replaceinstruction(opt, ENCODE_BYTE(OP_NOP));
    return true;
}

/* -------------------------------------
 * Constant Immutable Constructor
 * ------------------------------------- */

// Restrict constructor folding to a small trusted set of pure builtin result types.
bool _isfoldsafeconstructortype(value v) {
    return (MORPHO_ISEQUAL(v, typebool) ||
            MORPHO_ISEQUAL(v, typerange) ||
            MORPHO_ISEQUAL(v, typestring) ||
            MORPHO_ISEQUAL(v, typetuple) ||
            MORPHO_ISEQUAL(v, typeint) ||
            MORPHO_ISEQUAL(v, typefloat));
}

static bool _israngeenumerate(value fn, value recvtype) {
    if (!MORPHO_ISEQUAL(recvtype, typerange) || !MORPHO_ISBUILTINFUNCTION(fn)) return false;

    objectbuiltinfunction *builtin = MORPHO_GETBUILTINFUNCTION(fn);
    return (MORPHO_ISSTRING(builtin->name) &&
            strcmp(MORPHO_GETCSTRING(builtin->name), "enumerate")==0);
}

/* Restrict constant method folding to builtin methods that are pure and return immutable values.
   Range.enumerate currently lacks a return annotation, but its result is guaranteed immutable. */
static bool _isfoldsafeconstantmethod(value fn, value recvtype) {
    if (!MORPHO_ISBUILTINFUNCTION(fn)) return false;
    if (_israngeenumerate(fn, recvtype)) return true;

    objectbuiltinfunction *builtin = MORPHO_GETBUILTINFUNCTION(fn);
    if (!(builtin->flags & MORPHO_FN_PUREFN)) return false;

    value rettype = signature_getreturntype(&builtin->sig);
    if (_isfoldsafeconstructortype(rettype)) return true;

    return false;
}

bool strategy_constant_immutable(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    
    registerindx rA=DECODE_A(instr);
    int nargs = DECODE_B(instr);
    int nopt = DECODE_C(instr);
    
    // Ensure call target and arguments are all constants
    int nregs = nargs + 2*nopt + 1;
    indx cindx[nregs];
    for (int i=0; i<nregs; i++) {
        CHECK(optimize_isconstant(opt, rA + i, cindx + i));
    }
    
    // Retrieve the call target
    value fn = optimize_getconstant(opt, cindx[0]);
    
    // Check the function is a constructor
    CHECK(MORPHO_ISBUILTINFUNCTION(fn) &&
          (MORPHO_GETBUILTINFUNCTION(fn)->flags & MORPHO_FN_CONSTRUCTOR));
    
    // Restrict folding to constructor result types that are known safe to precompute.
    value type = signature_getreturntype(&MORPHO_GETBUILTINFUNCTION(fn)->sig);
    if (!_isfoldsafeconstructortype(type)) return false;
        
    // A program that evaluates the required op with the selected constants.
    varray_instruction prog;
    varray_instructioninit(&prog);
    
    for (int i=0; i<nregs; i++) { // Setup load constants incl. the function
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
 * Constant Immutable Method
 * ------------------------------------- */

bool strategy_constant_method(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    registerindx rA=DECODE_A(instr), receiver = rA+1;
    int nargs = DECODE_B(instr), nopt = DECODE_C(instr);

    int nregs = nargs + 2*nopt + 2;
    indx cindx[nregs];
    for (int i=0; i<nregs; i++) CHECK(optimize_isconstant(opt, rA + i, cindx + i));

    value fn = optimize_getconstant(opt, cindx[0]);
    value recv = optimize_getconstant(opt, cindx[1]), type = MORPHO_NIL;
    CHECK(optimize_typefromvalue(recv, &type));
    CHECK(_isfoldsafeconstantmethod(fn, type));

    instruction prog[nregs + 2];
    for (int i=0; i<nregs; i++) prog[i] = ENCODE_LONG(OP_LCT, i, (instruction) cindx[i]);
    prog[nregs] = ENCODE(OP_METHOD, 0, (instruction) nargs, (instruction) nopt);
    prog[nregs + 1] = ENCODE_BYTE(OP_END);

    value new = MORPHO_NIL;
    bool success = optimize_evalsubprogram(opt, prog, 1, &new);

    if (success && !optimize_replacewithloadconstant(opt, receiver, new)) {
        morpho_freeobject(new);
        success=false;
    }

    return success;
}

/* -------------------------------------
 * Simple function inlining
 * ------------------------------------- */

static bool _strategy_inlineconstant(optimizer *opt, objectfunction *callee, indx oldkindx, indx *newkindx) {
    if (oldkindx>=callee->konst.count) return false;
    return optimize_addconstant(opt, callee->konst.data[oldkindx], newkindx);
}

static bool _strategy_inlineregister(optimizer *opt, bool ismethod, registerindx rA, int nargs, registerindx inlinebase, registerindx callee, registerindx *out) {
    registerindx mapped;

    /* Reuse the caller's call frame directly for the callee receiver/self and
       positional arguments. Only allocate fresh registers for callee locals. */
    if (ismethod) {
        if (callee==0) mapped = rA + 1;
        else if (callee<=(registerindx) nargs) mapped = rA + 1 + callee;
        else mapped = inlinebase + (callee - nargs - 1);
    } else {
        if (callee<=(registerindx) nargs) mapped = rA + callee;
        else mapped = inlinebase + (callee - nargs - 1);
    }

    if (mapped>=optimize_currentblock(opt)->func->nregs) return false;

    *out = mapped;
    return true;
}

static bool _strategy_inlinebase(optimizer *opt, bool ismethod, registerindx rA, int nargs, objectfunction *callee, registerindx *out) {
    objectfunction *caller = optimize_currentblock(opt)->func;
    registerindx highest = rA + nargs + (ismethod ? 1 : 0), used;
    int required;
    int nextra = callee->nregs - nargs - 1;

    if (optimize_highestused(opt, &used) && used>highest) highest=used;

    required = (int) highest + ((nextra>0) ? nextra : 0) + 1;
    if (required>caller->nregs) {
        optimize_requirenregs(opt, caller, required);
        return false;
    }

    *out = highest + 1;
    return true;
}

static bool _strategy_inlineinstruction(optimizer *opt, bool ismethod, objectfunction *callee, registerindx rA, int nargs, registerindx inlinebase, instruction instr, registerindx resultreg, bool *sawreturn, instruction *out) {
    instruction op = DECODE_OP(instr);
    opcodeflags flags = opcode_getflags(op);
    registerindx a = DECODE_A(instr), b = DECODE_B(instr), c = DECODE_C(instr);
    indx kindx;

    switch (op) {
        case OP_RETURN:
            CHECK(sawreturn && !(*sawreturn));
            if (DECODE_A(instr)==0) {
                CHECK(optimize_addconstant(opt, MORPHO_NIL, &kindx));
                *out = ENCODE_LONG(OP_LCT, resultreg, (instruction) kindx);
            } else {
                CHECK(DECODE_A(instr)==1);
                CHECK(_strategy_inlineregister(opt, ismethod, rA, nargs, inlinebase, DECODE_B(instr), &b));
                *out = ((b==resultreg) ? ENCODE_BYTE(OP_NOP) : ENCODE_DOUBLE(OP_MOV, resultreg, b));
            }
            *sawreturn = true;
            return true;
        case OP_LCT:
            CHECK(_strategy_inlineregister(opt, ismethod, rA, nargs, inlinebase, a, &a));
            CHECK(_strategy_inlineconstant(opt, callee, DECODE_Bx(instr), &kindx));
            *out = ENCODE_LONG(OP_LCT, a, (instruction) kindx);
            return true;
        case OP_END:
            return false;
        default:
            /* Generic register rewrite: remap any operand field that participates in the
               instruction, and leave non-register payload like Bx/sBx untouched. */
            CHECK((flags & ~(OPCODE_OVERWRITES_A | OPCODE_OVERWRITES_B | OPCODE_USES_A | OPCODE_USES_B | OPCODE_USES_C | OPCODE_USES_RANGEBC | OPCODE_NODELETE | OPCODE_PROPAGATE))==0);
            CHECK(!(flags & OPCODE_OVERWRITES_AP1));

            if (flags & (OPCODE_OVERWRITES_A | OPCODE_USES_A)) CHECK(_strategy_inlineregister(opt, ismethod, rA, nargs, inlinebase, a, &a));
            if (flags & (OPCODE_OVERWRITES_B | OPCODE_USES_B | OPCODE_USES_RANGEBC)) CHECK(_strategy_inlineregister(opt, ismethod, rA, nargs, inlinebase, b, &b));
            if (flags & (OPCODE_USES_C | OPCODE_USES_RANGEBC)) CHECK(_strategy_inlineregister(opt, ismethod, rA, nargs, inlinebase, c, &c));

            *out = ENCODE(op, a, b, c);
            return true;
    }
}

bool strategy_inline_function(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    instruction callop = DECODE_OP(instr);
    bool ismethod = (callop==OP_METHOD);
    registerindx rA = DECODE_A(instr), inlinebase;
    int nargs = DECODE_B(instr), nopt = DECODE_C(instr);
    indx kindx;
    value calleeval;
    objectfunction *callee;
    block *blk;
    varray_instruction insert;
    bool sawreturn=false;

    if (!optimize_isconstant(opt, rA, &kindx)) return false;
    calleeval = optimize_getconstant(opt, kindx);
    if (!MORPHO_ISFUNCTION(calleeval)) return false;

    callee = MORPHO_GETFUNCTION(calleeval);
    if ((callop!=OP_CALL && callop!=OP_METHOD) ||
        callee==optimize_currentblock(opt)->func ||
        nopt!=0 ||
        callee->nopt!=0 ||
        nargs!=callee->nargs ||
        functioninfolist_countblocks(&opt->functioninfo, callee)!=1 ||
        functioninfolist_countinstructions(&opt->functioninfo, callee)>STRATEGY_INLINE_MAXINSTRUCTIONS ||
        !cfgraph_findblock(&opt->graph, callee->entry, &blk) ||
        blk->func!=callee) return false;

    for (instructionindx i=blk->start; i<=blk->end; i++) {
        instruction op = DECODE_OP(optimize_getinstructionat(opt, i));
        if (op==OP_END || op==OP_LUP || op==OP_SUP || op==OP_CLOSURE || op==OP_CLOSEUP) return false;
    }

    if (!_strategy_inlinebase(opt, ismethod, rA, nargs, callee, &inlinebase)) return false;

    varray_instructioninit(&insert);

    for (instructionindx i=blk->start; i<=blk->end; i++) {
        instruction cinstr = optimize_getinstructionat(opt, i);
        instruction remapped;

        if (!_strategy_inlineinstruction(opt, ismethod, callee, rA, nargs, inlinebase, cinstr, ismethod ? rA+1 : rA, &sawreturn, &remapped)) goto cleanup;
        varray_instructionwrite(&insert, remapped);
    }

    if (!sawreturn || insert.count==0) goto cleanup;

    optimize_insertinstructionswithrestart(opt, insert.count, insert.data, true);
    varray_instructionclear(&insert);
    return true;

cleanup:
    varray_instructionclear(&insert);
    return false;
}

/* -------------------------------------
 * Range enumerate reduction
 * ------------------------------------- */

static bool _isunitrangestep(objectrange *range) {
    return (MORPHO_ISNIL(range->step) ||
            (MORPHO_ISINTEGER(range->step) && MORPHO_GETINTEGERVALUE(range->step)==1) ||
            (MORPHO_ISFLOAT(range->step) && MORPHO_GETFLOATVALUE(range->step)==1.0));
}

static bool _uniquesourceblock(optimizer *opt, block *blk, block **out) {
    if (blk->src.count!=1) return false;

    for (int i=0; i<blk->src.capacity; i++) {
        value key = blk->src.contents[i].key;
        if (MORPHO_ISINTEGER(key) &&
            cfgraph_indx(&opt->graph, (blockindx) MORPHO_GETINTEGERVALUE(key), out)) return true;
    }

    return false;
}

static bool _rangeenumerateindex(optimizer *opt, instruction instr, objectrange *range, registerindx *out) {
    registerindx rA = DECODE_A(instr), receiver = rA+1, arg = rA+2, origarg;
    block *src;

    if (DECODE_B(instr)!=1 || DECODE_C(instr)!=0) return false;
    if (!_uniquesourceblock(opt, opt->currentblk, &src)) return false;
    if (src->end<=src->start) return false;

    instruction branch = optimize_getinstructionat(opt, src->end);
    instruction cmp = optimize_getinstructionat(opt, src->end-1);
    if (DECODE_OP(cmp)!=OP_LT) return false;
    if (DECODE_OP(branch)!=OP_BIF && DECODE_OP(branch)!=OP_BIFF) return false;
    if (DECODE_A(branch)!=DECODE_A(cmp)) return false;

    /* Earlier rewrites can collapse intermediate MOV chains before this reducer runs.
       Follow aliases back to the original induction register instead of requiring a
       direct alias on the enumerate argument copy. */
    origarg = optimize_findoriginalregister(opt, arg);
    if (origarg!=DECODE_B(cmp)) return false;

    regcontents contents;
    indx kindx;
    if (!reginfolist_contents(&src->rout, DECODE_C(cmp), &contents, &kindx) || contents!=REG_CONSTANT) return false;

    value bound = optimize_getconstant(opt, kindx);
    if (!MORPHO_ISINTEGER(bound) || MORPHO_GETINTEGERVALUE(bound)!=range->nsteps) return false;
    if (!MORPHO_ISEQUAL(optimize_type(opt, receiver), typerange)) return false;

    *out = arg;
    return true;
}

bool strategy_range_reduction(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    registerindx rA = DECODE_A(instr), receiver = rA+1, indexreg;
    indx fnindx, recvindx, startindx;

    CHECK(DECODE_B(instr)==1 && DECODE_C(instr)==0);
    CHECK(optimize_isconstant(opt, rA, &fnindx));
    CHECK(optimize_isconstant(opt, receiver, &recvindx));

    value fn = optimize_getconstant(opt, fnindx);
    value recv = optimize_getconstant(opt, recvindx);
    CHECK(MORPHO_ISRANGE(recv));
    CHECK(_israngeenumerate(fn, typerange));

    objectrange *range = MORPHO_GETRANGE(recv);
    CHECK(_isunitrangestep(range));
    CHECK(_rangeenumerateindex(opt, instr, range, &indexreg));
    CHECK(optimize_addconstant(opt, range->start, &startindx));

    instruction insert[] = {
        ENCODE_LONG(OP_LCT, receiver, (instruction) startindx),
        ENCODE(OP_ADD, receiver, indexreg, receiver)
    };
    optimize_insertinstructions(opt, 2, insert);
    return true;
}

/* -------------------------------------
 * Constant global
 * ------------------------------------- */

bool strategy_constant_global(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    value konst;
    bool success=false;
    
    globalinfolist *glist = optimize_globalinfolist(opt);
    if (globalinfolist_countstore(glist, DECODE_Bx(instr))==1 &&
        globalinfolist_isconstant(glist, DECODE_Bx(instr), &konst)) {
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

/* -------------------------------------
 * Method resolution
 * ------------------------------------- */

bool strategy_method_resolution(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    bool success=false;
    objectfunction *current = optimize_currentblock(opt)->func;
    
    registerindx receiver = DECODE_A(instr)+1;
    value type = optimize_type(opt, receiver);
    indx kindx;
    
    if (MORPHO_ISCLASS(type) && // Return early if type information isn't present
        optimize_isconstant(opt, DECODE_A(instr), &kindx) &&
        optimize_hasuniquetype(opt, receiver)) {
        value label = optimize_getconstant(opt, kindx);
        value dispatchtarget = type;
        bool isclassreceiver = MORPHO_ISEQUAL(type, typeclass);

        if (isclassreceiver) {
            indx recvkindx;

            if (!optimize_isconstant(opt, receiver, &recvkindx)) return false;
            dispatchtarget = optimize_getconstant(opt, recvkindx);
            if (!MORPHO_ISCLASS(dispatchtarget)) return false;

            if (current && current->klass &&
                optimize_classisderivedfrom(current->klass, MORPHO_GETCLASS(dispatchtarget))) {
                objectclass *dispatchklass = MORPHO_GETCLASS(dispatchtarget);
                if (!(dispatchklass==current->klass && optimize_classisleaf(dispatchklass))) return false;
            }
        }

        value method;
        indx newkindx;
        if (morpho_lookupmethod(dispatchtarget, label, &method) &&
            optimize_addconstant(opt, method, &newkindx)) {
            
            // Replace invoke with an equivalent sequence of instructions
            instruction insert[] = {
                ENCODE_LONG(OP_LCT, DECODE_A(instr), (instruction) newkindx),
                ENCODE(OP_METHOD, DECODE_A(instr), DECODE_B(instr), DECODE_C(instr)),
                ENCODE_LONG(OP_LCT, DECODE_A(instr), (instruction) kindx),
            };
            optimize_insertinstructions(opt, 3, insert);
            
            success=true;
        }
    }
    
    return success;
}

/* -------------------------------------
 * Self-dispatch analysis
 * ------------------------------------- */

/** Marks functions that dispatch recursively through r0. */
bool strategy_self_dispatch(optimizer *opt) {
    registerindx target = optimize_findoriginalregister(opt, DECODE_A(optimize_getinstruction(opt)));

    if (target==0) {
        block *blk = optimize_currentblock(opt);
        functioninfolist_setflags(&opt->functioninfo, blk->func, FUNCTIONINFO_USESELF_DISPATCH);
    }

    return false;
}

/* -------------------------------------
 * Metafunction reduction
 * ------------------------------------- */

static bool _hasselfdispatch(optimizer *opt, objectmetafunction *mfn) {
    for (int i=0; i<mfn->fns.count; i++) {
        value fn = mfn->fns.data[i];
        if (MORPHO_ISFUNCTION(fn) &&
            functioninfolist_hasflags(&opt->functioninfo, MORPHO_GETFUNCTION(fn), FUNCTIONINFO_USESELF_DISPATCH)) return true;
    }

    return false;
}

static void _printreducedsignature(value fn) {
    signature *sig = metafunction_getsignature(fn);

    printf("Metafunction reduced to: ");
    morpho_printvalue(NULL, fn);
    printf(" signature=");
    if (sig) signature_print(sig);
    else printf("<none>");
    printf("\n");
}

bool strategy_metafunction_reduction(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    bool isMethod = (DECODE_OP(instr) == OP_METHOD);
    
    registerindx rA=DECODE_A(instr);
    int nargs = DECODE_B(instr);
    int nopt = DECODE_C(instr);
    
    indx kindx, newkindx;
    value fn=MORPHO_NIL, newfn=MORPHO_NIL;
    if (optimize_isconstant(opt, rA, &kindx)) fn = optimize_getconstant(opt, kindx); // Retrieve the call target
    
    if (!MORPHO_ISMETAFUNCTION(fn)) return false;
    if (_hasselfdispatch(opt, MORPHO_GETMETAFUNCTION(fn))) return false;
    
    value types[nargs];
    for (registerindx i=0; i<nargs; i++) {
        registerindx r = rA + i + (isMethod ? 2 : 1);
        value type = optimize_type(opt, r);
        if (!optimize_hasexacttype(opt, r)) type=MORPHO_NIL;

        types[i]=type;
    }

    error reduceerr = opt->err;
    if (metafunction_reduce(MORPHO_GETMETAFUNCTION(fn), nargs, types, &reduceerr, &newfn) &&
        !MORPHO_ISEQUAL(fn, newfn) &&
        optimize_addconstant(opt, newfn, &newkindx)) {
        if (opt->verbose) _printreducedsignature(newfn);
        instruction insert[] = { // Insert replacement load constant
            ENCODE_LONG(OP_LCT, rA, (instruction) newkindx),
            instr
        };
        optimize_insertinstructions(opt, 2, insert);
        
        return true;
    }

    return false;
}

/* **********************************************************************
 * Strategy definition table
 * ********************************************************************** */

optimizationstrategy strategies[] = {
    { OP_ANY,  strategy_constant_folding,                 0 },
    { OP_ANY,  strategy_dead_store_elimination,           0 },
    { OP_ANY,  strategy_register_replacement,             0 },
    { OP_MOV,  strategy_self_copy_elimination,            0 },
    { OP_B,    strategy_redundant_branch_elimination,     0 },
    { OP_BIF,  strategy_constant_branch_elimination,      0 },
    { OP_BIFF, strategy_constant_branch_elimination,      0 },
    { OP_ADD,  strategy_add_identity,                     0 },
    { OP_SUB,  strategy_sub_identity,                     0 },
    { OP_MUL,  strategy_mul_identity,                     0 },
    { OP_DIV,  strategy_div_identity,                     0 },
    { OP_POW,  strategy_pow_identity,                     0 },
    //{ OP_ANY,  strategy_common_subexpression_elimination, 0 },
    { OP_LCT,  strategy_duplicate_load,                   0 },
    { OP_LGL,  strategy_duplicate_load,                   0 },
    { OP_LUP,  strategy_duplicate_load,                   0 },
    { OP_LIX,  strategy_load_index_list,                  0 },
    { OP_CALL, strategy_constant_immutable,               0 },
    { OP_CALL, strategy_inline_function,                  0 },
    { OP_METHOD, strategy_inline_function,                0 },
    { OP_METHOD, strategy_constant_method,                0 },
    { OP_INVOKE, strategy_method_resolution,              0 },
    { OP_METHOD, strategy_range_reduction,                0 },
    { OP_POW,  strategy_power_reduction,                  0 },
    { OP_CALL, strategy_self_dispatch,                    0 },
    { OP_METHOD, strategy_self_dispatch,                  0 },
    { OP_CALL, strategy_metafunction_reduction,           0 },
    { OP_METHOD, strategy_metafunction_reduction,         0 },
    
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
            bool success = (strategies[i].fn) (opt);
            if (optimize_checkerror(opt) && opt->verbose)
                printf("Strategy error at instruction %ti (errcat=%i)\n", optimize_getinstructionindx(opt), opt->err.cat);
            if (success) return true; // Terminate if the strategy function succeeds
        }
    }
    
    return false;
}
