/** @file info.c
 *  @author T J Atherton
 *
 *  @brief Track information about data structures
*/

#include "info.h"

/* **********************************************************************
 * Globals
 * ********************************************************************** */

void globalinfo_init(glblinfo *info) {
    info->contents=GLOBAL_EMPTY;
    info->type=MORPHO_NIL;
    info->val=MORPHO_NIL;
    info->nread=0;
}

/** Allocate and initialize a global info list */
bool globalinfolist_init(globalinfolist *glist, int n) {
    glist->nglobals=n;
    glist->list=MORPHO_MALLOC(sizeof(glblinfo)*n);
    
    if (glist->list) {
        for (int i=0; i<n; i++) globalinfo_init(glist->list+i);
    }
    return glist->list;
}

/** Clears a globalinfolist */
void globalinfolist_clear(globalinfolist *glist) {
    MORPHO_FREE(glist->list);
    glist->list=NULL;
    glist->nglobals=0;
}

/** Indicate a read from a global */
void globalinfolist_read(globalinfolist *glist, int gindx) {
    glist->list[gindx].nread++;
}

/** Write a value to a global */
void globalinfolist_writevalue(globalinfolist *glist, int gindx) {
    glist->list[gindx].contents=GLOBAL_VALUE;
}

/** Write contents to a global */
void globalinfolist_writeconstant(globalinfolist *glist, int gindx, value konst) {
    if (glist->list[gindx].contents==GLOBAL_EMPTY ||
               (glist->list[gindx].contents==GLOBAL_CONSTANT && // Ensure only one distinct constant is written
                MORPHO_ISEQUAL(glist->list[gindx].val, konst))) {
        glist->list[gindx].contents=GLOBAL_CONSTANT;
        glist->list[gindx].val=konst;
    } else globalinfolist_writevalue(glist, gindx);
}

/** Check if a global is constant */
bool globalinfolist_isconstant(globalinfolist *glist, int gindx, value *konst) {
    if (glist->list[gindx].contents==GLOBAL_CONSTANT) {
        *konst = glist->list[gindx].val;
        return true;
    }
    return false;
}

/** Show the global info list */
void globalinfolist_show(globalinfolist *glist) {
    printf("Globals:\n");
    for (int i=0; i<glist->nglobals; i++) {
        printf("|\tr%u : ", i);
        switch(glist->list[i].contents) {
            case GLOBAL_EMPTY: break;
            case GLOBAL_CONSTANT:
                printf("c [");
                morpho_printvalue(NULL, glist->list[i].val);
                printf("] ");
                break;
            case GLOBAL_VALUE:
                printf("v ");
        }
        printf(" u:%i ", glist->list[i].nread);
        if (!MORPHO_ISNIL(glist->list[i].type)) morpho_printvalue(NULL, glist->list[i].type);
        printf("\n");
    }
    
}
