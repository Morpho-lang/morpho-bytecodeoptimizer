/** @file layout.c
 *  @author T J Atherton
 *
 *  @brief Layout final program from control flow graph
*/

#include "morphocore.h"
#include <stdlib.h>
#include <morpho/debug.h>
#include "layout.h"
#include "cfgraph.h"
#include "opcodes.h"
#include "optimize.h"

/* **********************************************************************
 * Block composer
 * ********************************************************************** */

typedef struct {
    cfgraph *graph;
    program *in;
    
    cfgraph outgraph;
    varray_instruction out;
    dictionary outtables;
    dictionary map;
} blockcomposer;

/** Initialize composer structure */
void blockcomposer_init(blockcomposer *comp, program *in, cfgraph *graph) {
    comp->in=in;
    comp->graph=graph;
    
    cfgraph_init(&comp->outgraph);
    varray_instructioninit(&comp->out);
    dictionary_init(&comp->outtables);
    dictionary_init(&comp->map);
}

/** Clear composer structure */
void blockcomposer_clear(blockcomposer *comp) {
    cfgraph_clear(&comp->outgraph);
    varray_instructionclear(&comp->out);
    dictionary_clear(&comp->outtables);
    dictionary_clear(&comp->map);
}

/** Retrieve instruction at a given index */
instruction blockcomposer_getinstruction(blockcomposer *comp, instructionindx i) {
    return comp->in->code.data[i];
}

/** Add an instruction to the output program */
instructionindx blockcomposer_addinstruction(blockcomposer *comp, instruction instr) {
    return (instructionindx) varray_instructionwrite(&comp->out, instr);
}

/** Add an instruction to the output program */
void blockcomposer_setinstructionat(blockcomposer *comp, instructionindx i, instruction instr) {
    if (i>=comp->out.count) return;
    comp->out.data[i]=instr;
}

/** Adds a block to the composer data structure */
void blockcomposer_addblock(blockcomposer *comp, block *old, block *new) {
    blockindx oldindx;
    varray_blockadd(&comp->outgraph, new, 1);
    if (cfgraph_findindx(comp->graph, old, &oldindx)) {
        dictionary_insert(&comp->map, MORPHO_INTEGER(oldindx), MORPHO_INTEGER(comp->outgraph.count-1));
    }
}

/** Adds a branch table to the composer data structure */
void blockcomposer_addbranchtable(blockcomposer *comp, value table) {
    dictionary_insert(&comp->outtables, table, MORPHO_NIL);
}

/** Find a block in the source graph */
bool blockcomposer_findsrc(blockcomposer *comp, instructionindx start, block **out) {
    return cfgraph_findblock(comp->graph, start, out);
}

/** Flattens the contents of a dictionary into  */
int blockcomposer_dictflatten(dictionary *dict, int nmax, instructionindx *indx) {
    int k=0;
    for (int i=0; i<dict->capacity && k<nmax; i++) {
        if (!MORPHO_ISNIL(dict->contents[i].key)) {
            indx[k]=(instructionindx) MORPHO_GETINTEGERVALUE(dict->contents[i].key);
            k++;
        }
    }
    
    return k;
}

void _fixbrnch(blockcomposer *comp, instruction last, instructionindx newend, blockindx dest) {
    block *destblk;
    value mapped;

    if (dictionary_get(&comp->map, MORPHO_INTEGER(dest), &mapped) &&
        MORPHO_ISINTEGER(mapped) &&
        cfgraph_indx(&comp->outgraph, MORPHO_GETINTEGERVALUE(mapped), &destblk)) {
        instruction newinstr = ENCODE_LONG(DECODE_OP(last),
                                           DECODE_A(last),
                                           destblk->start - newend -1);
        blockcomposer_setinstructionat(comp, newend, newinstr);
    } else {
        UNREACHABLE("Missing block");
    }
}

/** Fixes branch instructions */
void blockcomposer_fixbranch(blockcomposer *comp, blockindx i) {
    block *old, *new;
    value mapped;
    
    if (!(cfgraph_indx(comp->graph, i, &old) &&
          dictionary_get(&comp->map, MORPHO_INTEGER(i), &mapped) &&
          MORPHO_ISINTEGER(mapped) &&
          cfgraph_indx(&comp->outgraph, MORPHO_GETINTEGERVALUE(mapped), &new))) return;
    
    instruction last = blockcomposer_getinstruction(comp, old->end);
    if (!((opcode_getflags(DECODE_OP(last)) & (OPCODE_BRANCH | OPCODE_BRANCH_TABLE)) )) return; // Only process branches
    
    instructionindx dest[2] = { INSTRUCTIONINDX_EMPTY, INSTRUCTIONINDX_EMPTY };
    int n=blockcomposer_dictflatten(&old->dest, 2, dest);
    
    if (DECODE_OP(last)==OP_B || DECODE_OP(last)==OP_POPERR) {
        _fixbrnch(comp, last, new->end, dest[0]);
        
    } else if (DECODE_OP(last)==OP_BIF || DECODE_OP(last)==OP_BIFF) {
        if (n<2 && DECODE_sBx(last)!=0) UNREACHABLE("Couldn't fix branch instruction due to error in control flow graph");
        if (n>1 && dest[0]==i+1) {
            _fixbrnch(comp, last, new->end, dest[1]);
        } else {
            _fixbrnch(comp, last, new->end, dest[0]);
        }
        
    } else if (DECODE_OP(last)==OP_PUSHERR) {
        indx kindx = DECODE_Bx(last);
        value btable = block_getconstant(old, kindx);
        
        blockcomposer_addbranchtable(comp, btable);
    }
}

/** Maps an instruction indx that starts a block in the original source to a new instruction */
bool _mapblockostart(blockcomposer *comp, instructionindx old, instructionindx *new) {
    bool success=false;
    instructionindx blkindx;
    block *dest;
    value mapped;
    if (cfgraph_findblockostart(comp->graph, old, &dest) && // Find block in old cfgraph
        cfgraph_findindx(comp->graph, dest, &blkindx) && // Retrieve the index
        dictionary_get(&comp->map, MORPHO_INTEGER(blkindx), &mapped) &&
        MORPHO_ISINTEGER(mapped) &&
        cfgraph_indx(&comp->outgraph, MORPHO_GETINTEGERVALUE(mapped), &dest)) { // Find the corresponding block in the new cfgraph
        *new = dest->start;
        success=true;
    }
    return success;
}

/** Fixes a branch table */
void blockcomposer_fixbranchtable(blockcomposer *comp, dictionary *table) {
    for (int i=0; i<table->capacity; i++) {
        value key = table->contents[i].key;
        if (MORPHO_ISNIL(key)) continue;
        
        instructionindx old = MORPHO_GETINTEGERVALUE(table->contents[i].val);
        instructionindx new;
        
        if (_mapblockostart(comp, old, &new)) {
            dictionary_insert(table, key, MORPHO_INTEGER(new));
        } else UNREACHABLE("Branch table entry not found.");
    }
}

/** Fixes the function corresponding */
void blockcomposer_fixfunction(blockcomposer *comp, objectfunction *func, instructionindx entry) {
    func->entry=entry;
}

static bool blockcomposer_blockhasrealinstructions(blockcomposer *comp, block *blk) {
    for (instructionindx i=blk->start; i<=blk->end; i++) {
        if (DECODE_OP(blockcomposer_getinstruction(comp, i))!=OP_NOP) return true;
    }

    return false;
}

/** Processes a block by copying instructions from a source block  */
void blockcomposer_processblock(blockcomposer *comp, block *blk) {
    block out;
    block_init(&out, blk->func, comp->out.count);
    
    for (instructionindx i=blk->start; i<=blk->end; i++) {
        instruction instr = blockcomposer_getinstruction(comp, i);
        if (DECODE_OP(instr)!=OP_NOP) {
            blockcomposer_addinstruction(comp, instr);
        }
    }

    // Preserve structure for empty non-entry blocks; empty entry blocks are relocated later.
    if (comp->out.count==out.start) {
        if (!block_isentry(blk)) {
            blockcomposer_addinstruction(comp, ENCODE_BYTE(OP_NOP));
        } else {
            return;
        }
    }
    
    out.end=comp->out.count-1;
    
    blockcomposer_addblock(comp, blk, &out);
}

/* **********************************************************************
 * Remove unused instructions
 * ********************************************************************** */

/** Sorts the control flow graph */
void layout_sortcfgraph(optimizer *opt) {
    cfgraph_sort(&opt->graph);
}

/** Deletes unusud instructions */
void layout_deleteunused(optimizer *opt) {
    varray_instruction *code = &opt->prog->code;
    
    instructionindx last = 0;
    
    for (indx i=0; i<opt->graph.count; i++) {
        block *blk = &opt->graph.data[i];
        
        // Erase any instructions before this block
        for (instructionindx k=last; k<blk->start; k++) optimize_replaceinstructionat(opt, k, ENCODE_BYTE(OP_NOP));
            
        last = blk->end+1;
    }
    
    // Erase to end of program
    for (instructionindx k=last; k<opt->prog->code.count; k++) optimize_replaceinstructionat(opt, k, ENCODE_BYTE(OP_NOP));
    
    varray_instructionwrite(&opt->prog->code, ENCODE_BYTE(OP_END));
}

/* **********************************************************************
 * Fix annotations
 * ********************************************************************** */

typedef struct {
    debugannotation *element;
    bool hasboundary;
    varray_debugannotation boundary;
} annotationanchor;

static void annotationanchor_add(annotationanchor *anchor, debugannotation *ann) {
    if (!anchor->hasboundary) {
        varray_debugannotationinit(&anchor->boundary);
        anchor->hasboundary = true;
    }

    varray_debugannotationadd(&anchor->boundary, ann, 1);
}

static void annotationanchors_clear(annotationanchor *anchors, instructionindx nanchors) {
    for (instructionindx i=0; i<nanchors; i++) {
        if (anchors[i].hasboundary) {
            varray_debugannotationclear(&anchors[i].boundary);
        }
    }
}

static void annotationfixer_flush(varray_debugannotation *out, debugannotation *element, int *count) {
    if (!element || *count<=0) return;

    debugannotation ann = *element;
    ann.content.element.ninstr = *count;
    varray_debugannotationadd(out, &ann, 1);
    *count = 0;
}

static void annotationfixer_append(varray_debugannotation *out, varray_debugannotation *pending) {
    for (unsigned int i=0; i<pending->count; i++) {
        varray_debugannotationadd(out, &pending->data[i], 1);
    }
    pending->count = 0;
}

/** Rebuild annotations in final emitted instruction order. */
static void blockcomposer_fixannotations(blockcomposer *comp) {
    program *prog = comp->in;
    instructionindx oldcount = prog->code.count;
    annotationanchor *anchors = calloc(oldcount+1, sizeof(annotationanchor));
    varray_debugannotation pending;
    varray_debugannotation out;
    varray_debugannotation displaced;
    instructionindx iindx = 0;
    debugannotation *current = NULL;
    int currentcount = 0;

    if (!anchors) return;

    varray_debugannotationinit(&pending);
    varray_debugannotationinit(&out);
    varray_debugannotationinit(&displaced);

    if (prog->annotations.count>0) {
        for (unsigned int i=0; i<prog->annotations.count; i++) {
            debugannotation *ann = &prog->annotations.data[i];

            if (ann->type==DEBUG_ELEMENT) {
                instructionindx anchor = (iindx<=oldcount) ? iindx : oldcount;
                for (unsigned int j=0; j<pending.count; j++) {
                    annotationanchor_add(&anchors[anchor], &pending.data[j]);
                }
                pending.count = 0;

                instructionindx end = iindx + ann->content.element.ninstr;
                if (end>oldcount) end=oldcount;
                for (instructionindx j=iindx; j<end; j++) {
                    anchors[j].element = ann;
                }

                iindx += ann->content.element.ninstr;
            } else {
                varray_debugannotationadd(&pending, ann, 1);
            }
        }

        instructionindx anchor = (iindx<=oldcount) ? iindx : oldcount;
        for (unsigned int j=0; j<pending.count; j++) {
            annotationanchor_add(&anchors[anchor], &pending.data[j]);
        }
        pending.count = 0;
    }

    if (comp->graph->count>0) {
        for (unsigned int i=0; i<comp->graph->count; i++) {
            block *blk = comp->graph->data+i;
            value mapped;
            bool emitted = dictionary_get(&comp->map, MORPHO_INTEGER(i), &mapped) && MORPHO_ISINTEGER(mapped);
            bool copied = false;
            debugannotation *fallback = current;

            if (!emitted) continue;

            for (instructionindx j=blk->start; j<=blk->end && j<oldcount; j++) {
                if (anchors[j].element) fallback = anchors[j].element;
                if (anchors[j].hasboundary) {
                    for (unsigned int k=0; k<anchors[j].boundary.count; k++) {
                        varray_debugannotationadd(&displaced, &anchors[j].boundary.data[k], 1);
                    }
                }

                instruction instr = blockcomposer_getinstruction(comp, j);
                if (DECODE_OP(instr)==OP_NOP) continue;

                if (displaced.count) {
                    annotationfixer_flush(&out, current, &currentcount);
                    annotationfixer_append(&out, &displaced);
                }

                if (anchors[j].element) {
                    if (current != anchors[j].element) {
                        annotationfixer_flush(&out, current, &currentcount);
                        current = anchors[j].element;
                    }
                } else if (!current) {
                    current = fallback;
                }

                currentcount++;
                copied = true;
            }

            if (!copied) {
                if (displaced.count) {
                    annotationfixer_flush(&out, current, &currentcount);
                    annotationfixer_append(&out, &displaced);
                }

                if (fallback && current != fallback) {
                    annotationfixer_flush(&out, current, &currentcount);
                    current = fallback;
                } else if (!current) {
                    current = fallback;
                }
                currentcount++;
            }
        }
    }

    if (displaced.count) {
        annotationfixer_flush(&out, current, &currentcount);
        annotationfixer_append(&out, &displaced);
    }
    annotationfixer_flush(&out, current, &currentcount);

    varray_debugannotation tmp = prog->annotations;
    prog->annotations = out;
    out = tmp;

    varray_debugannotationclear(&pending);
    varray_debugannotationclear(&out);
    varray_debugannotationclear(&displaced);
    annotationanchors_clear(anchors, oldcount+1);
    free(anchors);
}

/* **********************************************************************
 * Layout optimized blocks
 * ********************************************************************** */

static bool _findmappedentry(blockcomposer *comp, block *blk, dictionary *checked, instructionindx *entry) {
    blockindx blkindx;
    if (!cfgraph_findindx(comp->graph, blk, &blkindx) ||
        dictionary_get(checked, MORPHO_INTEGER(blkindx), NULL)) return false;

    dictionary_insert(checked, MORPHO_INTEGER(blkindx), MORPHO_NIL);

    bool found = false;
    value mapped;
    block *outblk;
    if (dictionary_get(&comp->map, MORPHO_INTEGER(blkindx), &mapped) &&
        MORPHO_ISINTEGER(mapped) &&
        cfgraph_indx(&comp->outgraph, MORPHO_GETINTEGERVALUE(mapped), &outblk)) {
        *entry = outblk->start;
        found = true;
    }

    for (int i=0; i<blk->dest.capacity; i++) {
        value key = blk->dest.contents[i].key;
        block *dest;
        instructionindx candidate;
        if (MORPHO_ISINTEGER(key) &&
            cfgraph_indx(comp->graph, MORPHO_GETINTEGERVALUE(key), &dest) &&
            _findmappedentry(comp, dest, checked, &candidate)) {
            if (!found || candidate < *entry) {
                *entry = candidate;
                found = true;
            }
        }
    }

    return found;
}

static void blockcomposer_fixentries(blockcomposer *comp) {
    for (unsigned int i=0; i<comp->graph->count; i++) {
        block *blk = comp->graph->data+i;
        if (!block_isentry(blk)) continue;

        dictionary checked;
        dictionary_init(&checked);
        instructionindx entry;
        if (_findmappedentry(comp, blk, &checked, &entry)) {
            blockcomposer_fixfunction(comp, blk->func, entry);
        }
        dictionary_clear(&checked);
    }
}

static void blockcomposer_fixcount(blockcomposer *comp) {
    instructionindx max = INSTRUCTIONINDX_EMPTY;

    for (unsigned int i=0; i<comp->outgraph.count; i++) {
        block *blk = &comp->outgraph.data[i];
        if (blk->end > max) max = blk->end;
    }

    comp->out.count = (max==INSTRUCTIONINDX_EMPTY ? 0 : max+1);
}

/** Layout and consolidate output program */
void layout_consolidate(optimizer *opt) {
    blockcomposer comp;
    blockcomposer_init(&comp, opt->prog, &opt->graph);
    
    // Copy across blocks
    for (unsigned int i=0; i<comp.graph->count; i++) {
        block *blk = comp.graph->data+i;
        if (optimize_blockisunreachable(blk)) continue;
        if (block_isentry(blk) && !blockcomposer_blockhasrealinstructions(&comp, blk)) continue;
        blockcomposer_processblock(&comp, blk);
    }

    blockcomposer_fixentries(&comp);
    
    if (opt->verbose) cfgraph_show(&comp.outgraph);
    
    // Fix branch instructions
    for (unsigned int i=0; i<comp.graph->count; i++) {
        blockcomposer_fixbranch(&comp, i);
    }
    
    // Fix branch tables
    for (int i=0; i<comp.outtables.capacity; i++) {
        value key = comp.outtables.contents[i].key;
        if (MORPHO_ISDICTIONARY(key)) {
            blockcomposer_fixbranchtable(&comp, &MORPHO_GETDICTIONARY(key)->dict);
        }
    }

    blockcomposer_fixcount(&comp);
    blockcomposer_fixannotations(&comp);

    // Swap old and new code
    varray_instruction tmp = comp.in->code;
    comp.in->code=comp.out; comp.out=tmp;
    
    blockcomposer_clear(&comp);
}

/* **********************************************************************
 * Layout
 * ********************************************************************** */

/** Layout the destination program, repairing data structures as necessary */
void layout(optimizer *opt) {
    layout_sortcfgraph(opt);
    layout_deleteunused(opt);
    layout_consolidate(opt);
}
