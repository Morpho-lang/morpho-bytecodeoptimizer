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

/** Gets the type of a global */
value optimize_getglobaltype(optimizer *opt, indx ix) {
    if (opt->globals) return opt->globals[ix].type;
    return MORPHO_NIL;
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
