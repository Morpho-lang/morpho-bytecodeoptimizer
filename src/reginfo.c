/** @file reginfo.c
 *  @author T J Atherton
 *
 *  @brief Data structure to track register status
*/

#include "morphocore.h"
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

/** Writes a value to a register */
void reginfolist_write(reginfolist *rlist, instructionindx iindx, int i, regcontents contents, indx indx) {
    if (i>rlist->nreg) return;
    rlist->rinfo[i].contents=contents;
    rlist->rinfo[i].indx=indx;
    rlist->rinfo[i].nused=0;
    rlist->rinfo[i].iindx=iindx;
    rlist->rinfo[i].type=MORPHO_NIL;
}

/** Sets the type associated with a register */
void reginfolist_settype(reginfolist *rlist, int i, value type) {
    if (i>rlist->nreg) return;
    rlist->rinfo[i].type=type;
}

/** Sets the type associated with a register */
bool reginfolist_contents(reginfolist *rlist, int i, regcontents *contents, indx *indx) {
    if (i>rlist->nreg) return false;
    if (contents) *contents = rlist->rinfo[i].contents;
    if (indx) *indx = rlist->rinfo[i].indx;
    return true;
}

/** Display the register info list */
void reginfolist_show(reginfolist *rlist) {
    for (int i=0; i<rlist->nreg; i++) {
        printf("|\tr%u :", i);
        switch (rlist->rinfo[i].contents) {
            case REG_EMPTY: break;
            case REG_REGISTER: printf(" r%td", rlist->rinfo[i].indx); break;
            case REG_GLOBAL: printf(" g%td", rlist->rinfo[i].indx); break;
            case REG_CONSTANT: printf(" c%td", rlist->rinfo[i].indx); break;
            case REG_UPVALUE: printf(" u%td", rlist->rinfo[i].indx); break;
            default: break;
        }
        if (!MORPHO_ISNIL(rlist->rinfo[i].type)) {
            printf(" ");
            morpho_printvalue(NULL, rlist->rinfo[i].type);
        }
        printf("\n");
    }
}
