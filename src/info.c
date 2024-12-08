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
    info->val=MORPHO_NIL;
    dictionary_init(&info->read);
    dictionary_init(&info->src);
}

void globalinfo_clear(glblinfo *info) {
    dictionary_clear(&info->read);
    dictionary_clear(&info->src);
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
    if (glist->list) {
        for (int i=0; i<glist->nglobals; i++) globalinfo_clear(glist->list+i);
        MORPHO_FREE(glist->list);
    }
    glist->list=NULL;
    glist->nglobals=0;
}

/** Sets the contents of a global to be a value */
void globalinfolist_setvalue(globalinfolist *glist, int gindx) {
    glist->list[gindx].contents=GLOBAL_VALUE;
}

/** Sets the contents of a global to be a constant */
void globalinfolist_setconstant(globalinfolist *glist, int gindx, value konst) {
    if (glist->list[gindx].contents==GLOBAL_EMPTY ||
               (glist->list[gindx].contents==GLOBAL_CONSTANT && // Ensure only one distinct constant is written
                MORPHO_ISEQUAL(glist->list[gindx].val, konst))) {
        glist->list[gindx].contents=GLOBAL_CONSTANT;
        glist->list[gindx].val=konst;
    } else globalinfolist_setvalue(glist, gindx);
}

/** Check if a global is constant */
bool globalinfolist_isconstant(globalinfolist *glist, int gindx, value *konst) {
    if (glist->list[gindx].contents==GLOBAL_CONSTANT) {
        *konst = glist->list[gindx].val;
        return true;
    }
    return false;
}

/** Adds a store instruction to a global */
void globalinfolist_store(globalinfolist *glist, int gindx, instructionindx src, value type) {
    dictionary_insert(&glist->list[gindx].src, MORPHO_INTEGER(src), type);
}

/** Gets the type of a global */
value globalinfolist_type(globalinfolist *glist, int gindx) {
    dictionary *dict = &glist->list[gindx].src;
    value type = MORPHO_NIL;
    
    for (int i=0; i<dict->capacity; i++) {
        if (!MORPHO_ISNIL(dict->contents[i].key)) {
            value thistype = dict->contents[i].val;
            if (MORPHO_ISNIL(thistype)) { // An unknown type always wins
                return thistype;
            } else if (MORPHO_ISNIL(type)) {
                type = thistype;
            } else if (!MORPHO_ISSAME(type, thistype)) {
                return MORPHO_NIL; // If there's a conflict, type is unknown
            }
        }
    }
    
    return type;
}


/** Removes a store instruction to a global */
void globalinfolist_removestore(globalinfolist *glist, int gindx, instructionindx src) {
    dictionary_remove(&glist->list[gindx].src, MORPHO_INTEGER(src));
}

/** Count number of instructions that store to this global */
unsigned int globalinfolist_countstore(globalinfolist *glist, int gindx) {
    return glist->list[gindx].src.count;
}

/** Adds a read instruction to a global */
void globalinfolist_read(globalinfolist *glist, int gindx, instructionindx src) {
    dictionary_insert(&glist->list[gindx].read, MORPHO_INTEGER(src), MORPHO_NIL);
}

/** Removes a store instruction from a global */
void globalinfolist_removeread(globalinfolist *glist, int gindx, instructionindx src) {
    dictionary_remove(&glist->list[gindx].read, MORPHO_INTEGER(src));
}

/** Count number of instructions that read from this global */
unsigned int globalinfolist_countread(globalinfolist *glist, int gindx) {
    return glist->list[gindx].read.count;
}

void _showdict(char *label, dictionary *dict) {
    printf("(%s ", label);
    for (unsigned int i=0; i<dict->capacity; i++) {
        if (!MORPHO_ISNIL(dict->contents[i].key)) printf("%i ", MORPHO_GETINTEGERVALUE(dict->contents[i].key));
    }
    printf(") ");
}

/** Show the global info list */
void globalinfolist_show(globalinfolist *glist) {
    printf("Globals:\n");
    for (int i=0; i<glist->nglobals; i++) {
        printf("|\tg%u : ", i);
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
        _showdict("r:", &glist->list[i].read);
        _showdict("w:", &glist->list[i].src);
        
        value type = globalinfolist_type(glist, i);
        if (!MORPHO_ISNIL(type)) morpho_printvalue(NULL, type);
        printf("\n");
    }
}
