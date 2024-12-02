/** @file cfgraph.c
 *  @author T J Atherton
 *
 *  @brief Control flow graph
*/

#include <morpho/dictionary.h>

#include "cfgraph.h"
#include "opcodes.h"

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
    
    dictionary_init(&b->uses);
    dictionary_init(&b->writes);
}

/** Clears a basic block structure */
void block_clear(block *b) {
    dictionary_clear(&b->uses);
    dictionary_clear(&b->writes);
}

/** Declare that a block uses a given register as input */
void block_setuses(block *b, registerindx r) {
    dictionary_insert(&b->uses, MORPHO_INTEGER((int) r), MORPHO_NIL);
}

/** Declare that a block overwrites a given register */
void block_setwrites(block *b, registerindx r) {
    dictionary_insert(&b->writes, MORPHO_INTEGER((int) r), MORPHO_NIL);
}

/** Check if a block uses a given register */
bool block_uses(block *b, registerindx r) {
    return dictionary_get(&b->uses, MORPHO_INTEGER((int) r), NULL);
}

/** Check if a block overwrites a given register */
bool block_writes(block *b, registerindx r) {
    return dictionary_get(&b->writes, MORPHO_INTEGER((int) r), NULL);
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
    for (int i=0; i<graph->count; i++) block_clear(&graph->data[i]);
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

/* **********************************************************************
 * Build basic blocks
 * ********************************************************************** */

/** Splits a block at instruction 'split' */
void cfgraphbuilder_split(cfgraphbuilder *bld, block *blk, instructionindx split) {
    if (blk->start==split) return; // No need to split
    
    blk->end=split-1; // Update the end of this block
    cfgraphbuilder_push(bld, split);
}

/** Finds whether the branch points to or wthin an existing block and either splits it as necessary
    or creates a new block */
void cfgraphbuilder_branchto(cfgraphbuilder *bld, instructionindx start) {
    block *old;
        
    if (cfgraph_find(bld->out, start, &old)) {
        cfgraphbuilder_split(bld, old, start);
    } else {
        cfgraphbuilder_push(bld, start);
    }
}

/** Adds a function to the control flow graph */
void cfgraphbuilder_addfunction(cfgraphbuilder *bld, value func) {
    if (!MORPHO_ISFUNCTION(func)) return;
    cfgraphbuilder_push(bld, MORPHO_GETFUNCTION(func)->entry);
}

/** Creates a new basic block starting at a given instruction */
void cfgraphbuilder_buildblock(cfgraphbuilder *bld, instructionindx start) {
    block blk;
    block_init(&blk);
    blk.start=start;
    
    for (instructionindx i=start; i<cfgraphbuilder_countinstructions(bld); i++) {
        instruction instr = cfgraphbuilder_fetch(bld, i);
        opcodeflags flags = opcode_getflags(DECODE_OP(instr));
        
        // Conditional branches generate a block immediately afterwards
        if (flags & OPCODE_CONDITIONAL) cfgraphbuilder_branchto(bld, i+1);
        
        // Branches generate a block at the branch target
        if (flags & OPCODE_BRANCH) {
            int branchby = DECODE_sBx(instr);
            cfgraphbuilder_branchto(bld, i+1+branchby);
        }

        // At a block ending instruction record the end point and terminate
        if (flags & OPCODE_ENDSBLOCK) {
            blk.end=i;
            break;
        }
        
        // Should also check that we don't run into an existing block
    }
    
    cfgraph_addblock(bld->out, &blk);
}

/** Adds metadata to a given block */
void cfgraphbuilder_addmetadata(cfgraphbuilder *bld, block *blk) {
    for (instructionindx i=blk->start; i<cfgraphbuilder_countinstructions(bld); i++) {
        instruction instr = cfgraphbuilder_fetch(bld, i);
        opcodeflags flags = opcode_getflags(DECODE_OP(instr));
        
        if (flags & OPCODE_OVERWRITES_A) block_setwrites(blk, DECODE_A(instr));
        if (flags & OPCODE_OVERWRITES_B) block_setwrites(blk, DECODE_B(instr));
        
        if (flags & OPCODE_USES_A &&
            !block_writes(blk, DECODE_A(instr))) {
            block_setuses(blk, DECODE_A(instr));
        }
        if (flags & OPCODE_USES_B &&
            !block_writes(blk, DECODE_B(instr))) {
            block_setuses(blk, DECODE_B(instr));
        }
        if (flags & OPCODE_USES_C) {
            block_setuses(blk, DECODE_C(instr));
        }
    }
}

/* **********************************************************************
 * Build control flow graph
 * ********************************************************************** */

/** Builds a control flow graph */
void cfgraph_build(program *in, cfgraph *out) {
    cfgraphbuilder bld;
    
    cfgraphbuilder_init(&bld, in, out);
    
    cfgraphbuilder_addfunction(&bld, MORPHO_OBJECT(in->global));
    
    instructionindx item;
    while (cfgraphbuilder_pop(&bld, &item)) {
        cfgraphbuilder_buildblock(&bld, item);
    }
    
    for (int i=0; i<out->count; i++) {
        cfgraphbuilder_addmetadata(&bld, &out->data[i]);
    }
    
    cfgraphbuilder_clear(&bld);
    
    cfgraph_show(out);
}
