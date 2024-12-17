/** @file opcodes.h
 *  @author T J Atherton
 *
 *  @brief Database of opcodes and their properties
*/

#ifndef opcodes_h
#define opcodes_h

#include "morphocore.h"
#include "optimize.h"

typedef unsigned int opcodeflags;

#define OPCODE_BLANK            0
#define OPCODE_OVERWRITES_A     (1<<0)
#define OPCODE_OVERWRITES_AP1   (1<<1)
#define OPCODE_OVERWRITES_B     (1<<2)
#define OPCODE_USES_A           (1<<3)
#define OPCODE_USES_B           (1<<4)
#define OPCODE_USES_C           (1<<5)
#define OPCODE_USES_RANGEBC     (1<<6)
#define OPCODE_ENDSBLOCK        (1<<7)
#define OPCODE_BRANCH           (1<<8)
#define OPCODE_NEWBLOCKAFTER    (1<<9)
#define OPCODE_BRANCH_TABLE     (1<<10)
#define OPCODE_TERMINATING      (1<<11)
#define OPCODE_SIDEEFFECTS      (1<<12)
#define OPCODE_UNSUPPORTED      (1<<13)

/* **********************************************************************
 * Interface
 * ********************************************************************** */

opcodeflags opcode_getflags(instruction opcode);
opcodetrackingfn opcode_gettrackingfn(instruction opcode);
opcodeusagefn opcode_getusagefn(instruction opcode);
opcodetrackingfn opcode_getreplacefn(instruction opcode);

void opcode_usageforinstruction(block *blk, instruction instr, usagecallbackfn usagefn, void *ref);
bool opcode_overwritesforinstruction(instruction instr, registerindx *out);

void opcode_initialize(void);

#endif
