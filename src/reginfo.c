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
    info->ndup=0; 
}

/** Initialize a reginfo list */
void reginfolist_init(reginfolist *rlist, int nreg) {
    rlist->nreg=nreg;
    rlist->rinfo=MORPHO_MALLOC(sizeof(reginfo)*nreg);
    if (rlist->rinfo) for (int i=0; i<nreg; i++) reginfo_init(&rlist->rinfo[i]);
}

/** Clears a reginfo list */
void reginfolist_clear(reginfolist *rlist) {
    if (rlist->rinfo) MORPHO_FREE(rlist->rinfo);
}

/** Wipes a reginfo list */
void reginfolist_wipe(reginfolist *rlist, int nreg) {
    rlist->nreg=nreg;
    for (int i=0; i<nreg; i++) reginfo_init(&rlist->rinfo[i]);
}

/** Copys a reginfo list */
bool reginfolist_copy(reginfolist *src, reginfolist *dest) {
    if (src->nreg>dest->nreg) return false;
    for (int i=0; i<src->nreg; i++) dest->rinfo[i]=src->rinfo[i];
    return true;
}

/** Writes a value to a register */
void reginfolist_write(reginfolist *rlist, instructionindx iindx, int rindx, regcontents contents, indx indx) {
    if (rindx>rlist->nreg) return;
    
    // Repair other registers if this one has been duplicated
    if (rlist->rinfo[rindx].ndup>0) reginfolist_unduplicate(rlist, rindx);
    
    rlist->rinfo[rindx].contents=contents;
    if (contents==REG_REGISTER) reginfolist_duplicate(rlist, (registerindx) indx); // Track duplication
    
    rlist->rinfo[rindx].indx=indx;
    rlist->rinfo[rindx].nused=0;
    rlist->rinfo[rindx].iindx=iindx;
    rlist->rinfo[rindx].type=MORPHO_NIL;
    rlist->rinfo[rindx].ndup=0;
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

/** Gets the content type and indx associated with a register */
bool reginfolist_contents(reginfolist *rlist, int rindx, regcontents *contents, indx *indx) {
    if (rindx>rlist->nreg) return false;
    if (contents) *contents = rlist->rinfo[rindx].contents;
    if (indx) *indx = rlist->rinfo[rindx].indx;
    return true;
}

/** Gets the content type associated with a register */
regcontents reginfolist_regcontents(reginfolist *rlist, int rindx) {
    if (rindx>rlist->nreg) return REG_EMPTY;
    return rlist->rinfo[rindx].contents;
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

/** Indicate a register is duplicated */
void reginfolist_duplicate(reginfolist *rlist, int rindx) {
    if (rindx>rlist->nreg) return;
    rlist->rinfo[rindx].ndup++;
}

/** Repairs duplicate registers when the original is overwritten */
void reginfolist_unduplicate(reginfolist *rlist, int rindx) {
    regcontents srccontents; // Content type of the source
    indx srcindx; // Indx of the source
    
    reginfolist_contents(rlist, rindx, &srccontents, &srcindx);
    value srctype = reginfolist_type(rlist, rindx);
    
    for (registerindx i=0; i<rlist->nreg; i++) { // Look for registers
        if (i==rindx) continue; // Skip over the tautological case
        
        regcontents icontents;
        indx iindx;
        
        reginfolist_contents(rlist, i, &icontents, &iindx);
        if (icontents==REG_REGISTER &&
            iindx==rindx) {
            // Move the contents from the source register into the duplicate reg i
            instructionindx src;
            reginfolist_source(rlist, i, &src); // The write instruction should remain the duplicating instruction
            
            reginfolist_write(rlist, src, i, srccontents, srcindx);
            if (!MORPHO_ISNIL(srctype)) reginfolist_settype(rlist, i, srctype);
            
            // Preserve usage count
            int nused = reginfolist_countuses(rlist, rindx);
            rlist->rinfo[i].nused=nused;
        }
        
    }
}

/** Checks for any registers containing a given content type with specified index and converts to a value  */
void reginfolist_invalidate(reginfolist *rlist, regcontents contents, indx ix) {
    for (registerindx i=0; i<rlist->nreg; i++) { // Look over registers
        regcontents icontents;
        indx iindx;
        
        reginfolist_contents(rlist, i, &icontents, &iindx);
        if (icontents==contents &&
            iindx==ix) {
            rlist->rinfo[i].contents=REG_VALUE;
        }
        
    }
}

/** Display the register info list */
void reginfolist_show(reginfolist *rlist) {
    for (int i=0; i<rlist->nreg; i++) {
        printf("|\tr%u :", i);
        switch (rlist->rinfo[i].contents) {
            case REG_EMPTY: printf(" \n"); continue;
            case REG_PARAMETER: printf(" p"); break; 
            case REG_VALUE: printf(" v"); break;
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
        
        if (rlist->rinfo[i].ndup) printf(" d:%i", rlist->rinfo[i].ndup); // Duplication
        
        if (rlist->rinfo[i].contents!=REG_EMPTY) { // Who wrote it?
            printf(" w:%i", (int) rlist->rinfo[i].iindx);
        }
        
        printf("\n");
    }
}
