/** @file optimize.h
 *  @author T J Atherton
 *
 *  @brief Optimizer for compiled morpho bytecode
*/

#ifndef optimize_h
#define optimize_h

#include "morphocore.h"
#include "reginfo.h"
#include "cfgraph.h"

/* **********************************************************************
 * Optimizer data structure
 * ********************************************************************** */

typedef struct {
    program *prog;
    
    cfgraph graph;

    reginfolist rlist;
    
    block *currentblk; 
    instructionindx pc;
    instruction current;
    int nchanged; /** Number of instructions changed in this pass */
    
    vm *v; /** VM to execute subprograms */
    program *temp; /** Temporary program structure */
} optimizer;

/** Function that will be called by the optimizer to set the contents of the register info file */
typedef void (*opcodetrackingfn) (optimizer *opt);

extern value typestring;
extern value typebool;

/* **********************************************************************
 * Interface for optimization strategies
 * ********************************************************************** */

void optimize_write(optimizer *opt, registerindx i, regcontents contents, indx indx);
void optimize_settype(optimizer *opt, registerindx i, value type);
value optimize_type(optimizer *opt, registerindx r);
bool optimize_typefromvalue(value val, value *type);

value optimize_getconstant(optimizer *opt, indx i);
bool optimize_addconstant(optimizer *opt, value val, indx *indx);
bool optimize_isconstant(optimizer *opt, registerindx i, indx *indx);

instruction optimize_getinstruction(optimizer *opt);
void optimize_replaceinstruction(optimizer *opt, instruction instr);

void optimize_disassemble(optimizer *opt);

#endif
