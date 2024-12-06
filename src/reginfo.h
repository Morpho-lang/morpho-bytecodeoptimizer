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
    REG_VALUE,    /** A value */
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
void reginfolist_settype(reginfolist *rlist, int rindx, value type);
void reginfolist_uses(reginfolist *rlist, int rindx);

value reginfolist_type(reginfolist *rlist, int rindx);
bool reginfolist_contents(reginfolist *rlist, int rindx, regcontents *contents, indx *indx);
bool reginfolist_source(reginfolist *rlist, int rindx, instructionindx *iindx);
int reginfolist_countuses(reginfolist *rlist, int rindx);

void reginfolist_show(reginfolist *rlist);

#endif
