/** @file opcodes.h
 *  @author T J Atherton
 *
 *  @brief Database of opcodes and their properties
*/

#ifndef opcodes_h
#define opcodes_h

#include "optimize.h"

typedef unsigned int opcodeflags;

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

bool opcode_getflags(instruction opcode, opcodeflags *flags);

void opcode_initialize(void);

#endif
