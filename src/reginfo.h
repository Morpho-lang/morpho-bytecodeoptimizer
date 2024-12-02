/** @file reginfo.h
 *  @author T J Atherton
 *
 *  @brief Data structure to track register status
*/

#ifndef reginfo_h
#define reginfo_h

#include "morphocore.h"

/* **********************************************************************
 * Reginfo
 * ********************************************************************** */

/** Enumerated type recording where the contents of a register came from */
typedef enum {
    REG_EMPTY,    /** Empty register */
    REG_REGISTER, /** Contents moved from another register */
    REG_CONSTANT, /** Contents came from the constant table */
    REG_GLOBAL,   /** Contents came from a global */
    REG_UPVALUE,  /** Contents came from an upvalue */
} regcontents;

/** Record information about each register */
typedef struct {
    regcontents contents; /** Source of contents */
    indx indx; /** Index of contents */
    int nused; /** Number of times the value has been referred to within the block */
    
    instructionindx iindx; /** Instruction that last wrote to this register */
    
    value type; /** Type information if known */
} reginfo;

typedef struct {
    int nreg;
    reginfo rinfo[MORPHO_MAXREGISTERS];
} reginfolist;

/* **********************************************************************
 * Interface
 * ********************************************************************** */

void reginfolist_init(reginfolist *rlist, int nreg);
void reginfolist_write(reginfolist *rlist, instructionindx iindx, int rindx, regcontents contents, indx indx);
void reginfolist_show(reginfolist *rlist);

#endif
