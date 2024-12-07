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
    int nread; /** Number of times read */
    value type; /** Type information if known  */
    value val; /** Value from constant */
} glblinfo;

typedef struct {
    int nglobals;
    glblinfo *list;
} globalinfolist;

bool globalinfolist_init(globalinfolist *glist, int n);
void globalinfolist_clear(globalinfolist *glist);

void globalinfolist_read(globalinfolist *glist, int gindx);
void globalinfolist_writevalue(globalinfolist *glist, int gindx);
void globalinfolist_writeconstant(globalinfolist *glist, int gindx, value konst);

bool globalinfolist_isconstant(globalinfolist *glist, int gindx, value *konst);

void globalinfolist_show(globalinfolist *glist);

#endif
