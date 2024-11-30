/** @file block.c
 *  @author T J Atherton
 *
 *  @brief Basic blocks
*/

#include "block.h"

DEFINE_VARRAY(block, block)

/** Initializes a basic block structure */
void block_init(block *b) {
    b->start=INSTRUCTIONINDX_EMPTY;
    b->end=INSTRUCTIONINDX_EMPTY;
}

/** Clears a basic block structure */
void block_clear(block *b) {
    
}
