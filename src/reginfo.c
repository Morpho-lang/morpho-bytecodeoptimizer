/** @file reginfo.c
 *  @author T J Atherton
 *
 *  @brief Data structure to track register status
*/

#include "optimize.h"
#include "reginfo.h"
#include "cfgraph.h"

/** Initializes a reginfo structure */
void reginfo_init(reginfo *info) {
    info->contents=REG_EMPTY;
    info->indx=0;
    info->iindx=INSTRUCTIONINDX_EMPTY;
    info->nused=0;
    info->type=MORPHO_NIL;
}

/** Initialize a reginfo list */
void reginfolist_init(reginfolist *rlist, int nreg) {
    rlist->nreg=nreg;
    for (int i=0; i<nreg; i++) reginfo_init(&rlist->rinfo[i]);
}

/** Display the register info list */
void reginfolist_show(reginfolist *rlist) {
    
}
