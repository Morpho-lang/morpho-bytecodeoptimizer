/** @file block.h
 *  @author T J Atherton
 *
 *  @brief Basic blocks
*/

#ifndef cfgraph_h
#define cfgraph_h

#include "optimize.h"

DECLARE_VARRAY(instructionindx, instructionindx)

/* **********************************************************************
 * Basic block data structure
 * ********************************************************************** */

#define INSTRUCTIONINDX_EMPTY -1

typedef struct sblock {
    instructionindx start; /** First instruction in the block */
    instructionindx end; /** Last instruction in the block */
} block;

/* **********************************************************************
 * Control flow graph
 * ********************************************************************** */

DECLARE_VARRAY(block, block);

typedef varray_block cfgraph; 

/* **********************************************************************
 * Interface
 * ********************************************************************** */

void block_init(block *b);
void block_clear(block *b);

void cfgraph_init(cfgraph *graph);
void cfgraph_clear(cfgraph *graph);
void cfgraph_show(cfgraph *graph);

void cfgraph_build(program *in, cfgraph *out);

#endif
