/** @file reginfo.h
 *  @author T J Atherton
 *
 *  @brief Data structure to track register status
*/

#ifndef reginfo_h
#define reginfo_h

#include "morphocore.h"

/* **********************************************************************
 * Reginfo
 * ********************************************************************** */

/** Enumerated type recording what is known about the contents of a register. */
typedef enum {
    REG_NOFACT,     /** No semantic fact is currently known for this register */
    REG_VALUE,      /** Unknown value */
    REG_TYPEDVALUE, /** Unknown value with known type information */
    REG_CONSTANT,   /** Contents came from the constant table */
    REG_GLOBAL,     /** Contents came from a global */
    REG_UPVALUE,    /** Contents came from an upvalue */
} regcontents;

/** Enumerated type recording local usage of the current register fact. */
typedef enum {
    REGUSE_NONE,
    REGUSE_READ,
    REGUSE_WRITTEN,
    REGUSE_READWRITTEN,
} regusage;

/** Enumerated type recording the precision of type information */
typedef enum {
    REGTYPE_UNKNOWN, /** No type information */
    REGTYPE_EXACT,   /** Register definitely has this type */
    REGTYPE_SUBTYPE /** Register has this type or a child type */
} regtypeinfo;

/** Record information about each register */
typedef struct {
    regcontents contents; /** Semantic knowledge about the value */
    indx indx; /** Index associated with the contents, if any */

    regusage usage; /** Local usage summary for the current fact */
    instructionindx iindx; /** Instruction that last wrote this fact */

    value type; /** Type information if known */
    regtypeinfo typeinfo; /** Precision of type information */

    bool hasalias; /** True if this fact is currently known to alias another register */
    registerindx alias; /** Register that this fact currently aliases */
} reginfo;

typedef struct {
    int nreg;
    reginfo *rinfo;
} reginfolist;

/* **********************************************************************
 * Interface
 * ********************************************************************** */

void reginfolist_init(reginfolist *rlist, int nreg);
void reginfolist_clear(reginfolist *rlist);
void reginfolist_wipe(reginfolist *rlist, int nreg);
bool reginfolist_copy(reginfolist *src, reginfolist *dest);
bool reginfo_equal(reginfo *a, reginfo *b);
bool reginfolist_equal(reginfolist *a, reginfolist *b);
void reginfo_join(reginfo *dest, reginfo *src);
void reginfo_weaken(reginfo *info);
void reginfolist_write(reginfolist *rlist, instructionindx iindx, int rindx, regcontents contents, indx indx);
void reginfolist_copyregister(reginfolist *rlist, instructionindx iindx, int dest, int src);
void reginfolist_settype(reginfolist *rlist, int rindx, value type);
void reginfolist_settypeinfo(reginfolist *rlist, int rindx, value type, regtypeinfo info);
void reginfolist_incread(reginfolist *rlist, int rindx);
void reginfolist_incwrite(reginfolist *rlist, int rindx);

value reginfolist_type(reginfolist *rlist, int rindx);
regtypeinfo reginfolist_typeinfo(reginfolist *rlist, int rindx);
bool reginfolist_contents(reginfolist *rlist, int rindx, regcontents *contents, indx *indx);
regcontents reginfolist_regcontents(reginfolist *rlist, int rindx);
bool reginfolist_source(reginfolist *rlist, int rindx, instructionindx *iindx);
int reginfolist_countuses(reginfolist *rlist, int rindx);
int reginfolist_countwrites(reginfolist *rlist, int rindx);
bool reginfolist_alias(reginfolist *rlist, int rindx, registerindx *alias);

void reginfolist_invalidate(reginfolist *rlist, regcontents contents, indx indx);
void reginfolist_generalizecontent(reginfolist *rlist, regcontents contents);

void reginfolist_show(reginfolist *rlist);

#endif
