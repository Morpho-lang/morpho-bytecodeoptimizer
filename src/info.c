/** @file info.c
 *  @author T J Atherton
 *
 *  @brief Track information about data structures
*/

#include "info.h"

DEFINE_VARRAY(methodinfoentry, methodinfoentry)

/* **********************************************************************
 * Globals
 * ********************************************************************** */

void globalinfo_init(glblinfo *info) {
    info->contents=GLOBAL_EMPTY;
    info->val=MORPHO_NIL;
    info->nread=0;
    info->nstore=0;
    varray_valueinit(&info->typeassignments);
    info->type=MORPHO_NIL;
}

void globalinfo_clear(glblinfo *info) {
    varray_valueclear(&info->typeassignments);
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

/** Sets a possible type assignment to a global */
void globalinfolist_settype(globalinfolist *glist, int gindx, value type) {
    varray_value *assignments = &glist->list[gindx].typeassignments;
    
    for (int i=0; i<assignments->count; i++) if (MORPHO_ISEQUAL(assignments->data[i], type)) return;
    
    varray_valuewrite(assignments, type);
}

/** Gets the type of a global */
value globalinfolist_type(globalinfolist *glist, int gindx) {
    return glist->list[gindx].type;
}

/** Gets the type of a global */
void globalinfolist_computetype(globalinfolist *glist, int gindx) {
    varray_value *assignments = &glist->list[gindx].typeassignments;
    
    if (assignments->count>1 || assignments->count==0) glist->list[gindx].type=MORPHO_NIL;
    else glist->list[gindx].type=assignments->data[0];
}

/** Adds a store instruction to a global */
void globalinfolist_store(globalinfolist *glist, int gindx) {
    glist->list[gindx].nstore++;
}

/** Count number of instructions that store to this global */
int globalinfolist_countstore(globalinfolist *glist, int gindx) {
    return glist->list[gindx].nstore;
}

/** Adds a read instruction to a global */
void globalinfolist_read(globalinfolist *glist, int gindx) {
    glist->list[gindx].nread++;
}

/** Count number of instructions that read from this global */
int globalinfolist_countread(globalinfolist *glist, int gindx) {
    return glist->list[gindx].nread;
}

/** Rest global information before an optimization pass */
void globalinfolist_startpass(globalinfolist *glist) {
    for (int i=0; i<glist->nglobals; i++) {
        globalinfolist_computetype(glist, i);
        glist->list[i].contents=GLOBAL_EMPTY;
        glist->list[i].val=MORPHO_NIL;
        glist->list[i].nread=0;
        glist->list[i].nstore=0;
        glist->list[i].typeassignments.count=0; // Clear assignments from previous pass
    }
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
        printf("r: %i w: %i ", glist->list[i].nread, glist->list[i].nstore);
        
        value type = globalinfolist_type(glist, i);
        if (!MORPHO_ISNIL(type)) morpho_printvalue(NULL, type);
        printf("\n");
    }
}

/* **********************************************************************
 * Methods/functions
 * ********************************************************************** */

/** Initializes method metadata. */
static void methodinfo_init(mthdinfo *info) {
    info->nowners=0;
    info->flags=METHODINFO_NONE;
}

/** Finds the array index for a function's metadata. */
static int methodinfolist_find(methodinfolist *mlist, objectfunction *method) {
    value indx;
    if (dictionary_get(&mlist->indx, MORPHO_OBJECT(method), &indx) &&
        MORPHO_ISINTEGER(indx)) return MORPHO_GETINTEGERVALUE(indx);

    return -1;
}

/** Retrieves metadata for a function if it exists. */
static mthdinfo *methodinfolist_get(methodinfolist *mlist, objectfunction *method) {
    int indx = methodinfolist_find(mlist, method);

    if (indx<0) return NULL;

    return &mlist->list.data[indx].info;
}

/** Adds a new metadata record for a function. */
static mthdinfo *methodinfolist_add(methodinfolist *mlist, objectfunction *method) {
    methodinfoentry entry = { .method=method};
    methodinfo_init(&entry.info);

    if (!varray_methodinfoentryadd(&mlist->list, &entry, 1)) return NULL;

    dictionary_insert(&mlist->indx,MORPHO_OBJECT(method), MORPHO_INTEGER(mlist->list.count-1));

    return &mlist->list.data[mlist->list.count-1].info;
}

/** Retrieves metadata for a function, creating it on demand. */
static mthdinfo *methodinfolist_getoradd(methodinfolist *mlist, objectfunction *method) {
    mthdinfo *info = methodinfolist_get(mlist, method);
    if (!info) info = methodinfolist_add(mlist, method);
    return info;
}

/** Initializes a method metadata list. */
bool methodinfolist_init(methodinfolist *mlist) {
    varray_methodinfoentryinit(&mlist->list);
    dictionary_init(&mlist->indx);
    return true;
}

/** Clears a method metadata list. */
void methodinfolist_clear(methodinfolist *mlist) {
    varray_methodinfoentryclear(&mlist->list);
    dictionary_clear(&mlist->indx);
}

/** Increments the method-owner count for a function. */
bool methodinfolist_incrementowners(methodinfolist *mlist, objectfunction *method) {
    mthdinfo *info = methodinfolist_getoradd(mlist, method);
    if (!info) return false;

    info->nowners++;
    return true;
}

/** Returns the number of owning method entries for a function. */
int methodinfolist_countowners(methodinfolist *mlist, objectfunction *method) {
    mthdinfo *info = methodinfolist_get(mlist, method);
    return info ? info->nowners : 0;
}

/** Sets metadata flags on a function record. */
bool methodinfolist_setflags(methodinfolist *mlist, objectfunction *method, unsigned int flags) {
    mthdinfo *info = methodinfolist_getoradd(mlist, method);
    if (!info) return false;

    info->flags |= flags;
    return true;
}

/** Checks whether a function record has all requested flags set. */
bool methodinfolist_hasflags(methodinfolist *mlist, objectfunction *method, unsigned int flags) {
    mthdinfo *info = methodinfolist_get(mlist, method);

    return (info && ((info->flags & flags)==flags));
}
