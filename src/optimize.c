/** @file optimize.c
 *  @author T J Atherton
 *
 *  @brief Optimizer for compiled morpho bytecode
*/

#include "optimize.h"
#include "cfgraph.h"

/* **********************************************************************
 * Opcodes
 * ********************************************************************** */

typedef struct {
    instruction code;
    unsigned int flags;
} opcode;

#define OPCODE_BLANK            0
#define OPCODE_OVERWRITES_A     (1<<0)
#define OPCODE_OVERWRITES_B     (1<<1)
#define OPCODE_USES_A           (1<<2)
#define OPCODE_USES_B           (1<<3)
#define OPCODE_USES_C           (1<<4)
#define OPCODE_USES_RANGEBC     (1<<5)
#define OPCODE_ENDSBLOCK        (1<<6)
#define OPCODE_UNSUPPORTED      (1<<7)
#define OPCODE_BRANCH           (1<<8)
#define OPCODE_CONDITIONAL      (1<<9)

opcode opcodetable[] = {
    { OP_NOP, OPCODE_BLANK },
    
    { OP_MOV, OPCODE_OVERWRITES_A | OPCODE_USES_B },
    
    { OP_ADD, OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C },
    { OP_SUB, OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C },
    { OP_MUL, OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C },
    { OP_DIV, OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C },
    { OP_POW, OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C },
    
    { OP_EQ, OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C },
    { OP_NEQ, OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C },
    { OP_LT, OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C },
    { OP_LE, OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C },
    
    { OP_NOT, OPCODE_OVERWRITES_A | OPCODE_USES_B },
    
    { OP_PUSHERR, OPCODE_BLANK },
    { OP_POPERR, OPCODE_ENDSBLOCK },
    { OP_B, OPCODE_ENDSBLOCK | OPCODE_BRANCH },
    { OP_BIF, OPCODE_ENDSBLOCK | OPCODE_BRANCH | OPCODE_CONDITIONAL | OPCODE_USES_A },
    { OP_BIFF, OPCODE_ENDSBLOCK | OPCODE_BRANCH | OPCODE_CONDITIONAL | OPCODE_USES_A },
    
    { OP_CALL, OPCODE_UNSUPPORTED },
    { OP_INVOKE, OPCODE_UNSUPPORTED },
    { OP_RETURN, OPCODE_UNSUPPORTED },
    
    { OP_CLOSEUP, OPCODE_UNSUPPORTED },
    
    { OP_LCT, OPCODE_OVERWRITES_A },
    { OP_LGL, OPCODE_OVERWRITES_A },
    { OP_SGL, OPCODE_USES_A },
    { OP_LPR, OPCODE_OVERWRITES_A | OPCODE_USES_B | OPCODE_USES_C },
    { OP_SPR, OPCODE_USES_A | OPCODE_USES_B | OPCODE_USES_C },
    { OP_LUP, OPCODE_OVERWRITES_A },
    { OP_SUP, OPCODE_USES_B },
    { OP_LIX, OPCODE_OVERWRITES_B | OPCODE_USES_A | OPCODE_USES_RANGEBC },
    { OP_SIX, OPCODE_USES_A | OPCODE_USES_RANGEBC },
    
    { OP_CLOSURE, OPCODE_UNSUPPORTED },
    
    { OP_PRINT, OPCODE_USES_A },
    
    { OP_BREAK, OPCODE_BLANK },
    
    { OP_CAT, OPCODE_UNSUPPORTED },
    
    { OP_END, OPCODE_ENDSBLOCK }
}

/* **********************************************************************
 * Optimizer data structure
 * ********************************************************************** */

typedef struct {
    program *prog;
    
    cfgraph graph;
} optimizer;

/** Initializes an optimizer data structure */
void optimizer_init(optimizer *opt, program *prog) {
    opt->prog=prog;
    cfgraph_init(&opt->graph);
}

/** Clears an optimizer data structure */
void optimize_clear(optimizer *opt) {
    cfgraph_clear(&opt->graph);
}

/* **********************************************************************
 * Optimizer 
 * ********************************************************************** */

/** Public interface to optimizer */
bool optimize(program *in) {
    optimizer opt;
    
    optimizer_init(&opt, in);
    
    cfgraph_build(in, &opt.graph);
    
    optimize_clear(&opt);
    
    return true;
}

/* **********************************************************************
 * Initialization/Finalization
 * ********************************************************************** */

void bytecodeoptimizer_initialize(void) {
    morpho_setoptimizer(optimize);
}
