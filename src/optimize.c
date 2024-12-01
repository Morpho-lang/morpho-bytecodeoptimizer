/** @file optimize.c
 *  @author T J Atherton
 *
 *  @brief Optimizer for compiled morpho bytecode
*/

#include "optimize.h"
#include "block.h"

DEFINE_VARRAY(instructionindx, instructionindx)

typedef unsigned int blockindx;

/* **********************************************************************
 * Optimizer data structure
 * ********************************************************************** */

typedef struct {
    program *prog;
    
    varray_block cfgraph;
    varray_instructionindx cfworklist;
} optimizer;

/** Initializes an optimizer data structure */
void optimizer_init(optimizer *opt, program *prog) {
    opt->prog=prog;
    varray_blockinit(&opt->cfgraph);
    varray_instructionindxinit(&opt->cfworklist);
}

/** Clears an optimizer data structure */
void optimize_clear(optimizer *opt) {
    varray_blockclear(&opt->cfgraph);
    varray_instructionindxclear(&opt->cfworklist);
}

/* **********************************************************************
 * Build the control flow graph of basic blocks
 * ********************************************************************** */

/** Count the total number of instructions */
int optimize_countinstructions(optimizer *opt) {
    return opt->prog->code.count;
}

/** Fetches the instruction at index i */
instruction optimize_fetch(optimizer *opt, instructionindx i) {
    return opt->prog->code.data[i];
}

/** Adds a block to the control flow graph */
bool optimize_addblock(optimizer *opt, block *blk) {
    return varray_blockadd(&opt->cfgraph, blk, 1);
}

/** Finds a block with instruction indx inside
 * @param[in] opt - optimizer
 * @param[in] indx - index to find
 * @param[out] block - returns the block definit
 * @returns true if found, false otherwise */
bool optimize_findblock(optimizer *opt, instructionindx indx, block **blk) {
    for (blockindx i=0; i<opt->cfgraph.count; i++) {
        if (indx>=opt->cfgraph.data[i].start &&
            indx<=opt->cfgraph.data[i].end) {
            if (blk) *blk = opt->cfgraph.data+i;
            return true;
        }
    }
    return false;
}

/** Adds a block */
bool optimize_addblocktoworklist(optimizer *opt, instructionindx start) {
    return varray_instructionindxadd(&opt->cfworklist, &start, 1);
}

/** Show the current code blocks*/
void optimize_showcodeblocks(optimizer *opt) {
    for (int i=0; i<opt->cfgraph.count; i++) {
        block *blk = opt->cfgraph.data+i;
        printf("Block %u [%td, %td]\n", i, blk->start, blk->end);
    }
}

/** Processes a branch instruction.
 * @details Finds whether the branch points to or wthin an existing block and either splits it as necessary or creates a new block
 * @param[in] opt - optimizer
 * @param[in] start - Instructionindx that would start the block  */
void optimize_branchto(optimizer *opt, instructionindx start) {
    block *old;
        
    if (optimize_findblock(opt, start, &old)) {
        //optimize_splitblock(opt, destblock, dest);
    } else {
        optimize_addblocktoworklist(opt, start);
    }
}

/** Build a basic block starting at a given instruction */
void optimize_buildblock(optimizer *opt, instructionindx start) {
    block blk;
    block_init(&blk);
    blk.start=start;
    
    for (instructionindx i=start; i<optimize_countinstructions(opt); i++) {
        instruction instr = optimize_fetch(opt, i);
        
        switch (DECODE_OP(instr)) { // Check for block-terminating instructions
            default: continue;
            case OP_BIF: // Conditional branches generate a block immediately after
            case OP_BIFF:
                optimize_branchto(opt, i+1);
                // Fallthrough to also generate block at branch target
            case OP_B:
            case OP_POPERR:
            {
                int branchby = DECODE_sBx(instr);
                optimize_branchto(opt, i+1+branchby);
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
    
    optimize_addblock(opt, &blk);
}

void optimize_buildcontrolflowgraph(optimizer *opt) {
    instructionindx current;
    
    optimize_branchto(opt, 0);
    
    while (varray_instructionindxpop(&opt->cfworklist, &current)) {
        optimize_buildblock(opt, current);
    }
    
    optimize_showcodeblocks(opt);
}

/* **********************************************************************
 * Optimizer 
 * ********************************************************************** */

/** Public interface to optimizer */
bool optimize(program *in) {
    optimizer opt;
    
    optimizer_init(&opt, in);
    
    optimize_buildcontrolflowgraph(&opt);
    
    optimize_clear(&opt);
    
    return true;
}

/* **********************************************************************
 * Initialization/Finalization
 * ********************************************************************** */

void bytecodeoptimizer_initialize(void) {
    morpho_setoptimizer(optimize);
}
