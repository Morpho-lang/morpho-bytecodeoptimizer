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
    info->nread=0;
    info->nwrite=0;
    info->type=MORPHO_NIL;
    info->typeinfo=REGTYPE_UNKNOWN;
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

static bool reginfo_hasindexedcontents(regcontents contents);

/** Checks if two reginfo records represent the same dataflow fact. */
bool reginfo_equal(reginfo *a, reginfo *b) {
    return (a->contents==b->contents &&
            a->typeinfo==b->typeinfo &&
            a->nread==b->nread &&
            a->nwrite==b->nwrite &&
            a->iindx==b->iindx &&
            a->ndup==b->ndup &&
            MORPHO_ISEQUAL(a->type, b->type) &&
            (!reginfo_hasindexedcontents(a->contents) || a->indx==b->indx));
}

/** Checks if two reginfo lists represent the same dataflow fact. */
bool reginfolist_equal(reginfolist *a, reginfolist *b) {
    if (a->nreg!=b->nreg) return false;
    for (int i=0; i<a->nreg; i++) {
        if (!reginfo_equal(&a->rinfo[i], &b->rinfo[i])) return false;
    }
    return true;
}

static bool reginfo_hasindexedcontents(regcontents contents) {
    return (contents==REG_GLOBAL ||
            contents==REG_UPVALUE ||
            contents==REG_CONSTANT ||
            contents==REG_REGISTER);
}

static void reginfo_clearsource(reginfo *info) {
    info->indx=0;
    info->iindx=INSTRUCTIONINDX_EMPTY;
}

static void reginfo_normalize(reginfo *info) {
    info->nread=0;
    info->nwrite=0;
    info->ndup=0;

    if (info->contents==REG_EMPTY || info->contents==REG_VALUE) {
        reginfo_clearsource(info);
    }

    if (MORPHO_ISNIL(info->type)) info->typeinfo=REGTYPE_UNKNOWN;
}

/** Joins register information from one predecessor into another. */
void reginfo_join(reginfo *dest, reginfo *src) {
    reginfo_normalize(dest);

    if (dest->contents==REG_EMPTY) {
        if (src->contents!=REG_EMPTY) {
            dest->contents=REG_VALUE;
            reginfo_clearsource(dest);
        }
    } else if (src->contents==REG_EMPTY) {
        dest->contents=REG_VALUE;
        reginfo_clearsource(dest);
    } else if (dest->contents!=src->contents ||
               (reginfo_hasindexedcontents(dest->contents) && dest->indx!=src->indx)) {
        dest->contents=REG_VALUE;
        reginfo_clearsource(dest);
    } else if (dest->contents==REG_VALUE) {
        reginfo_clearsource(dest);
    } else if (dest->iindx!=src->iindx) {
        dest->iindx=INSTRUCTIONINDX_EMPTY;
    }

    if (MORPHO_ISNIL(dest->type) ||
        MORPHO_ISNIL(src->type) ||
        !MORPHO_ISEQUAL(dest->type, src->type)) {
        dest->type=MORPHO_NIL;
        dest->typeinfo=REGTYPE_UNKNOWN;
    } else if (src->typeinfo>dest->typeinfo) {
        dest->typeinfo=src->typeinfo;
    }

    reginfo_normalize(dest);
}

/** Adds one to the read counter for register i */
void reginfolist_incread(reginfolist *rlist, int rindx) {
    if (rindx>=rlist->nreg) return;
    rlist->rinfo[rindx].nread++;
}

/** Adds one to the write counter for register i */
void reginfolist_incwrite(reginfolist *rlist, int rindx) {
    if (rindx>=rlist->nreg) return;
    rlist->rinfo[rindx].nwrite++;
}

/** Writes a value to a register */
void reginfolist_write(reginfolist *rlist, instructionindx iindx, int rindx, regcontents contents, indx indx) {
    if (rindx>=rlist->nreg) return;
    
    // Repair other registers if this one has been duplicated
    if (rlist->rinfo[rindx].ndup>0) reginfolist_unduplicate(rlist, rindx);
    
    rlist->rinfo[rindx].contents=contents;
    if (contents==REG_REGISTER) reginfolist_duplicate(rlist, (registerindx) indx); // Track duplication
    
    rlist->rinfo[rindx].indx=indx;
    rlist->rinfo[rindx].nread=0;
    reginfolist_incwrite(rlist, rindx);
    rlist->rinfo[rindx].iindx=iindx;
    rlist->rinfo[rindx].type=MORPHO_NIL;
    rlist->rinfo[rindx].typeinfo=REGTYPE_UNKNOWN;
    rlist->rinfo[rindx].ndup=0;
}

/** Sets the type associated with a register */
void reginfolist_settype(reginfolist *rlist, int rindx, value type) {
    reginfolist_settypeinfo(rlist, rindx, type, REGTYPE_EXACT);
}

/** Sets the type and precision associated with a register */
void reginfolist_settypeinfo(reginfolist *rlist, int rindx, value type, regtypeinfo info) {
    if (rindx>=rlist->nreg) return;
    rlist->rinfo[rindx].type=type;
    rlist->rinfo[rindx].typeinfo=(MORPHO_ISNIL(type) ? REGTYPE_UNKNOWN : info);
}

/** Gets the type associated with a register */
value reginfolist_type(reginfolist *rlist, int rindx) {
    if (rindx>=rlist->nreg) return MORPHO_NIL;
    return rlist->rinfo[rindx].type;
}

/** Gets the type precision associated with a register */
regtypeinfo reginfolist_typeinfo(reginfolist *rlist, int rindx) {
    if (rindx>=rlist->nreg) return REGTYPE_UNKNOWN;
    return rlist->rinfo[rindx].typeinfo;
}

/** Gets the content type and indx associated with a register */
bool reginfolist_contents(reginfolist *rlist, int rindx, regcontents *contents, indx *indx) {
    if (rindx>=rlist->nreg) return false;
    if (contents) *contents = rlist->rinfo[rindx].contents;
    if (indx) *indx = rlist->rinfo[rindx].indx;
    return true;
}

/** Gets the content type associated with a register */
regcontents reginfolist_regcontents(reginfolist *rlist, int rindx) {
    if (rindx>=rlist->nreg) return REG_EMPTY;
    return rlist->rinfo[rindx].contents;
}

/** Gets the instruction responsible for writing to this store */
bool reginfolist_source(reginfolist *rlist, int rindx, instructionindx *iindx) {
    if (rindx>=rlist->nreg) return false;
    if (iindx) *iindx = rlist->rinfo[rindx].iindx;
    return true;
}

/** Count the number of times a register is used */
int reginfolist_countuses(reginfolist *rlist, int rindx) {
    if (rindx>=rlist->nreg) return 0;
    return rlist->rinfo[rindx].nread;
}

/** Count the number of times a register is written */
int reginfolist_countwrites(reginfolist *rlist, int rindx) {
    if (rindx>=rlist->nreg) return 0;
    return rlist->rinfo[rindx].nwrite;
}

/** Indicate a register is duplicated */
void reginfolist_duplicate(reginfolist *rlist, int rindx) {
    if (rindx>=rlist->nreg) return;
    rlist->rinfo[rindx].ndup++;
}

/** Repairs duplicate registers when the original is overwritten */
void reginfolist_unduplicate(reginfolist *rlist, int rindx) {
    regcontents srccontents; // Content type of the source
    indx srcindx; // Indx of the source
    
    reginfolist_contents(rlist, rindx, &srccontents, &srcindx);
    value srctype = reginfolist_type(rlist, rindx);
    regtypeinfo srctypeinfo = reginfolist_typeinfo(rlist, rindx);
    
    for (registerindx i=0; i<rlist->nreg; i++) { // Look for registers
        if (i==rindx) continue; // Skip over the tautological case
        
        regcontents icontents;
        indx iindx;
        
        reginfolist_contents(rlist, i, &icontents, &iindx);
        if (icontents==REG_REGISTER &&
            iindx==rindx) {
            // Move the contents from the source register into the duplicate reg i
            int nread = rlist->rinfo[i].nread, nwrite = rlist->rinfo[i].nwrite;
            instructionindx src;
            reginfolist_source(rlist, i, &src); // The write instruction should remain the duplicating instruction
            
            reginfolist_write(rlist, src, i, srccontents, srcindx);
            if (!MORPHO_ISNIL(srctype)) reginfolist_settypeinfo(rlist, i, srctype, srctypeinfo);
            
            rlist->rinfo[i].nread=nread;
            rlist->rinfo[i].nwrite=nwrite;
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
            if (rlist->rinfo[i].typeinfo==REGTYPE_EXACT) printf("=:");
            if (rlist->rinfo[i].typeinfo==REGTYPE_SUBTYPE) printf("<:");
            morpho_printvalue(NULL, rlist->rinfo[i].type);
        }
        
        printf(" r:%i", rlist->rinfo[i].nread); // Reads
        printf(" w:%i", rlist->rinfo[i].nwrite); // Writes
        
        if (rlist->rinfo[i].ndup) printf(" d:%i", rlist->rinfo[i].ndup); // Duplication
        
        if (rlist->rinfo[i].contents!=REG_EMPTY) { // Who wrote it?
            printf(" i:%i", (int) rlist->rinfo[i].iindx);
        }
        
        printf("\n");
    }
}
