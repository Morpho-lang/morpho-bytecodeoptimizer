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
    indx indx; /** An index for any contents */
    value type; /** Type information if known  */
    int nread; /** Number of times read */
} glblinfo;

typedef struct {
    int nglobals;
    glblinfo *list;
} globalinfolist;

bool globalinfolist_init(int n, globalinfolist *glist);
void globalinfolist_clear(globalinfolist *glist);

void globalinfolist_read(globalinfolist *glist, int gindx);
void globalinfolist_write(globalinfolist *glist, int gindx, globalcontents contents, value type);

bool global_isconstant(globalinfolist *glist, int gindx, indx *kindx);

#endif
