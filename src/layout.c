/** @file layout.c
 *  @author T J Atherton
 *
 *  @brief Layout final program from control flow graph
*/

#include "morphocore.h"
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
    dictionary map;
} blockcomposer;

/** Initialize composer structure */
void blockcomposer_init(blockcomposer *comp, program *in, cfgraph *graph) {
    comp->in=in;
    comp->graph=graph;
    
    varray_instructioninit(&comp->out);
    cfgraph_init(&comp->outgraph);
    dictionary_init(&comp->map);
}

/** Clear composer structure */
void blockcomposer_clear(blockcomposer *comp) {
    varray_instructionclear(&comp->out);
    cfgraph_clear(&comp->outgraph);
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
    varray_blockadd(&comp->outgraph, new, 1);
    dictionary_insert(&comp->map, MORPHO_INTEGER(old->start), MORPHO_INTEGER(new->start));
}

/** Find a block in the source graph */
bool blockcomposer_findsrc(blockcomposer *comp, instructionindx start, block **out) {
    return cfgraph_findsrtd(comp->graph, start, out);
}

/** Maps a block id to the new block id */
bool blockcomposer_map(blockcomposer *comp, instructionindx old, instructionindx *new) {
    value val;
    bool success=dictionary_get(&comp->map, MORPHO_INTEGER(old), &val);
    
    if (success && new) {
        *new = MORPHO_GETINTEGERVALUE(val);
    }
    
    return success;
}

/** Processes a block by copying instrucitons from a source block  */
void blockcomposer_processblock(blockcomposer *comp, block *blk) {
    block out;
    block_init(&out);
    
    out.start=comp->out.count;
    
    for (instructionindx i=blk->start; i<=blk->end; i++) {
        instruction instr = blockcomposer_getinstruction(comp, i);
        if (DECODE_OP(instr)!=OP_NOP) {
            blockcomposer_addinstruction(comp, instr);
        }
    }
    
    out.end=comp->out.count-1;
    
    blockcomposer_addblock(comp, blk, &out);
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

void _fixbrnch(blockcomposer *comp, instruction last, instructionindx newend, instructionindx dest) {
    instructionindx newdest;
    if (blockcomposer_map(comp, dest, &newdest)) {
        instruction newinstr = ENCODE_LONG(DECODE_OP(last), 
                                           DECODE_A(last),
                                           newdest - newend -1);
        blockcomposer_setinstructionat(comp, newend, newinstr);
    }
}

/** Fixes branch instructions */
void blockcomposer_fixbranch(blockcomposer *comp, block *old, block *new) {
    instruction last = blockcomposer_getinstruction(comp, old->end);
    if (!(opcode_getflags(DECODE_OP(last)) & OPCODE_BRANCH)) return; // Only process branches
    
    instructionindx dest[2] = { INSTRUCTIONINDX_EMPTY, INSTRUCTIONINDX_EMPTY };
    int n=blockcomposer_dictflatten(&old->dest, 2, dest);
    
    if (DECODE_OP(last)==OP_B || DECODE_OP(last)==OP_POPERR) {
        _fixbrnch(comp, last, new->end, dest[0]);
        
    } else if (DECODE_OP(last)==OP_BIF || DECODE_OP(last)==OP_BIFF) {
        if (dest[0]!=old->end+1) {
            _fixbrnch(comp, last, new->end, dest[0]);
        } else {
            _fixbrnch(comp, last, new->end, dest[1]);
        }
        
    } else if (DECODE_OP(last)==OP_PUSHERR) {
        UNREACHABLE("PUSHERR unimplemented.");
    }
}
 
/* **********************************************************************
 * Layout optimized blocks
 * ********************************************************************** */

/** Layout and consolidate output program */
void layout_build(optimizer *opt) {
    blockcomposer comp;
    
    blockcomposer_init(&comp, opt->prog, &opt->graph);
    cfgraph_sort(comp.graph);
    
    // Copy across blocks
    for (unsigned int i=0; i<comp.graph->count; i++) {
        blockcomposer_processblock(&comp, comp.graph->data+i);
    }
    
    // Fix branch instructions
    for (unsigned int i=0; i<comp.graph->count; i++) {
        blockcomposer_fixbranch(&comp, comp.graph->data+i, comp.outgraph.data+i);
    }
    
    // Copy new code across
    comp.in->code.count=0;
    varray_instructionadd(&comp.in->code, comp.out.data, comp.out.count);
    
    // Patch in annotations
    
    blockcomposer_clear(&comp);
}
