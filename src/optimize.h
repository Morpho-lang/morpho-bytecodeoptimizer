/** @file optimize.h
 *  @author T J Atherton
 *
 *  @brief Optimizer for compiled morpho bytecode
*/

#ifndef optimize_h
#define optimize_h

#include "morphocore.h"
#include "reginfo.h"
#include "info.h"
#include "cfgraph.h"

//#define OPTIMIZER_VERBOSE

/* **********************************************************************
 * Optimizer data structure
 * ********************************************************************** */

typedef struct {
    objectfunction *func;
    reginfolist input;
} functioninputinfo;

DECLARE_VARRAY(functioninputinfo, functioninputinfo)

typedef struct {
    program *prog;
    
    error err; 
    
    cfgraph graph;
    dictionary reachable;
    bool reachabledirty;

    reginfolist rlist; /** Used to track register state */
    globalinfolist glist; /** Used to track globals */
    classinfolist classinfo; /** Store class construction metadata */
    functioninfolist functioninfo; /** Store per-function metadata */
    varray_functioninputinfo functioninputs; /** Inferred call-site inputs for functions */
    dictionary functioninputindx; /** Map functions to functioninputs indices */
    dictionary requirednregs; /** Requested register counts for subsequent passes */
    dictionary processedlabels; /** Labels whose method escapes were already processed this pass */
    
    int pass; /** Count passes */
    
    block *currentblk;
    instructionindx pc;
    instruction current;
    int nchanged; /** Number of instructions changed in this pass */
    
    varray_instruction insertions;
    
    vm *v; /** VM to execute subprograms */
    program *temp; /** Temporary program structure */
    
    bool verbose; /** Provide debugging output */
    bool ipachanged; /** Whether interprocedural facts changed during dataflow */
} optimizer;

/** Function that can be called by the optimizer to set the contents of the register info file */
typedef void (*opcodetrackingfn) (optimizer *opt);

/** Usage functions will call this function to identify registers that are used */
typedef void (*usagecallbackfn) (registerindx r, void *ref);

/** Function that can be called by the optimizer to track register usage */
typedef void (*opcodeusagefn) (instruction instr, block *blk, usagecallbackfn usefn, void *ref);

extern value typeint, typelist, typefloat, typestring, typebool, typeclosure, typerange, typetuple, typeclass, typecallable;

/* **********************************************************************
 * Interface for optimization strategies
 * ********************************************************************** */

void optimize_error(optimizer *opt, errorid id, ...);
bool optimize_checkerror(optimizer *opt);

void optimize_write(optimizer *opt, registerindx i, regcontents contents, indx indx);
void optimize_copyregister(optimizer *opt, registerindx dest, registerindx src);
void optimize_writevalue(optimizer *opt, registerindx i);
void optimize_settype(optimizer *opt, registerindx i, value type, regtypeinfo info);
void optimize_setexacttype(optimizer *opt, registerindx i, value type);
regtypeinfo optimize_typeprecision(value type);
value optimize_type(optimizer *opt, registerindx r);
regtypeinfo optimize_typeinfo(optimizer *opt, registerindx r);
bool optimize_typefromvalue(value val, value *type);
bool optimize_classisleaf(objectclass *klass);
bool optimize_classisderivedfrom(objectclass *klass, objectclass *base);
bool optimize_recordcallsite(optimizer *opt, objectfunction *func, registerindx argstart, int nargs, registerindx selfreg);
void optimize_markrecursive(optimizer *opt, objectfunction *func);

value optimize_getconstant(optimizer *opt, indx i);
bool optimize_addconstant(optimizer *opt, value val, indx *indx);

bool optimize_isempty(optimizer *opt, registerindx i);
bool optimize_isconstant(optimizer *opt, registerindx i, indx *indx);
bool optimize_isglobal(optimizer *opt, registerindx i, indx *indx);
bool optimize_isregister(optimizer *opt, registerindx i, registerindx *indx);
bool optimize_contents(optimizer *opt, registerindx i, regcontents *contents, indx *indx);
bool optimize_hasuniquetype(optimizer *opt, registerindx r);
bool optimize_hasexacttype(optimizer *opt, registerindx r);
void optimize_markescaped(optimizer *opt, objectfunction *func);
void optimize_markinitconstructoruse(optimizer *opt, objectfunction *func);
void optimize_markinitmethoduse(optimizer *opt, objectfunction *func);
void optimize_markmethodsforlabelescaped(optimizer *opt, value label);

bool optimize_isoverwritten(optimizer *opt, registerindx i, instructionindx start);

registerindx optimize_findoriginalregister(optimizer *opt, registerindx rindx);
bool optimize_findconstant(optimizer *opt, registerindx i, indx *out);

int optimize_countuses(optimizer *opt, registerindx i);
bool optimize_source(optimizer *opt, registerindx i, instructionindx *indx);

instruction optimize_getinstruction(optimizer *opt);
instruction optimize_getinstructionat(optimizer *opt, instructionindx i);
instructionindx optimize_getinstructionindx(optimizer *opt);
block *optimize_currentblock(optimizer *opt);

void optimize_replaceinstruction(optimizer *opt, instruction instr);
void optimize_replaceinstructionat(optimizer *opt, instructionindx i, instruction instr);
bool optimize_replacewithloadconstant(optimizer *opt, registerindx r, value konst);
void optimize_insertinstructions(optimizer *opt, int n, instruction *instr);
void optimize_insertinstructionswithrestart(optimizer *opt, int n, instruction *instr, bool restart);

bool optimize_deleteinstruction(optimizer *opt, instructionindx indx);

globalinfolist *optimize_globalinfolist(optimizer *opt);

void optimize_disassemble(optimizer *opt);

bool optimize_isused(optimizer *opt, registerindx rindx);
bool optimize_highestused(optimizer *opt, registerindx *out);
bool optimize_requirenregs(optimizer *opt, objectfunction *func, int nregs);
bool optimize_checkdestusage(optimizer *opt, block *blk, registerindx rindx);
bool optimize_candeletedeadstore(optimizer *opt, instruction instr, registerindx rindx);
bool optimize_blockisreachable(optimizer *opt, block *blk);
void optimize_repairerasedconditionalbranch(optimizer *opt, instruction instr);
void optimize_repairtakenconditionalbranch(optimizer *opt, instruction instr);

#endif
