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
