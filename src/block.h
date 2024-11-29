/** @file block.h
 *  @author T J Atherton
 *
 *  @brief Basic blocks
*/

#ifndef block_h
#define block_h

#include "optimize.h"

typedef struct sblock {
    instructionindx start; /** First instruction in the block */
    instructionindx end; /** Last instruction in the block */
} block;

DECLARE_VARRAY(block, block)

void block_init(block *b);
void block_clear(block *b);

#endif
