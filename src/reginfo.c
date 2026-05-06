/** @file reginfo.c
 *  @author T J Atherton
 *
 *  @brief Data structure to track register status
*/

#include "morphocore.h"
#include "reginfo.h"
#include "cfgraph.h"

static bool reginfo_hasindexedcontents(regcontents contents);
static void reginfo_clearsource(reginfo *info);
static void reginfo_clearalias(reginfo *info);
static void reginfo_generalize(reginfo *info);
static void reginfo_normalize(reginfo *info);
static void regusage_merge(regusage *dest, regusage src);
static bool regusage_hasread(regusage usage);
static bool regusage_haswrite(regusage usage);
static void reginfolist_invalidatealiases(reginfolist *rlist, registerindx rindx);

/** Initializes a reginfo structure */
void reginfo_init(reginfo *info) {
    info->contents=REG_NOFACT;
    info->indx=0;
    info->usage=REGUSE_NONE;
    info->iindx=INSTRUCTIONINDX_EMPTY;
    info->type=MORPHO_NIL;
    info->typeinfo=REGTYPE_UNKNOWN;
    info->hasalias=false;
    info->alias=0;
}

/** Initialize a reginfo list */
void reginfolist_init(reginfolist *rlist, int nreg) {
    rlist->nreg=nreg;
    rlist->rinfo=MORPHO_MALLOC(sizeof(reginfo)*nreg);
    if (rlist->rinfo) for (int i=0; i<nreg; i++) reginfo_init(&rlist->rinfo[i]);
}

/** Clears a reginfo list */
void reginfolist_clear(reginfolist *rlist) {
    if (rlist->rinfo) MORPHO_FREE(rlist->rinfo);
}

/** Wipes a reginfo list */
void reginfolist_wipe(reginfolist *rlist, int nreg) {
    rlist->nreg=nreg;
    for (int i=0; i<nreg; i++) reginfo_init(&rlist->rinfo[i]);
}

/** Copys a reginfo list */
bool reginfolist_copy(reginfolist *src, reginfolist *dest) {
    if (src->nreg>dest->nreg) return false;
    for (int i=0; i<src->nreg; i++) dest->rinfo[i]=src->rinfo[i];
    return true;
}

/** Checks if two reginfo records represent the same dataflow fact. */
bool reginfo_equal(reginfo *a, reginfo *b) {
    return (a->contents==b->contents &&
            a->usage==b->usage &&
            a->typeinfo==b->typeinfo &&
            a->iindx==b->iindx &&
            a->hasalias==b->hasalias &&
            (!a->hasalias || a->alias==b->alias) &&
            MORPHO_ISEQUAL(a->type, b->type) &&
            (!reginfo_hasindexedcontents(a->contents) || a->indx==b->indx));
}

/** Checks if two reginfo lists represent the same dataflow fact. */
bool reginfolist_equal(reginfolist *a, reginfolist *b) {
    if (a->nreg!=b->nreg) return false;
    for (int i=0; i<a->nreg; i++) {
        if (!reginfo_equal(&a->rinfo[i], &b->rinfo[i])) return false;
    }
    return true;
}

static bool reginfo_hasindexedcontents(regcontents contents) {
    return (contents==REG_GLOBAL ||
            contents==REG_UPVALUE ||
            contents==REG_CONSTANT);
}

static void reginfo_clearsource(reginfo *info) {
    info->indx=0;
    info->iindx=INSTRUCTIONINDX_EMPTY;
}

static void reginfo_clearalias(reginfo *info) {
    info->hasalias=false;
    info->alias=0;
}

static void reginfo_generalize(reginfo *info) {
    info->contents = (MORPHO_ISNIL(info->type) ? REG_VALUE : REG_TYPEDVALUE);
    reginfo_clearsource(info);
    reginfo_clearalias(info);
}

static void reginfo_normalize(reginfo *info) {
    if (MORPHO_ISNIL(info->type)) info->typeinfo=REGTYPE_UNKNOWN;
    if (info->contents==REG_TYPEDVALUE && MORPHO_ISNIL(info->type)) info->contents=REG_VALUE;

    if (info->contents==REG_NOFACT || info->contents==REG_TYPEDVALUE || info->contents==REG_VALUE) {
        reginfo_clearsource(info);
    }

    if (info->contents==REG_NOFACT) {
        reginfo_clearalias(info);
        info->usage=REGUSE_NONE;
    }
}

static void regusage_merge(regusage *dest, regusage src) {
    if (*dest==src) return;
    if (*dest==REGUSE_NONE) {
        *dest=src;
    } else if (src==REGUSE_NONE) {
        return;
    } else if (*dest!=REGUSE_READWRITTEN) {
        *dest=REGUSE_READWRITTEN;
    }
}

static bool regusage_hasread(regusage usage) {
    return (usage==REGUSE_READ || usage==REGUSE_READWRITTEN);
}

static bool regusage_haswrite(regusage usage) {
    return (usage==REGUSE_WRITTEN || usage==REGUSE_READWRITTEN);
}

/** Joins register information from one predecessor into another. */
void reginfo_join(reginfo *dest, reginfo *src) {
    reginfo joined = *dest;
    reginfo incoming = *src;
    bool lostidentity=false;

    reginfo_normalize(&joined);
    reginfo_normalize(&incoming);

    if (joined.contents==REG_NOFACT) {
        if (incoming.contents!=REG_NOFACT) lostidentity=true;
    } else if (incoming.contents==REG_NOFACT) {
        lostidentity=true;
    } else if (joined.contents!=incoming.contents ||
               (reginfo_hasindexedcontents(joined.contents) && joined.indx!=incoming.indx)) {
        lostidentity=true;
    } else if (joined.contents==REG_TYPEDVALUE || joined.contents==REG_VALUE) {
        reginfo_clearsource(&joined);
    } else if (joined.iindx!=incoming.iindx) {
        joined.iindx=INSTRUCTIONINDX_EMPTY;
    }

    if (MORPHO_ISNIL(joined.type) ||
        MORPHO_ISNIL(incoming.type) ||
        !MORPHO_ISEQUAL(joined.type, incoming.type)) {
        joined.type=MORPHO_NIL;
        joined.typeinfo=REGTYPE_UNKNOWN;
    } else if (incoming.typeinfo>joined.typeinfo) {
        joined.typeinfo=incoming.typeinfo;
    }

    if (!(joined.hasalias && incoming.hasalias && joined.alias==incoming.alias)) {
        reginfo_clearalias(&joined);
    }

    regusage_merge(&joined.usage, incoming.usage);

    if (lostidentity) reginfo_generalize(&joined);

    reginfo_normalize(&joined);
    *dest=joined;
}

/** Adds one to the read summary for register i */
void reginfolist_incread(reginfolist *rlist, int rindx) {
    if (rindx>=rlist->nreg) return;
    regusage_merge(&rlist->rinfo[rindx].usage, REGUSE_READ);
}

/** Adds one to the write summary for register i */
void reginfolist_incwrite(reginfolist *rlist, int rindx) {
    if (rindx>=rlist->nreg) return;
    regusage_merge(&rlist->rinfo[rindx].usage, REGUSE_WRITTEN);
}

static void reginfolist_invalidatealiases(reginfolist *rlist, registerindx rindx) {
    for (registerindx i=0; i<rlist->nreg; i++) {
        if (rlist->rinfo[i].hasalias && rlist->rinfo[i].alias==rindx) {
            reginfo_clearalias(&rlist->rinfo[i]);
        }
    }
}

/** Writes a value to a register */
void reginfolist_write(reginfolist *rlist, instructionindx iindx, int rindx, regcontents contents, indx indx) {
    if (rindx>=rlist->nreg) return;

    reginfolist_invalidatealiases(rlist, rindx);

    rlist->rinfo[rindx].contents=contents;
    rlist->rinfo[rindx].indx=indx;
    rlist->rinfo[rindx].usage=REGUSE_NONE;
    rlist->rinfo[rindx].iindx=iindx;
    rlist->rinfo[rindx].type=MORPHO_NIL;
    rlist->rinfo[rindx].typeinfo=REGTYPE_UNKNOWN;
    reginfo_clearalias(&rlist->rinfo[rindx]);

    reginfolist_incwrite(rlist, rindx);
}

/** Copies one register fact into another and records an alias relation. */
void reginfolist_copyregister(reginfolist *rlist, instructionindx iindx, int dest, int src) {
    if (dest>=rlist->nreg || src>=rlist->nreg) return;

    reginfolist_invalidatealiases(rlist, dest);

    rlist->rinfo[dest]=rlist->rinfo[src];
    rlist->rinfo[dest].usage=REGUSE_WRITTEN;
    rlist->rinfo[dest].iindx=iindx;
    rlist->rinfo[dest].hasalias=true;
    rlist->rinfo[dest].alias=src;
}

/** Sets the type associated with a register */
void reginfolist_settype(reginfolist *rlist, int rindx, value type) {
    reginfolist_settypeinfo(rlist, rindx, type, REGTYPE_EXACT);
}

/** Sets the type and precision associated with a register */
void reginfolist_settypeinfo(reginfolist *rlist, int rindx, value type, regtypeinfo info) {
    if (rindx>=rlist->nreg) return;
    rlist->rinfo[rindx].type=type;
    rlist->rinfo[rindx].typeinfo=(MORPHO_ISNIL(type) ? REGTYPE_UNKNOWN : info);
    if (rlist->rinfo[rindx].contents==REG_VALUE && !MORPHO_ISNIL(type)) {
        rlist->rinfo[rindx].contents=REG_TYPEDVALUE;
    }
}

/** Gets the type associated with a register */
value reginfolist_type(reginfolist *rlist, int rindx) {
    if (rindx>=rlist->nreg) return MORPHO_NIL;
    return rlist->rinfo[rindx].type;
}

/** Gets the type precision associated with a register */
regtypeinfo reginfolist_typeinfo(reginfolist *rlist, int rindx) {
    if (rindx>=rlist->nreg) return REGTYPE_UNKNOWN;
    return rlist->rinfo[rindx].typeinfo;
}

/** Gets the content type and index associated with a register */
bool reginfolist_contents(reginfolist *rlist, int rindx, regcontents *contents, indx *indx) {
    if (rindx>=rlist->nreg) return false;
    if (contents) *contents = rlist->rinfo[rindx].contents;
    if (indx) *indx = rlist->rinfo[rindx].indx;
    return true;
}

/** Gets the content type associated with a register */
regcontents reginfolist_regcontents(reginfolist *rlist, int rindx) {
    if (rindx>=rlist->nreg) return REG_NOFACT;
    return rlist->rinfo[rindx].contents;
}

/** Gets alias information associated with a register. */
bool reginfolist_alias(reginfolist *rlist, int rindx, registerindx *alias) {
    if (rindx>=rlist->nreg) return false;
    if (!rlist->rinfo[rindx].hasalias) return false;
    if (alias) *alias = rlist->rinfo[rindx].alias;
    return true;
}

/** Gets the instruction responsible for writing to this store */
bool reginfolist_source(reginfolist *rlist, int rindx, instructionindx *iindx) {
    if (rindx>=rlist->nreg) return false;
    if (iindx) *iindx = rlist->rinfo[rindx].iindx;
    return true;
}

/** Count whether a register fact has been read */
int reginfolist_countuses(reginfolist *rlist, int rindx) {
    if (rindx>=rlist->nreg) return 0;
    return regusage_hasread(rlist->rinfo[rindx].usage);
}

/** Count whether a register fact has been written */
int reginfolist_countwrites(reginfolist *rlist, int rindx) {
    if (rindx>=rlist->nreg) return 0;
    return regusage_haswrite(rlist->rinfo[rindx].usage);
}

/** Checks for any registers containing a given content type with specified index and converts to a value  */
void reginfolist_invalidate(reginfolist *rlist, regcontents contents, indx ix) {
    for (registerindx i=0; i<rlist->nreg; i++) {
        regcontents icontents;
        indx iindx;

        reginfolist_contents(rlist, i, &icontents, &iindx);
        if (icontents==contents && iindx==ix) {
            rlist->rinfo[i].contents = (MORPHO_ISNIL(rlist->rinfo[i].type) ? REG_VALUE : REG_TYPEDVALUE);
            reginfo_clearsource(&rlist->rinfo[i]);
            reginfo_clearalias(&rlist->rinfo[i]);
        }
    }
}

/** Converts all facts of a given content type into generic values while preserving type info. */
void reginfolist_generalizecontent(reginfolist *rlist, regcontents contents) {
    for (registerindx i=0; i<rlist->nreg; i++) {
        if (rlist->rinfo[i].contents==contents) {
            rlist->rinfo[i].contents = (MORPHO_ISNIL(rlist->rinfo[i].type) ? REG_VALUE : REG_TYPEDVALUE);
            reginfo_clearsource(&rlist->rinfo[i]);
            reginfo_clearalias(&rlist->rinfo[i]);
        }
    }
}

/** Display the register info list */
void reginfolist_show(reginfolist *rlist) {
    for (int i=0; i<rlist->nreg; i++) {
        printf("|\tr%u :", i);
        switch (rlist->rinfo[i].contents) {
            case REG_NOFACT: printf(" \n"); continue;
            case REG_PARAMETER: printf(" p"); break;
            case REG_TYPEDVALUE: printf(" tv"); break;
            case REG_VALUE: printf(" v"); break;
            case REG_CONSTANT: printf(" c%td", rlist->rinfo[i].indx); break;
            case REG_GLOBAL: printf(" g%td", rlist->rinfo[i].indx); break;
            case REG_UPVALUE: printf(" u%td", rlist->rinfo[i].indx); break;
            default: break;
        }

        if (!MORPHO_ISNIL(rlist->rinfo[i].type)) {
            printf(" ");
            if (rlist->rinfo[i].typeinfo==REGTYPE_EXACT) printf("=:");
            if (rlist->rinfo[i].typeinfo==REGTYPE_SUBTYPE) printf("<:");
            morpho_printvalue(NULL, rlist->rinfo[i].type);
        }

        switch (rlist->rinfo[i].usage) {
            case REGUSE_READ: printf(" u:r"); break;
            case REGUSE_WRITTEN: printf(" u:w"); break;
            case REGUSE_READWRITTEN: printf(" u:rw"); break;
            default: break;
        }

        if (rlist->rinfo[i].hasalias) printf(" a:r%u", rlist->rinfo[i].alias);

        if (rlist->rinfo[i].contents!=REG_NOFACT) {
            printf(" i:%i", (int) rlist->rinfo[i].iindx);
        }

        printf("\n");
    }
}
