/** @file optimize.c
 *  @author T J Atherton
 *
 *  @brief Optimizer for compiled morpho bytecode
*/

#include "optimize.h"
#include "block.h"

/* **********************************************************************
 * Optimizer data structure
 * ********************************************************************** */

typedef struct {
    program *prog;
    
    varray_block cfgraph;
} optimizer;

/** Initializes an optimizer data structure */
void optimizer_init(optimizer *opt, program *prog) {
    opt->prog=prog;
    varray_blockinit(&opt->cfgraph);
}

/** Clears an optimizer data structure */
void optimize_clear(optimizer *opt) {
    varray_blockclear(&opt->cfgraph);
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

/** Show the current code blocks*/
void optimize_showcodeblocks(optimizer *opt) {
    for (int i=0; i<opt->cfgraph.count; i++) {
        block *blk = opt->cfgraph.data+i;
        printf("Block %u [%td, %td]\n", i, blk->start, blk->end);
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
                // optimize_branchto(opt, block, optimizer_currentindx(opt)+1, worklist);
                // Fallthrough to generate block at branch target
            case OP_B:
            case OP_POPERR:
            {
                int branchby = DECODE_sBx(instr);
                //optimize_branchto(opt, block, optimizer_currentindx(opt)+1+branchby, worklist);
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
    optimize_buildblock(opt, 0);
    
    optimize_showcodeblocks(opt);
}

/* **********************************************************************
 * Optimizer 
 * ********************************************************************** */

/** Public interface to optimizer */
bool optimize(program *in) {
    optimizer opt;
    
    optimizer_init(&opt, in);
    
    printf("Optimizing.\n");
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
