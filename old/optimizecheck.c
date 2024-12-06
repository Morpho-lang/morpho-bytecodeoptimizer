/** @file optimize.c
 *  @author T J Atherton
 *
 *  @brief Optimizer for compiled code
*/

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

/* **********************************************************************
 * Handling code annotations
 * ********************************************************************** */

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
* Control Flow graph
* ********************************************************************** */

/** Build a code block from the current starting point
 * @param[in] opt - optimizer
 * @param[in] block - block to process
 * @param[out] worklist - worklist of blocks to process; updated */
void optimize_buildblock(optimizer *opt, codeblockindx block, varray_codeblockindx *worklist) {
    optimize_moveto(opt, optimize_getstart(opt, block));
    
    while (!optimize_atend(opt)) {
        switch (opt->op) {
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
            default:
                break;
        }
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
