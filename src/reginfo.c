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
void reginfolist_write(reginfolist *rlist, instructionindx iindx, int rindx, regcontents contents, indx indx) {
    if (rindx>rlist->nreg) return;
    rlist->rinfo[rindx].contents=contents;
    rlist->rinfo[rindx].indx=indx;
    rlist->rinfo[rindx].nused=0;
    rlist->rinfo[rindx].iindx=iindx;
    rlist->rinfo[rindx].type=MORPHO_NIL;
}

/** Sets the type associated with a register */
void reginfolist_settype(reginfolist *rlist, int rindx, value type) {
    if (rindx>rlist->nreg) return;
    rlist->rinfo[rindx].type=type;
}

/** Gets the type associated with a register */
value reginfolist_type(reginfolist *rlist, int rindx) {
    if (rindx>rlist->nreg) return MORPHO_NIL;
    return rlist->rinfo[rindx].type;
}

/** Adds one to the usage counter for register i */
void reginfolist_uses(reginfolist *rlist, int rindx) {
    if (rindx>rlist->nreg) return;
    rlist->rinfo[rindx].nused++;
}

/** Gets the content type associated with a register */
bool reginfolist_contents(reginfolist *rlist, int rindx, regcontents *contents, indx *indx) {
    if (rindx>rlist->nreg) return false;
    if (contents) *contents = rlist->rinfo[rindx].contents;
    if (indx) *indx = rlist->rinfo[rindx].indx;
    return true;
}

/** Gets the instruction responsible for writing to this store */
bool reginfolist_source(reginfolist *rlist, int rindx, instructionindx *iindx) {
    if (rindx>rlist->nreg) return false;
    if (iindx) *iindx = rlist->rinfo[rindx].iindx;
    return true;
}

/** Count the number of times a register is used */
int reginfolist_countuses(reginfolist *rlist, int rindx) {
    if (rindx>rlist->nreg) return 0;
    return rlist->rinfo[rindx].nused;
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
        
        if (!MORPHO_ISNIL(rlist->rinfo[i].type)) { // Type
            printf(" ");
            morpho_printvalue(NULL, rlist->rinfo[i].type);
        }
        
        printf(" u:%i", rlist->rinfo[i].nused); // Usage
        
        if (rlist->rinfo[i].contents!=REG_EMPTY) { // Who wrote it?
            printf(" w:%i", (int) rlist->rinfo[i].iindx);
        }
        
        printf("\n");
    }
}
