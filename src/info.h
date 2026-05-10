/** @file info.h
 *  @author T J Atherton
 *
 *  @brief Track information about data structures
*/

#ifndef info_h
#define info_h

#include "morphocore.h"
#include "reginfo.h"

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
    regtypeinfo typeassignmentinfo; /** Conservatively merged precision for current-pass type assignments */
    value type;
    regtypeinfo typeinfo;
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

void globalinfolist_settype(globalinfolist *glist, int gindx, value type, regtypeinfo info);
value globalinfolist_type(globalinfolist *glist, int gindx);
regtypeinfo globalinfolist_typeinfo(globalinfolist *glist, int gindx);

void globalinfolist_store(globalinfolist *glist, int gindx);
int globalinfolist_countstore(globalinfolist *glist, int gindx);

void globalinfolist_read(globalinfolist *glist, int gindx);
int globalinfolist_countread(globalinfolist *glist, int gindx);

void globalinfolist_startpass(globalinfolist *glist);

void globalinfolist_show(globalinfolist *glist);

/* **********************************************************************
 * Information about classes
 * ********************************************************************** */

typedef struct {
    dictionary constructed;
} classinfolist;

bool classinfolist_init(classinfolist *clist);
void classinfolist_clear(classinfolist *clist);
void classinfolist_startpass(classinfolist *clist);

bool classinfolist_incrementconstructed(classinfolist *clist, objectclass *klass);
int classinfolist_countconstructed(classinfolist *clist, objectclass *klass);

/* **********************************************************************
 * Information about methods/functions
 * ********************************************************************** */

typedef enum {
    FUNCTIONINFO_NONE              = 0x0,
    FUNCTIONINFO_USESELF_DISPATCH  = 0x1,
    FUNCTIONINFO_RECURSIVE         = 0x2,
    FUNCTIONINFO_ESCAPES           = 0x4
} functioninfoflags;

typedef struct {
    int nowners;
    int nblocks;
    int ninstructions;
    unsigned int flags;
} functioninfo;

typedef struct {
    objectfunction *function;
    functioninfo info;
} functioninfoentry;

DECLARE_VARRAY(functioninfoentry, functioninfoentry)

typedef struct {
    varray_functioninfoentry list;
    dictionary indx;
} functioninfolist;

bool functioninfolist_init(functioninfolist *flist);
void functioninfolist_clear(functioninfolist *flist);

bool functioninfolist_incrementowners(functioninfolist *flist, objectfunction *function);
int functioninfolist_countowners(functioninfolist *flist, objectfunction *function);

void functioninfolist_startpass(functioninfolist *flist);
bool functioninfolist_addblock(functioninfolist *flist, objectfunction *function, int ninstructions);
int functioninfolist_countblocks(functioninfolist *flist, objectfunction *function);
int functioninfolist_countinstructions(functioninfolist *flist, objectfunction *function);

bool functioninfolist_setflags(functioninfolist *flist, objectfunction *function, unsigned int flags);
bool functioninfolist_hasflags(functioninfolist *flist, objectfunction *function, unsigned int flags);

#endif
