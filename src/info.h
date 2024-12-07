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
    value type; /** Type information if known  */
    value val; /** Value from constant */
    dictionary src; /** A dictionary of instruction indices that store to this global */
    dictionary read; /** A dictionary of instruction indices that read from this global */
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

void globalinfolist_store(globalinfolist *glist, int gindx, instructionindx src);
void globalinfolist_removestore(globalinfolist *glist, int gindx, instructionindx src);
unsigned int globalinfolist_countstore(globalinfolist *glist, int gindx);

void globalinfolist_read(globalinfolist *glist, int gindx, instructionindx src);
void globalinfolist_removeread(globalinfolist *glist, int gindx, instructionindx src);
unsigned int globalinfolist_countread(globalinfolist *glist, int gindx);

void globalinfolist_show(globalinfolist *glist);

#endif
