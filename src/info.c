/** @file info.c
 *  @author T J Atherton
 *
 *  @brief Track information about data structures
*/

#include "info.h"

/* **********************************************************************
 * Globals
 * ********************************************************************** */

bool globalinfo_init(glblinfo *info) {
    info->nread=0;
    info->type=MORPHO_NIL;
    info->contents=GLOBAL_EMPTY;
    info->indx=0;
}

/** Allocate and initialize a global info list */
bool globalinfolist_init(int n, globalinfolist *glist) {
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
void globalinfolist_writeconstant(globalinfolist *glist, int gindx, indx kindx) {
    if (glist->list[gindx].contents==GLOBAL_EMPTY) {
        glist->list[gindx].contents=GLOBAL_CONSTANT;
        glist->list[gindx].indx=kindx;
    } else globalinfolist_writevalue(glist, gindx);
}

/** Check if a global is constant */
bool globalinfolist_isconstant(globalinfolist *glist, int gindx, indx *kindx) {
    if (glist->list[gindx].contents==GLOBAL_CONSTANT) {
        *kindx = glist->list[gindx].indx;
        return true;
    }
    return false;
}
