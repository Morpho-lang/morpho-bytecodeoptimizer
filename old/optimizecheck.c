/** @file optimize.c
 *  @author T J Atherton
 *
 *  @brief Optimizer for compiled code
*/

#include "optimize.h"
#include "debug.h"
#include "vm.h"

/* **********************************************************************
 * Data structures
 * ********************************************************************** */

/* -----------
 * Globals
 * ----------- */

/** Initialize globals */
void optimize_initglobals(optimizer *opt) {
    if (!opt->globals) return;
    for (unsigned int i=0; i<opt->out->nglobals; i++) {
        opt->globals[i].contains=NOTHING;
        opt->globals[i].used=0;
        opt->globals[i].type=MORPHO_NIL;
        opt->globals[i].contents=MORPHO_NIL;
    }
}

/** Indicates an instruction uses a global */
void optimize_useglobal(optimizer *opt, indx ix) {
    if (opt->globals) opt->globals[ix].used++;
}

/** Remove a reference to a global */
void optimize_unuseglobal(optimizer *opt, indx ix) {
    if (opt->globals) opt->globals[ix].used--;
}

/** Updates the contents of a global */
void optimize_globalcontents(optimizer *opt, indx ix, returntype type, indx id) {
    if (opt->globals) {
        if (opt->globals[ix].contains==NOTHING) {
            opt->globals[ix].contains = type;
            opt->globals[ix].id = id;
        } else if (opt->globals[ix].contains==type) {
            if (opt->globals[ix].id!=id) opt->globals[ix].id = GLOBAL_UNALLOCATED;
        } else {
            opt->globals[ix].contains = VALUE;
            opt->globals[ix].id = GLOBAL_UNALLOCATED;
        }
    }
}

/** Decides whether two types match */
bool optimize_matchtype(value t1, value t2) {
    return MORPHO_ISSAME(t1, t2);
}

/** Updates a globals type  */
void optimize_updateglobaltype(optimizer *opt, indx ix, value type) {
    if (!opt->globals) return;
    
    if (MORPHO_ISNIL(type) || MORPHO_ISNIL(opt->globals[ix].type)) {
        opt->globals[ix].type = type;
    } else if (MORPHO_ISSAME(opt->globals[ix].contents, OPTIMIZER_AMBIGUOUS)) {
        return;
    } else if (!optimize_matchtype(opt->globals[ix].type, type)) opt->globals[ix].type = OPTIMIZER_AMBIGUOUS;
}

/** Updates a globals value  */
void optimize_updateglobalcontents(optimizer *opt, indx ix, value val) {
    if (!opt->globals) return;
    
    if (MORPHO_ISNIL(opt->globals[ix].contents)) {
        opt->globals[ix].contents = val;
    } else if (MORPHO_ISSAME(opt->globals[ix].contents, OPTIMIZER_AMBIGUOUS)) {
        return; 
    } else if (!MORPHO_ISEQUAL(val, opt->globals[ix].contents)) opt->globals[ix].contents = OPTIMIZER_AMBIGUOUS;
}

/** Gets the type of a global */
value optimize_getglobaltype(optimizer *opt, indx ix) {
    if (opt->globals) return opt->globals[ix].type;
    return MORPHO_NIL;
}

/** Gets the contents of a global */
bool optimize_getglobalcontents(optimizer *opt, indx ix, returntype *contains, indx *id, value *contents) {
    if (opt->globals) {
        if (contains) *contains = opt->globals[ix].contains;
        if (id) *id = opt->globals[ix].id;
        if (contents) *contents = opt->globals[ix].contents;
        return true;
    }
    return false;
}

/* -----------
 * Reginfo
 * ----------- */

/** Invalidates any old copies of a stored quantity */
void optimize_reginvalidate(optimizer *opt, returntype type, indx id) {
    for (unsigned int i=0; i<opt->maxreg; i++) {
        if (opt->reg[i].contains==type && opt->reg[i].id==id) {
            opt->reg[i].contains=VALUE;
        }
    }
}

/** Resolves the type of value produced by an arithmetic instruction */
void optimize_resolvearithmetictype(optimizer *opt) {
    registerindx a=DECODE_A(opt->current);
    registerindx b=DECODE_B(opt->current);
    registerindx c=DECODE_C(opt->current);
    value ta = MORPHO_NIL, tb = opt->reg[b].type, tc = opt->reg[c].type;
    
    if (MORPHO_ISINTEGER(tb) && MORPHO_ISINTEGER(tc)) {
        ta = MORPHO_INTEGER(1);
    } else if ((MORPHO_ISINTEGER(tb) && MORPHO_ISFLOAT(tc)) ||
               (MORPHO_ISFLOAT(tb) && MORPHO_ISINTEGER(tc)) ||
               (MORPHO_ISFLOAT(tb) && MORPHO_ISFLOAT(tc))) {
        ta = MORPHO_FLOAT(1.0);
    }
    
    optimize_regsettype(opt, a, ta);
}

/* ------------
 * Search
 * ------------ */

/** Trace back through duplicate registers */
registerindx optimize_findoriginalregister(optimizer *opt, registerindx reg) {
    registerindx out=reg;
    while (opt->reg[out].contains==REGISTER) {
        out=(registerindx) opt->reg[out].id;
        if (out==reg) return out;
    }
    return out;
}

/* **********************************************************************
 * Handling code annotations
 * ********************************************************************** */

/** Restarts annotations */
void optimize_restartannotation(optimizer *opt) {
    opt->a=0;
    opt->aindx=0;
    opt->aoffset=0;
}

/** Gets the current annotation */
debugannotation *optimize_currentannotation(optimizer *opt) {
    return &opt->out->annotations.data[opt->a];
}

/** Get next annotation and update annotation counters */
void optimize_annotationadvance(optimizer *opt) {
    debugannotation *ann=optimize_currentannotation(opt);
    
    opt->aoffset=0;
    if (ann->type==DEBUG_ELEMENT) opt->aindx+=ann->content.element.ninstr;
    
    opt->a++;
}

/** Are we at the end of annotations */
bool optimize_annotationatend(optimizer *opt) {
    return !(opt->a < opt->out->annotations.count);
}

/** Moves the annotation  system to a specified instruction */
void optimize_annotationmoveto(optimizer *opt, instructionindx ix) {
    indx lastelement = 0;
    if (ix<opt->aindx) optimize_restartannotation(opt);
    for (;
         !optimize_annotationatend(opt);
         optimize_annotationadvance(opt)) {
        
        debugannotation *ann=optimize_currentannotation(opt);
        if (ann->type==DEBUG_ELEMENT) {
            if (opt->aindx+ann->content.element.ninstr>ix) {
                opt->aoffset=ix-opt->aindx;
                // If we are at the start of an element, instead return us to just after the last element record
                if (opt->aoffset==0 && lastelement<opt->a) opt->a=lastelement+1;
                return;
            }
            
            lastelement=opt->a;
        }
    }
}

/** Copies across annotations for a specific code block */
void optimize_annotationcopyforblock(optimizer *opt, codeblock *block) {
    optimize_annotationmoveto(opt, block->start);
    instructionindx iindx = block->start;
    
    for (;
         !optimize_annotationatend(opt);
         optimize_annotationadvance(opt)) {
        debugannotation *ann=optimize_currentannotation(opt);
        
        if (ann->type==DEBUG_ELEMENT) {
            // Figure out how many instructions are left in the block and in the annotation
            instructionindx remaininginblock = block->end-iindx+1;
            instructionindx remaininginann = ann->content.element.ninstr-opt->aoffset;
            
            int nnops=0; // Count NOPs which will be deleted by compactify
            
            instructionindx ninstr=(remaininginann<remaininginblock ? remaininginann : remaininginblock);
            
            for (int i=0; i<ninstr; i++) {
                instruction instruction=optimize_fetchinstructionat(opt, i+iindx);
                if (DECODE_OP(instruction)==OP_NOP) nnops++;
            }
            
            if (ninstr>nnops) {
                debugannotation new = { .type=DEBUG_ELEMENT,
                    .content.element.ninstr = (int) ninstr - nnops,
                    .content.element.line = ann->content.element.line,
                    .content.element.posn = ann->content.element.posn
                };
                varray_debugannotationwrite(&opt->aout, new);
            }
            
            iindx+=ninstr;
            
            // Break if we are done with the block
            if (remaininginblock<=remaininginann) break;
        } else {
            // Copy across non element records
            varray_debugannotationadd(&opt->aout, ann, 1);
        }
    }
}

/** Copies across and fixes annotations */
void optimize_fixannotations(optimizer *opt, varray_codeblockindx *blocks) {
#ifdef MORPHO_DEBUG_LOGOPTIMIZER
    debug_showannotations(&opt->out->annotations);
#endif
    optimize_restartannotation(opt);
    for (unsigned int i=0; i<blocks->count; i++) {
//        printf("Fixing annotations for block %i\n", blocks->data[i]);
        codeblock *block = optimize_getblock(opt, blocks->data[i]);
        optimize_annotationcopyforblock(opt, block);
    }
#ifdef MORPHO_DEBUG_LOGOPTIMIZER
    debug_showannotations(&opt->aout);
#endif
}

/* **********************************************************************
* Decode instructions
* ********************************************************************** */

/** Track contents of registers etc */
void optimize_track(optimizer *opt) {
    instruction instr=opt->current;
    
    int op=DECODE_OP(instr);
    switch (op) {
        case OP_NOP: // Opcodes to ignore
        case OP_PUSHERR:
        case OP_POPERR:
        case OP_BREAK:
        case OP_END:
        case OP_B:
        case OP_CLOSEUP:
            break;
        case OP_MOV:
            //optimize_reguse(opt, DECODE_B(instr));
            //optimize_regoverwrite(opt, DECODE_A(instr));
            //optimize_regcontents(opt, DECODE_A(instr), REGISTER, DECODE_B(instr));
            //optimize_regsettype(opt, DECODE_A(instr), optimize_getregtype(opt, DECODE_B(instr)));
            break;
        case OP_LCT:
            //optimize_regoverwrite(opt, DECODE_A(instr));
            //optimize_regcontents(opt, DECODE_A(instr), CONSTANT, DECODE_Bx(instr));
            //if (opt->func && DECODE_Bx(instr)<opt->func->konst.count) {
            //    value k = opt->func->konst.data[DECODE_Bx(instr)];
            //    optimize_regsettype(opt, DECODE_A(instr), k);
            //}
            break;
        case OP_ADD:
        case OP_SUB:
        case OP_MUL:
        case OP_DIV:
        case OP_POW:
            //optimize_reguse(opt, DECODE_B(instr));
            //optimize_reguse(opt, DECODE_C(instr));
            //optimize_regoverwrite(opt, DECODE_A(instr));
            //optimize_regcontents(opt, DECODE_A(instr), VALUE, REGISTER_UNALLOCATED);
            optimize_resolvearithmetictype(opt);
            break;
        case OP_EQ:
        case OP_NEQ:
        case OP_LT:
        case OP_LE:
            //optimize_reguse(opt, DECODE_B(instr));
            //optimize_reguse(opt, DECODE_C(instr));
            //optimize_regoverwrite(opt, DECODE_A(instr));
            //optimize_regcontents(opt, DECODE_A(instr), VALUE, REGISTER_UNALLOCATED);
            //optimize_regsettype(opt, DECODE_A(instr), MORPHO_TRUE);
            break;
        case OP_NOT:
            //optimize_reguse(opt, DECODE_B(instr));
            //optimize_regoverwrite(opt, DECODE_A(instr));
            //optimize_regcontents(opt, DECODE_A(instr), VALUE, REGISTER_UNALLOCATED);
            //optimize_regsettype(opt, DECODE_A(instr), MORPHO_TRUE);
            break;
        case OP_BIF:
        case OP_BIFF:
            //optimize_reguse(opt, DECODE_A(instr));
            break;
        case OP_CALL:
        {
            //registerindx a = DECODE_A(instr);
            //registerindx b = DECODE_B(instr);
            //optimize_reguse(opt, a);
            //for (unsigned int i=0; i<b; i++) {
            //    optimize_reguse(opt, a+i+1);
            //    opt->reg[a+i+1].contains=NOTHING; // call uses and overwrites arguments.
            //}
            //optimize_regoverwrite(opt, DECODE_A(instr));
            //optimize_regcontents(opt, DECODE_A(instr), VALUE, REGISTER_UNALLOCATED);
        }
            break;
        case OP_INVOKE:
        {
            //registerindx a = DECODE_A(instr);
            //registerindx b = DECODE_B(instr);
            //registerindx c = DECODE_C(instr);
            //optimize_reguse(opt, a);
            //optimize_reguse(opt, b);
            //for (unsigned int i=0; i<c; i++) {
            //    optimize_reguse(opt, a+i+1);
            //    opt->reg[a+i+1].contains=NOTHING; // invoke uses and overwrites arguments.
            //}
            //optimize_regoverwrite(opt, a);
            //optimize_regcontents(opt, a, VALUE, REGISTER_UNALLOCATED);
        }
            break;
        case OP_RETURN:
            //if (DECODE_A(instr)>0) optimize_reguse(opt, DECODE_B(instr));
            break;
        case OP_LGL:
        {
            //registerindx a = DECODE_A(instr);
            //optimize_regoverwrite(opt, a);
            optimize_useglobal(opt, DECODE_Bx(instr));
            //optimize_regcontents(opt, a, GLOBAL, DECODE_Bx(instr));
            optimize_regsettype(opt, DECODE_A(instr), optimize_getglobaltype(opt, DECODE_Bx(instr)));
        }
            break;
        case OP_SGL:
            optimize_reginvalidate(opt, GLOBAL, DECODE_Bx(instr));
            //optimize_reguse(opt, DECODE_A(instr));
            //optimize_regcontents(opt, DECODE_A(instr), GLOBAL, DECODE_Bx(instr));
            break;
        case OP_LPR:
        {
            //registerindx a = DECODE_A(instr);
            //optimize_reguse(opt, DECODE_B(instr));
            //optimize_reguse(opt, DECODE_C(instr));
            //optimize_regoverwrite(opt, a);
            optimize_regcontents(opt, a, VALUE, REGISTER_UNALLOCATED);
        }
            break;
        case OP_SPR:
            //optimize_reguse(opt, DECODE_A(instr));
            //optimize_reguse(opt, DECODE_B(instr));
            //optimize_reguse(opt, DECODE_C(instr));
            break;
        case OP_CLOSURE:
        {
            optimize_reguse(opt, DECODE_A(instr));
            registerindx b = DECODE_B(instr); // Get which registers are used from the upvalue prototype
            varray_upvalue *v = &opt->func->prototype.data[b];
            for (unsigned int i=0; i<v->count; i++) optimize_reguse(opt, (registerindx) v->data[i].reg);
            optimize_regoverwrite(opt, DECODE_A(instr)); // Generates a closure in register
            optimize_regcontents(opt, DECODE_A(instr), VALUE, REGISTER_UNALLOCATED);
        }
            break;
        case OP_LUP:
            //optimize_regoverwrite(opt, DECODE_A(instr));
            //optimize_regcontents(opt, DECODE_A(instr), UPVALUE, DECODE_B(instr));
            //optimize_regcontents(opt, DECODE_A(instr), VALUE, REGISTER_UNALLOCATED);
            break;
        case OP_SUP:
            //optimize_reguse(opt, DECODE_B(instr));
            break;
        case OP_LIX:
        {
            //registerindx a=DECODE_A(instr);
            //registerindx b=DECODE_B(instr);
            //registerindx c=DECODE_C(instr);
            //optimize_reguse(opt, a);
            //for (unsigned int i=b; i<=c; i++) optimize_reguse(opt, i);
            //optimize_regoverwrite(opt, b);
            //optimize_regcontents(opt, b, VALUE, REGISTER_UNALLOCATED);
        }
            break;
        case OP_SIX:
        {
            //registerindx a=DECODE_A(instr);
            //registerindx b=DECODE_B(instr);
            //registerindx c=DECODE_C(instr);
            //optimize_reguse(opt, a);
            //for (unsigned int i=b; i<=c; i++) optimize_reguse(opt, i);
        }
            break;
        case OP_PRINT:
            //optimize_reguse(opt, DECODE_A(instr));
            break;
        case OP_CAT:
        {
            //registerindx a=DECODE_A(instr);
            //registerindx b=DECODE_B(instr);
            //registerindx c=DECODE_C(instr);
            //for (unsigned int i=b; i<=c; i++) optimize_reguse(opt, i);
            //optimize_regoverwrite(opt, a);
            //optimize_regcontents(opt, a, VALUE, REGISTER_UNALLOCATED);
        }
            break;
        default:
            UNREACHABLE("Opcode not supported in optimizer.");
    }
}

/* **********************************************************************
* Control Flow graph
* ********************************************************************** */

/** Build a code block from the current starting point
 * @param[in] opt - optimizer
 * @param[in] block - block to process
 * @param[out] worklist - worklist of blocks to process; updated */
void optimize_buildblock(optimizer *opt, codeblockindx block, varray_codeblockindx *worklist) {
    optimize_moveto(opt, optimize_getstart(opt, block));
    
    while (!optimize_atend(opt)) {
        optimize_fetch(opt);
        
        // If we have come upon an existing block terminate this one
        codeblockindx next;
        if (optimize_findblock(opt, optimizer_currentindx(opt), &next) && next!=block) {
            optimize_adddest(opt, block, next); // and link this block to the existing one
            return; // Terminate block
        }
        
        optimize_setend(opt, block, optimizer_currentindx(opt));
        
        switch (opt->op) {
            case OP_B:
            case OP_POPERR:
            {
                //int branchby = DECODE_sBx(opt->current);
                //optimize_branchto(opt, block, optimizer_currentindx(opt)+1+branchby, worklist);
            }
                return; // Terminate current block
            case OP_BIF:
            case OP_BIFF:
            {
                //int branchby = DECODE_sBx(opt->current);
                
                // Create two new blocks, one for each possible destination
                //optimize_branchto(opt, block, optimizer_currentindx(opt)+1, worklist);
                //optimize_branchto(opt, block, optimizer_currentindx(opt)+1+branchby, worklist);
            }
                return; // Terminate current block
            case OP_PUSHERR:
            {
                int ix = DECODE_Bx(opt->current);
                if (MORPHO_ISDICTIONARY(opt->func->konst.data[ix])) {
                    objectdictionary *dict = MORPHO_GETDICTIONARY(opt->func->konst.data[ix]);
                    
                    for (unsigned int i=0; i<dict->dict.capacity; i++) {
                        if (MORPHO_ISTRUE(dict->dict.contents[i].key)) {
                            instructionindx hindx=MORPHO_GETINTEGERVALUE(dict->dict.contents[i].val);
                            codeblockindx errhandler = optimize_newblock(opt, hindx); // Start at the entry point of the program
                            optimize_setroot(opt, errhandler);
                            varray_codeblockindxwrite(worklist, errhandler);
                        }
                    }
                }
                optimize_branchto(opt, block, optimizer_currentindx(opt)+1, worklist);
            }
                return; // Terminate current block
            case OP_RETURN:
            case OP_END:
                return; // Terminate current block
            default:
                break;
        }
        
        optimize_track(opt); // Track register contents
        optimize_overwrite(opt, false);
        optimize_advance(opt);
    }
}

/** Adds a function to the control flow graph */
void optimize_addfunction(optimizer *opt, value func) {
    if (!MORPHO_ISFUNCTION(func)) return;
    dictionary_insert(&opt->functions, func, MORPHO_TRUE);
    optimize_searchlist(opt, &MORPHO_GETFUNCTION(func)->konst); // Search constant table
}

/** Builds all blocks starting from the current function */
void optimize_rootblock(optimizer *opt, varray_codeblockindx *worklist) {
    codeblockindx first = optimize_newblock(opt, opt->func->entry); // Start at the entry point of the program
    
#ifdef MORPHO_DEBUG_LOGOPTIMIZER
    printf("Building root block [%u] for function '", first);
    morpho_printvalue(MORPHO_OBJECT(opt->func));
    printf("'\n");
#endif
    
    optimize_setroot(opt, first);
    varray_codeblockindxwrite(worklist, first);
    optimize_incinbound(opt, first);

    while (worklist->count>0) {
        codeblockindx current;
        if (!varray_codeblockindxpop(worklist, &current)) UNREACHABLE("Unexpectedly empty worklist in control flow graph");
        
        optimize_buildblock(opt, current, worklist);
    }
}

/* **********************************************************************
 * Optimization strategies
 * ********************************************************************** */

typedef bool (*optimizationstrategyfn) (optimizer *opt);

typedef struct {
    int match;
    optimizationstrategyfn fn;
} optimizationstrategy;

/** Identifies duplicate constants instructions */
bool optimize_duplicate_loadconst(optimizer *opt) {
    registerindx out = DECODE_A(opt->current);
    indx cindx = DECODE_Bx(opt->current);
    
    // Find if another register contains this constant
    for (registerindx i=0; i<opt->maxreg; i++) {
        if (opt->reg[i].contains==CONSTANT &&
            opt->reg[i].id==cindx &&
            opt->reg[i].block==opt->currentblock &&
            opt->reg[i].iix<optimizer_currentindx(opt)) {
            
            if (i!=out) { // Replace with a move instruction and note the duplication
                optimize_replaceinstruction(opt, ENCODE_DOUBLE(OP_MOV, out, i));
            } else { // Register already contains this constant
                optimize_replaceinstruction(opt, ENCODE_BYTE(OP_NOP));
            }
            
            return true;
        }
    }
    
    return false;
}

/** Identifies duplicate load instructions */
bool optimize_duplicate_loadglobal(optimizer *opt) {
    registerindx out = DECODE_A(opt->current);
    indx global = DECODE_Bx(opt->current);
    
    // Find if another register contains this global
    for (registerindx i=0; i<opt->maxreg; i++) {
        if (opt->reg[i].contains==GLOBAL &&
            opt->reg[i].id==global &&
            opt->reg[i].block==opt->currentblock &&
            opt->reg[i].iix<optimizer_currentindx(opt)) { // Nonlocal eliminations require understanding the call graph to check for SGL. 
            
            if (i!=out) { // Replace with a move instruction and note the duplication
                optimize_replaceinstruction(opt, ENCODE_DOUBLE(OP_MOV, out, i));
            } else { // Register already contains this global
                optimize_replaceinstruction(opt, ENCODE_BYTE(OP_NOP));
            }
            
            return true;
        }
    }
    
    return false;
}

/** Replaces duplicate registers  */
bool optimize_register_replacement(optimizer *opt) {
    if (opt->op<OP_ADD || opt->op>OP_LE) return false; // Quickly eliminate non-arithmetic instructions
    
    instruction instr=opt->current;
    registerindx a=DECODE_A(instr),
                 b=DECODE_B(instr),
                 c=DECODE_C(instr);
    
    b=optimize_findoriginalregister(opt, b);
    c=optimize_findoriginalregister(opt, c);
    
    optimize_replaceinstruction(opt, ENCODE(opt->op, a, b, c));
    
    return false; // This allows other optimization strategies to intervene after
}


/** Searches to see if an expression has already been calculated  */
bool optimize_subexpression_elimination(optimizer *opt) {
    if (opt->op<OP_ADD || opt->op>OP_LE) return false; // Quickly eliminate non-arithmetic instructions
    static instruction mask = ( MASK_OP | MASK_B | MASK_C );
    registerindx reg[] = { DECODE_B(opt->current), DECODE_C(opt->current) } ;
    
    // Find if another register contains the same calculated value.
    for (registerindx i=0; i<opt->maxreg; i++) {
        if (opt->reg[i].contains==VALUE) {
            if (opt->reg[i].block!=opt->currentblock || // Only look within this block
                opt->reg[i].iix==INSTRUCTIONINDX_EMPTY) continue;
            instruction comp = optimize_fetchinstructionat(opt, opt->reg[i].iix);
            
            if ((comp & mask)==(opt->current & mask)) {
                /* Need to check if an instruction between the previous one and the
                   current one overwrites any operands */
                
                if (!optimize_checkoverwites(opt, opt->reg[i].iix, optimizer_currentindx(opt), (opt->op==OP_NOT ? 1 : 2), reg)) return false;
                
                optimize_replaceinstruction(opt, ENCODE_DOUBLE(OP_MOV, DECODE_A(opt->current), i));
                return true;
            }
        }
    }
    return false;
}

/** Optimize unconditional branches */
bool optimize_branch_optimization(optimizer *opt) {
    int sbx=DECODE_sBx(opt->current);
    
    if (sbx==0) {
        optimize_replaceinstruction(opt, ENCODE_BYTE(OP_NOP));
        return true;
    }
    
    return false;
}

/** Deletes unused globals  */
bool optimize_unused_global(optimizer *opt) {
    indx gid=DECODE_Bx(opt->current);
    if (!opt->globals[gid].used) { // If the global is unused, do not store to it
        optimize_replaceinstruction(opt, ENCODE_BYTE(OP_NOP));
    }
    
    return false;
}

/** Identifies globals that just contain a constant */
bool optimize_constant_global(optimizer *opt) {
    indx global = DECODE_Bx(opt->current);
    returntype contents;
    value val;
    
    if (optimize_getglobalcontents(opt, global, &contents, NULL, &val) &&
        contents==CONSTANT &&
        !OPTIMIZER_ISAMBIGUOUS(val)) {
        indx kindx;
        
        if (optimize_addconstant(opt, val, &kindx)) {
            optimize_replaceinstruction(opt, ENCODE_LONG(OP_LCT, DECODE_A(opt->current), kindx));
            optimize_unuseglobal(opt, global);
            return true;
        }
    }
    
    return false;
}

/** Tracks information written to a global */
bool optimize_storeglobal_trackcontents(optimizer *opt) {
    registerindx rix = DECODE_A(opt->current);
    value type = optimize_getregtype(opt, rix);
    
    optimize_updateglobaltype(opt, DECODE_Bx(opt->current), (MORPHO_ISNIL(type) ? OPTIMIZER_AMBIGUOUS: type));
    
    if (opt->reg[rix].contains!=NOTHING) {
        optimize_globalcontents(opt, DECODE_Bx(opt->current), opt->reg[rix].contains, opt->reg[rix].id);
        optimize_updateglobalcontents(opt, DECODE_Bx(opt->current), opt->reg[rix].type);
    }
    
    return false;
}

/* --------------------------
 * Table of strategies
 * -------------------------- */

#define OP_ANY -1
#define OP_LAST OP_END+1

// The first pass establishes the data flow from block-block
// Only put things that can act on incomplete data flow here
optimizationstrategy firstpass[] = {
    { OP_LCT, optimize_duplicate_loadconst },
    { OP_LGL, optimize_duplicate_loadglobal },
    { OP_SGL, optimize_storeglobal_trackcontents },
    { OP_POW, optimize_power_reduction },
    { OP_LAST, NULL }
};

// The second pass is for arbitrary transformations
optimizationstrategy secondpass[] = {
    { OP_ANY, optimize_register_replacement },
    { OP_ANY, optimize_subexpression_elimination },
    { OP_ANY, optimize_constant_folding },          // Must be in second pass for correct data flow
    { OP_LCT, optimize_duplicate_loadconst },
    { OP_LGL, optimize_duplicate_loadglobal },
    { OP_LGL, optimize_constant_global },           // Second pass to ensure all sgls have been seen
    { OP_B,   optimize_branch_optimization },
    { OP_SGL, optimize_unused_global },
    { OP_LAST, NULL }
};

/* **********************************************************************
 * Optimize a block
 * ********************************************************************** */

/** Restores register info from the parents of a block */
void optimize_restoreregisterstate(optimizer *opt, codeblockindx handle) {
    codeblock *block = optimize_getblock(opt, handle);
    
    // Check if all parents have been visited.
    //for (unsigned int i=0; i<block->src.count; i++) if (!optimize_getvisited(opt, block->src.data[i])) return;
    
    // Copy across the first block
    if (block->src.count>0) {
        codeblock *src = optimize_getblock(opt, block->src.data[0]);
        for (unsigned int j=0; j<src->nreg; j++) opt->reg[j]=src->reg[j];
        optimize_showregisterstateforblock(opt, block->src.data[0]);
    }
    
    /** Now update based on subsequent blocks */
    for (unsigned int i=1; i<block->src.count; i++) {
        codeblock *src = optimize_getblock(opt, block->src.data[i]);
        optimize_showregisterstateforblock(opt, block->src.data[i]);
        
        for (unsigned int j=0; j<src->nreg; j++) {
            if (src->reg[j].contains==NOTHING) continue;
            // If it doesn't match the type, we mark it as a VALUE
            if (opt->reg[j].contains!=src->reg[j].contains ||
                opt->reg[j].id!=src->reg[j].id) {
                
                opt->reg[j].contains=VALUE;
                
                // Mark block creator
                if (opt->reg[j].contains==NOTHING || opt->reg[j].block==CODEBLOCKDEST_EMPTY) {
                    opt->reg[j].block=src->reg[j].block;
                }
                // Copy usage information
                if (src->reg[j].used>opt->reg[j].used)  opt->reg[j].used=src->reg[j].used;
            }
        }
        
        if (src->nreg>opt->maxreg) opt->maxreg=src->nreg;
    }
    
#ifdef MORPHO_DEBUG_LOGOPTIMIZER
        printf("Combined register state:\n");
        optimize_regshow(opt);
#endif
}

/** Optimize a block */
void optimize_optimizeblock(optimizer *opt, codeblockindx block, optimizationstrategy *strategies) {
    instructionindx start=optimize_getstart(opt, block),
                    end=optimize_getend(opt, block);
    
    optimize_setcurrentblock(opt, block);
    optimize_setfunction(opt, optimize_getfunction(opt, block));
    
#ifdef MORPHO_DEBUG_LOGOPTIMIZER
    printf("Optimizing block %u.\n", block);
#endif
    
    do {
        opt->nchanged=0;
        optimize_restart(opt, start);
        optimize_restoreregisterstate(opt, block); // Load registers
        
        for (;
            optimizer_currentindx(opt)<=end;
            optimize_advance(opt)) {
            
            optimize_fetch(opt);
#ifdef MORPHO_DEBUG_LOGOPTIMIZER
            debug_disassembleinstruction(opt->current, optimizer_currentindx(opt), NULL, NULL);
            printf("\n");
#endif
            optimize_optimizeinstruction(opt, strategies);
            optimize_track(opt); // Track contents of registers
            optimize_overwrite(opt, true);
            
    #ifdef MORPHO_DEBUG_LOGOPTIMIZER
            optimize_regshow(opt);
    #endif
        }
    } while (opt->nchanged>0);
    
    optimize_saveregisterstatetoblock(opt, block);
    
#ifdef MORPHO_DEBUG_LOGOPTIMIZER
    printf("Optimized block %u:\n", block);
    optimize_printblock(opt, block);
#endif
}


/* **********************************************************************
 * Final processing and layout of final program
 * ********************************************************************** */

/** Fixes a pusherr dictionary */
void optimize_fixpusherr(optimizer *opt, codeblock *block, varray_instruction *dest) {
    instruction last = dest->data[block->oend];
    int ix = DECODE_Bx(last);
    if (MORPHO_ISDICTIONARY(block->func->konst.data[ix])) {
        objectdictionary *dict = MORPHO_GETDICTIONARY(block->func->konst.data[ix]);
        
        // Loop over error handler dictionary, repairing indices into code.
        for (unsigned int i=0; i<dict->dict.capacity; i++) {
            if (MORPHO_ISFALSE(dict->dict.contents[i].key)) continue;
            instructionindx hindx=MORPHO_GETINTEGERVALUE(dict->dict.contents[i].val);
            
            codeblockindx errhandler;
            if (optimize_findblock(opt, hindx, &errhandler)) {
                codeblock *h = optimize_getblock(opt, errhandler);
                if (h) dict->dict.contents[i].val=MORPHO_INTEGER(h->ostart);
                else UNREACHABLE("Couldn't find block for error handler");
            } else UNREACHABLE("Couldn't find error handler");
        }
    }
}


/** Fix function starting point */
void optimize_fixfunction(optimizer *opt, codeblock *block) {
    if (block->isroot && block->func->entry==block->start) {
        block->func->entry=block->ostart;
    }
}

/* **********************************************************************
 * Public interface
 * ********************************************************************** */

/* Perform an optimization pass */
bool optimization_pass(optimizer *opt, optimizationstrategy *strategylist) {
    // Now optimize blocks
    varray_codeblockindx worklist;
    varray_codeblockindxinit(&worklist);
    
    for (unsigned int i=0; i<opt->cfgraph.count; i++) {
        opt->cfgraph.data[i].visited=0; // Clear visit flag
        // Ensure all root blocks are on the worklist
        if (opt->cfgraph.data[i].isroot) varray_codeblockindxwrite(&worklist, i); // Add to the worklist
    }
    
    while (worklist.count>0) {
        codeblockindx current;
        if (!varray_codeblockindxpop(&worklist, &current)) UNREACHABLE("Unexpectedly empty worklist in optimizer");
        
        // Make sure we didn't already finalize this block
        if (optimize_getvisited(opt, current)>=optimize_getinbound(opt, current)) continue;
        
        optimize_optimizeblock(opt, current, strategylist);
        optimize_visit(opt, current);
        optimize_desttoworklist(opt, current, &worklist);
    }
    
    //optimize_checkunused(&opt);
    
    varray_codeblockindxclear(&worklist);
    
    return true;
}
