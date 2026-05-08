/** @file info.c
 *  @author T J Atherton
 *
 *  @brief Track information about data structures
*/

#include "info.h"

DEFINE_VARRAY(functioninfoentry, functioninfoentry)
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
 * Classes
 * ********************************************************************** */

/** Initializes a class metadata list. */
bool classinfolist_init(classinfolist *clist) {
    dictionary_init(&clist->constructed);
    return true;
}

/** Clears a class metadata list. */
void classinfolist_clear(classinfolist *clist) {
    dictionary_clear(&clist->constructed);
}

/** Resets per-pass class metadata. */
void classinfolist_startpass(classinfolist *clist) {
    dictionary_clear(&clist->constructed);
    dictionary_init(&clist->constructed);
}

/** Increments the construction count for a class. */
bool classinfolist_incrementconstructed(classinfolist *clist, objectclass *klass) {
    value key = MORPHO_OBJECT(klass), count = MORPHO_INTEGER(0);
    if (dictionary_get(&clist->constructed, key, &count) && MORPHO_ISINTEGER(count)) {
        count = MORPHO_INTEGER(MORPHO_GETINTEGERVALUE(count)+1);
    } else count = MORPHO_INTEGER(1);
    return dictionary_insert(&clist->constructed, key, count);
}

/** Returns the number of constructor calls seen for a class. */
int classinfolist_countconstructed(classinfolist *clist, objectclass *klass) {
    value count;
    if (dictionary_get(&clist->constructed, MORPHO_OBJECT(klass), &count) && MORPHO_ISINTEGER(count)) {
        return MORPHO_GETINTEGERVALUE(count);
    }
    return 0;
}

/* **********************************************************************
 * Methods/functions
 * ********************************************************************** */

/** Initializes function metadata. */
static void functioninfo_init(functioninfo *info) {
    info->nowners=0;
    info->flags=FUNCTIONINFO_NONE;
}

/** Finds the array index for a function's metadata. */
static int functioninfolist_find(functioninfolist *flist, objectfunction *function) {
    value indx;
    if (dictionary_get(&flist->indx, MORPHO_OBJECT(function), &indx) &&
        MORPHO_ISINTEGER(indx)) return MORPHO_GETINTEGERVALUE(indx);

    return -1;
}

/** Retrieves metadata for a function if it exists. */
static functioninfo *functioninfolist_get(functioninfolist *flist, objectfunction *function) {
    int indx = functioninfolist_find(flist, function);

    if (indx<0) return NULL;

    return &flist->list.data[indx].info;
}

/** Adds a new metadata record for a function. */
static functioninfo *functioninfolist_add(functioninfolist *flist, objectfunction *function) {
    functioninfoentry entry = { .function=function};
    functioninfo_init(&entry.info);

    if (!varray_functioninfoentryadd(&flist->list, &entry, 1)) return NULL;

    dictionary_insert(&flist->indx,MORPHO_OBJECT(function), MORPHO_INTEGER(flist->list.count-1));

    return &flist->list.data[flist->list.count-1].info;
}

/** Retrieves metadata for a function, creating it on demand. */
static functioninfo *functioninfolist_getoradd(functioninfolist *flist, objectfunction *function) {
    functioninfo *info = functioninfolist_get(flist, function);
    if (!info) info = functioninfolist_add(flist, function);
    return info;
}

/** Initializes a function metadata list. */
bool functioninfolist_init(functioninfolist *flist) {
    varray_functioninfoentryinit(&flist->list);
    dictionary_init(&flist->indx);
    return true;
}

/** Clears a function metadata list. */
void functioninfolist_clear(functioninfolist *flist) {
    varray_functioninfoentryclear(&flist->list);
    dictionary_clear(&flist->indx);
}

/** Increments the owner count for a function. */
bool functioninfolist_incrementowners(functioninfolist *flist, objectfunction *function) {
    functioninfo *info = functioninfolist_getoradd(flist, function);
    if (!info) return false;

    info->nowners++;
    return true;
}

/** Returns the number of owning entries for a function. */
int functioninfolist_countowners(functioninfolist *flist, objectfunction *function) {
    functioninfo *info = functioninfolist_get(flist, function);
    return info ? info->nowners : 0;
}

/** Sets metadata flags on a function record. */
bool functioninfolist_setflags(functioninfolist *flist, objectfunction *function, unsigned int flags) {
    functioninfo *info = functioninfolist_getoradd(flist, function);
    if (!info) return false;

    info->flags |= flags;
    return true;
}

/** Checks whether a function record has all requested flags set. */
bool functioninfolist_hasflags(functioninfolist *flist, objectfunction *function, unsigned int flags) {
    functioninfo *info = functioninfolist_get(flist, function);

    return (info && ((info->flags & flags)==flags));
}
