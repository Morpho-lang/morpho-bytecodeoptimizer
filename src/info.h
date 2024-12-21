/** @file info.h
 *  @author T J Atherton
 *
 *  @brief Track information about data structures
*/

#ifndef info_h
#define info_h

#include "morphocore.h"

/* **********************************************************************
 * Information about globals
 * ********************************************************************** */

/** Enumerated type recording where the contents of a global came from */
typedef enum {
    GLOBAL_EMPTY,    /** Empty global */
    GLOBAL_CONSTANT, /** Contents came from the constant table */
    GLOBAL_VALUE,    /** A value */
} globalcontents;

typedef struct {
    globalcontents contents; /** What the global contains */
    value val; /** Value from constant */
    int nstore; /** Number of times global is stored to */
    int nread; /** Number of times global is read from */
    varray_value typeassignments; /** A dictionary of types stored to this global in the current pass */
    value type;
} glblinfo;

typedef struct {
    int nglobals;
    glblinfo *list;
} globalinfolist;

bool globalinfolist_init(globalinfolist *glist, int n);
void globalinfolist_clear(globalinfolist *glist);

void globalinfolist_setvalue(globalinfolist *glist, int gindx);
void globalinfolist_setconstant(globalinfolist *glist, int gindx, value konst);

bool globalinfolist_isconstant(globalinfolist *glist, int gindx, value *konst);

void globalinfolist_settype(globalinfolist *glist, int gindx, value type);
value globalinfolist_type(globalinfolist *glist, int gindx);

void globalinfolist_store(globalinfolist *glist, int gindx);
int globalinfolist_countstore(globalinfolist *glist, int gindx);

void globalinfolist_read(globalinfolist *glist, int gindx);
int globalinfolist_countread(globalinfolist *glist, int gindx);

void globalinfolist_startpass(globalinfolist *glist);

void globalinfolist_show(globalinfolist *glist);

#endif
