/** @file cfgraph.c
 *  @author T J Atherton
 *
 *  @brief Control flow graph
*/

#include "cfgraph.h"

DEFINE_VARRAY(instructionindx, instructionindx)
DEFINE_VARRAY(block, block)

typedef unsigned int blockindx;

/* **********************************************************************
 * Basic blocks
 * ********************************************************************** */

/** Initializes a basic block structure */
void block_init(block *b) {
    b->start=INSTRUCTIONINDX_EMPTY;
    b->end=INSTRUCTIONINDX_EMPTY;
}

/** Clears a basic block structure */
void block_clear(block *b) {
    
}

/* **********************************************************************
 * Control flow graph data structure
 * ********************************************************************** */

/** Initializes an cfgraph data structure */
void cfgraph_init(cfgraph *graph) {
    varray_blockinit(graph);
}

/** Clears an cfgraph data structure */
void cfgraph_clear(cfgraph *graph) {
    varray_blockclear(graph);
}

/** Shows code blocks in a cfgraph */
void cfgraph_show(cfgraph *graph) {
    for (int i=0; i<graph->count; i++) {
        block *blk = graph->data+i;
        printf("Block %u [%td, %td]\n", i, blk->start, blk->end);
    }
}

/** Adds a block to the control flow graph */
bool cfgraph_addblock(cfgraph *graph, block *blk) {
    return varray_blockadd(graph, blk, 1);
}

/** Finds a block with instruction indx that lies within it
 * @param[in] opt - optimizer
 * @param[in] indx - index to find
 * @param[out] blk - returns a temporary pointer to the block
 * @returns true if found, false otherwise */
bool cfgraph_find(cfgraph *graph, instructionindx indx, block **blk) {
    for (blockindx i=0; i<graph->count; i++) {
        if (indx>=graph->data[i].start &&
            indx<=graph->data[i].end) {
            if (blk) *blk = graph->data+i;
            return true;
        }
    }
    return false;
}

/* **********************************************************************
 * Control flow graph builder
 * ********************************************************************** */

/** Data structure to hold temporary information while building the cf graph */
typedef struct {
    program *in;
    cfgraph *out;
    varray_instructionindx worklist;
} cfgraphbuilder;

/** Initializes an optimizer data structure */
void cfgraphbuilder_init(cfgraphbuilder *bld, program *in, cfgraph *out) {
    bld->in=in;
    bld->out=out;
    varray_instructionindxinit(&bld->worklist);
}

/** Clears an optimizer data structure */
void cfgraphbuilder_clear(cfgraphbuilder *bld) {
    varray_instructionindxclear(&bld->worklist);
}

/** Adds a block to the worklist */
void cfgraphbuilder_push(cfgraphbuilder *bld, instructionindx start) {
    varray_instructionindxadd(&bld->worklist, &start, 1);
}

/** Pops a block item off the worklist */
bool cfgraphbuilder_pop(cfgraphbuilder *bld, instructionindx *item) {
    return varray_instructionindxpop(&bld->worklist, item);
}

/** Count the total number of instructions in the input program */
int cfgraphbuilder_countinstructions(cfgraphbuilder *bld) {
    return bld->in->code.count;
}

/** Fetches the instruction at index i */
instruction cfgraphbuilder_fetch(cfgraphbuilder *bld, instructionindx i) {
    return bld->in->code.data[i];
}

/** Processes a branch instruction.
 * @details Finds whether the branch points to or wthin an existing block and either splits it as necessary or creates a new block
 * @param[in] opt - optimizer
 * @param[in] start - Instructionindx that would start the block  */
void cfgraphbuilder_branchto(cfgraphbuilder *bld, instructionindx start) {
    block *old;
        
    if (cfgraph_find(bld->out, start, &old)) {
        //optimize_splitblock(opt, destblock, dest);
    } else {
        cfgraphbuilder_push(bld, start);
    }
}

/** Creates a new basic block starting at a given instruction */
void cfgraphbuilder_buildblock(cfgraphbuilder *bld, instructionindx start) {
    block blk;
    block_init(&blk);
    blk.start=start;
    
    for (instructionindx i=start; i<cfgraphbuilder_countinstructions(bld); i++) {
        instruction instr = cfgraphbuilder_fetch(bld, i);
        
        switch (DECODE_OP(instr)) { // Check for block-terminating instructions
            default: continue;
            case OP_BIF: // Conditional branches generate a block immediately after
            case OP_BIFF:
                cfgraphbuilder_branchto(bld, i+1);
                // Fallthrough to also generate block at branch target
            case OP_B:
            case OP_POPERR:
            {
                int branchby = DECODE_sBx(instr);
                cfgraphbuilder_branchto(bld, i+1+branchby);
            }
                break;
            case OP_PUSHERR:
            case OP_RETURN:
            case OP_END:
                break;
        }
        
        blk.end=i;
        break;
    }
    
    cfgraph_addblock(bld->out, &blk);
}

/** Builds a control flow graph */
void cfgraph_build(program *in, cfgraph *out) {
    cfgraphbuilder bld;
    
    cfgraphbuilder_init(&bld, in, out);
    
    cfgraphbuilder_branchto(&bld, 0);
    
    instructionindx item;
    while (cfgraphbuilder_pop(&bld, &item)) {
        cfgraphbuilder_buildblock(&bld, item);
    }
    
    cfgraphbuilder_clear(&bld);
    
    cfgraph_show(out);
}
