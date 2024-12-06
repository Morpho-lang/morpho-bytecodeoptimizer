/** @file cfgraph.h
 *  @author T J Atherton
 *
 *  @brief Basic blocks
*/

#ifndef cfgraph_h
#define cfgraph_h

DECLARE_VARRAY(instructionindx, instructionindx)

/* **********************************************************************
 * Basic block data structure
 * ********************************************************************** */

#define INSTRUCTIONINDX_EMPTY -1

typedef struct sblock {
    instructionindx start; /** First instruction in the block */
    instructionindx end; /** Last instruction in the block */
    
    dictionary dest; /** Destination blocks */
    dictionary src; /** Source blocks */
    
    dictionary uses; /** Registers that the block uses as input */
    dictionary writes; /** Registers that the block writes to */
    
    objectfunction *func; /** Function that encapsulates the block */
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

void block_setuses(block *b, registerindx r);
void block_setwrites(block *b, registerindx r);
bool block_uses(block *b, registerindx r);
bool block_writes(block *b, registerindx r);

void block_setsource(block *b, instructionindx indx);
void block_setdest(block *b, instructionindx indx);

bool block_isentry(block *b);

void cfgraph_init(cfgraph *graph);
void cfgraph_clear(cfgraph *graph);
void cfgraph_show(cfgraph *graph);

void cfgraph_sort(cfgraph *graph);
bool cfgraph_findsrtd(cfgraph *graph, instructionindx start, block **out);

void cfgraph_build(program *in, cfgraph *out, bool verbose);

#endif
