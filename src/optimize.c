/** @file optimize.c
 *  @author T J Atherton
 *
 *  @brief Optimizer for compiled morpho bytecode
*/

#include "optimize.h"

/* **********************************************************************
 * Optimizer data structure
 * ********************************************************************** */

typedef struct {
    program *prog;
} optimizer;

/** Initializes an optimizer data structure */
void optimizer_init(optimizer *opt, program *prog) {
    opt->prog=prog;
}

/** Clears an optimizer data structure */
void optimize_clear(optimizer *opt) {
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

/** Build a basic block starting at a given instruction */
void optimize_buildblock(optimizer *opt, instructionindx start) {
    for (instructionindx i=start; i<optimize_countinstructions(opt); i++) {
        instruction instr = optimize_fetch(opt, i);
        
        switch (DECODE_OP(instr)) {
            case OP_B:
            case OP_POPERR:
            {
                int branchby = DECODE_sBx(instr);
                //optimize_branchto(opt, block, optimizer_currentindx(opt)+1+branchby, worklist);
            }
                return; // Terminate current block
            case OP_BIF:
            case OP_BIFF:
            {
                int branchby = DECODE_sBx(instr);
                
                // Create two new blocks, one for each possible destination
                // optimize_branchto(opt, block, optimizer_currentindx(opt)+1, worklist);
                // optimize_branchto(opt, block, optimizer_currentindx(opt)+1+branchby, worklist);
            }
                return; // Terminate current block
            case OP_PUSHERR:
            {
                //
            }
                return; // Terminate current block
            case OP_RETURN:
            case OP_END:
                return; // Terminate current block
            default:
                break;
        }
    }
}

void optimize_buildcontrolflowgraph(optimizer *opt) {
    optimize_buildblock(opt, 0);
}

/* **********************************************************************
 * Optimizer 
 * ********************************************************************** */

/** Public interface to optimizer */
bool optimize(program *in) {
    optimizer opt;
    
    optimizer_init(&opt, in);
    
    printf("Optimizing\n");
    
    optimize_clear(&opt);
    
    return true;
}

/* **********************************************************************
 * Initialization/Finalization
 * ********************************************************************** */

void bytecodeoptimizer_initialize(void) {
    morpho_setoptimizer(optimize);
}
