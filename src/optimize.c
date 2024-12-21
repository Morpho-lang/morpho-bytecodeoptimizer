/** @file optimize.c
 *  @author T J Atherton
 *
 *  @brief Optimizer for compiled morpho bytecode
*/

#include <stdarg.h>

#include "morphocore.h"
#include "optimize.h"
#include "opcodes.h"
#include "cfgraph.h"
#include "reginfo.h"
#include "info.h"
#include "strategy.h"
#include "layout.h"

/* **********************************************************************
 * Optimizer data structure
 * ********************************************************************** */

/** Initializes an optimizer data structure */
void optimizer_init(optimizer *opt, program *prog) {
    opt->prog=prog;
    opt->pass=0;
    
    error_init(&opt->err);
    cfgraph_init(&opt->graph);
    reginfolist_init(&opt->rlist, MORPHO_MAXREGISTERS);
    globalinfolist_init(&opt->glist, prog->globals.count);
    varray_instructioninit(&opt->insertions);
    
    opt->v=morpho_newvm();
    opt->temp=morpho_newprogram();
    
#ifdef OPTIMIZER_VERBOSE
    opt->verbose=true;
#else
    opt->verbose=false;
#endif
}

/** Clears an optimizer data structure */
void optimize_clear(optimizer *opt) {
    error_clear(&opt->err);
    cfgraph_clear(&opt->graph);
    reginfolist_clear(&opt->rlist);
    globalinfolist_clear(&opt->glist);
    varray_instructionclear(&opt->insertions);
    
    if (opt->v) morpho_freevm(opt->v);
    if (opt->temp) morpho_freeprogram(opt->temp);
}

/* **********************************************************************
 * Optimize a code block
 * ********************************************************************** */

/** Raise an error with the optimizer */
void optimize_error(optimizer *opt, errorid id, ...) {
    va_list args;
    va_start(args, id);
    morpho_writeerrorwithidvalist(&opt->err, id, NULL, 0, ERROR_POSNUNIDENTIFIABLE, args);
    va_end(args);
}

/** Checks whether an error occurred */
bool optimize_checkerror(optimizer *opt) {
    return (opt->err.cat!=ERROR_NONE);
}

/** Fetches the instruction at index i and sets this as the current instruction */
instruction optimize_fetch(optimizer *opt, instructionindx i) {
    opt->pc=i;
    return opt->current=opt->prog->code.data[i];
}

/** Callback function to set the contents of a register */
void optimize_write(optimizer *opt, registerindx r, regcontents contents, indx ix) {
    reginfolist_write(&opt->rlist, opt->pc, r, contents, ix);
}

/** Callback function to set the contents of a register */
void optimize_writevalue(optimizer *opt, registerindx r) {
    optimize_write(opt, r, REG_VALUE, INSTRUCTIONINDX_EMPTY);
}

/** Callback function to set the type of a register */
void optimize_settype(optimizer *opt, registerindx r, value type) {
    reginfolist_settype(&opt->rlist, r, type);
}

/** Callback function to set the type of a register */
value optimize_type(optimizer *opt, registerindx r) {
    return reginfolist_type(&opt->rlist, r);
}

/** Wrapper to get the type information from a value */
bool optimize_typefromvalue(value val, value *type) {
    return metafunction_typefromvalue(val, type);
}

/** Callback function to get a constant from the current constant table */
value optimize_getconstant(optimizer *opt, indx i) {
    return block_getconstant(opt->currentblk, i);
}

/** Adds a constant to the current constant table */
bool optimize_addconstant(optimizer *opt, value val, indx *out) {
    objectfunction *func = opt->currentblk->func;
    
    // Does the constant already exist?
    unsigned int k;
    if (varray_valuefindsame(&func->konst, val, &k)) {
        *out = (indx) k;
        return true;
    }
    
    // If not add it
    if (!varray_valueadd(&func->konst, &val, 1)) return false;
        
    *out=func->konst.count-1;
    
    if (MORPHO_ISOBJECT(val)) { // Bind the object to the program
        program_bindobject(opt->prog, MORPHO_GETOBJECT(val));
    }
    
    return true;
}

/** Checks if a register is empty */
bool optimize_isempty(optimizer *opt, registerindx i) {
    regcontents contents=REG_EMPTY;
    reginfolist_contents(&opt->rlist, i, &contents, NULL);
    return (contents==REG_EMPTY);
}

/** Checks if a register holds a constant */
bool optimize_isconstant(optimizer *opt, registerindx i, indx *out) {
    regcontents contents=REG_EMPTY;
    indx ix;
    if (!reginfolist_contents(&opt->rlist, i, &contents, &ix)) return false;
    
    bool success=(contents==REG_CONSTANT);
    if (success && out) *out = ix;
    
    return success;
}

/** Checks if a register holds another register */
bool optimize_isglobal(optimizer *opt, registerindx i, indx *out) {
    regcontents contents=REG_EMPTY;
    indx ix;
    if (!reginfolist_contents(&opt->rlist, i, &contents, &ix)) return false;
    
    bool success=(contents==REG_GLOBAL);
    if (success && out) *out = ix;
    
    return success;
}

/** Checks if a register holds another register */
bool optimize_isregister(optimizer *opt, registerindx i, registerindx *out) {
    regcontents contents=REG_EMPTY;
    indx ix;
    if (!reginfolist_contents(&opt->rlist, i, &contents, &ix)) return false;
    
    bool success=(contents==REG_REGISTER);
    if (success && out) *out = (registerindx) ix;
    
    return success;
}

/** Returns the content type of a register */
bool optimize_contents(optimizer *opt, registerindx i, regcontents *contents, indx *indx) {
    return reginfolist_contents(&opt->rlist, i, contents, indx);
}

/** Checks if a register is overwritten between start and the current instruction */
bool optimize_isoverwritten(optimizer *opt, registerindx rindx, instructionindx start) {
    for (instructionindx i=start; i<opt->pc; i++) {
        instruction instr = optimize_getinstructionat(opt, i);
        registerindx overwrites;
        if (opcode_overwritesforinstruction(instr, &overwrites) &&
            overwrites==rindx) {
            return true;
        }
    }
    
    return false;
}

typedef struct {
    registerindx r;
    bool isused;
} _usedstruct;

static void _usagefn(registerindx i, void *ref) { // Set usage
    _usedstruct *s = (_usedstruct *) ref;
    if (i==s->r) s->isused=true;
}

/** Checks if a register is used between the instruction after the current one and the end of the block OR an instruction that overwrites it */
bool optimize_isused(optimizer *opt, registerindx rindx) {
    for (instructionindx i=opt->pc+1; i<=opt->currentblk->end; i++) {
        instruction instr = optimize_getinstructionat(opt, i);
        
        _usedstruct s = { .r = rindx, .isused = false };
        opcode_usageforinstruction(opt->currentblk, instr, _usagefn, &s);
        if (s.isused) return true;
        
        registerindx overwrites;
        if (opcode_overwritesforinstruction(instr, &overwrites) &&
            overwrites==rindx) return false;
    }
    
    return optimize_checkdestusage(opt, opt->currentblk, rindx);
}

/** Trace back through duplicate registers */
registerindx optimize_findoriginalregister(optimizer *opt, registerindx rindx) {
    registerindx out=rindx;
    
    while (optimize_isregister(opt, out, &out)) {
        if (out==rindx) return out; // Break cycles
    }
    
    return out;
}

/** Finds whether a register refers to a constant, searching back through other registers */
bool optimize_findconstant(optimizer *opt, registerindx i, indx *out) {
    registerindx orig = optimize_findoriginalregister(opt, i);
    return optimize_isconstant(opt, orig, out);
}

/** Extracts usage information */
int optimize_countuses(optimizer *opt, registerindx i) {
    return reginfolist_countuses(&opt->rlist, i);
}

bool optimize_source(optimizer *opt, registerindx i, instructionindx *indx) {
    return reginfolist_source(&opt->rlist, i, indx);
}

/** Callback function to get the current instruction */
instruction optimize_getinstruction(optimizer *opt) {
    return opt->current;
}

/** Callback function to get the current instruction */
instructionindx optimize_getinstructionindx(optimizer *opt) {
    return opt->pc;
}

/** Gets the instruction at a given index; doesn't set this as the current instruction */
instruction optimize_getinstructionat(optimizer *opt, instructionindx i) {
    return opt->prog->code.data[i];
}

/** Get the current block */
block *optimize_currentblock(optimizer *opt) {
    return opt->currentblk;
}

/** Callback function to replace the current instruction */
void optimize_replaceinstruction(optimizer *opt, instruction instr) {
    optimize_replaceinstructionat(opt, opt->pc, instr);
    opt->current=instr;
    if (opt->verbose) optimize_disassemble(opt);
}

/** Replaces an instruction at a given index */
void optimize_replaceinstructionat(optimizer *opt, instructionindx i, instruction instr) {
    instruction oinstr = opt->prog->code.data[i];
    
    //opcodetrackingfn replacefn=opcode_getreplacefn(DECODE_OP(oinstr));
    //if (replacefn) replacefn(opt);
    
    opt->prog->code.data[i]=instr;
    opt->nchanged++;
}

/** Inserts a sequence of instructions at a given index, replacing the current instruction there */
void optimize_insertinstructions(optimizer *opt, int n, instruction *instr) {
    instructionindx start = opt->insertions.count;
    varray_instructionadd(&opt->insertions, instr, n);
    varray_instructionwrite(&opt->insertions, ENCODE_BYTE(OP_END));
    
    optimize_replaceinstruction(opt, ENCODE_LONG(OP_INSERT, n, start));
    opt->nchanged++;
}

/** Replaces the current instruction with LCT r, and a given constant */
bool optimize_replacewithloadconstant(optimizer *opt, registerindx r, value konst) {
    // Add the constant to the constant table
    indx kindx;
    if (!optimize_addconstant(opt, konst, &kindx)) return false;
    
    // Replace the instruction
    optimize_replaceinstruction(opt, ENCODE_LONG(OP_LCT, r, (unsigned int) kindx));
    
    // Set the contents of the register
    optimize_write(opt, r, REG_CONSTANT, kindx);
    
    value type; // Also set type information
    if (optimize_typefromvalue(konst, &type)) optimize_settype(opt, r, type);
    
    return true;
}

/** Attempts to delete an instruction. Checks to see if the instruction has side effects and ignores the deletion if it does.
    Returns true if the instruction was deleted */
bool optimize_deleteinstruction(optimizer *opt, instructionindx indx) {
    instruction instr = optimize_getinstructionat(opt, indx);
    if (DECODE_OP(instr)==OP_INSERT) return false; // Instruction to be deleted was an insertion point
    
    opcodeflags flags = opcode_getflags(DECODE_OP(instr));
    
    // Check for side effects
    if (flags & OPCODE_SIDEEFFECTS) return false; // Todo: Check whether an instruction with potential side effects could still be safe.
    
    optimize_replaceinstructionat(opt, indx, ENCODE_BYTE(OP_NOP));
    return true;
}

/** Gets the global info list */
globalinfolist *optimize_globalinfolist(optimizer *opt) {
    return &opt->glist;
}

void _optusagefn(registerindx r, void *ref) {
    reginfolist *rinfo = (reginfolist *) ref;
    
    reginfolist_uses(rinfo, r);
}

/** Updates reginfo usage information for an insertion */
void optimize_usageforinsertion(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    int n=DECODE_A(instr);
    instructionindx start=DECODE_Bx(instr);
    
    for (int i=0; i<n; i++) {
        instr = opt->insertions.data[start+i];
        opcode_usageforinstruction(opt->currentblk, instr, _optusagefn, &opt->rlist);
    }
}

/** Updates reginfo usage information based on the opcode */
void optimize_usage(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    if (DECODE_OP(instr)!=OP_INSERT) {
        opcode_usageforinstruction(opt->currentblk, instr, _optusagefn, &opt->rlist);
    } else optimize_usageforinsertion(opt);
}

/** Updates reginfo tracking information for an insertion */
void optimize_trackforinsertion(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    int n=DECODE_A(instr);
    instructionindx start=DECODE_Bx(instr);
    
    for (int i=0; i<n; i++) {
        instruction iinstr = opt->insertions.data[start+i];
        opt->current=iinstr; // Patch in the inserted instruction so that the tracking fn gets it
        opcodetrackingfn trackingfn = opcode_gettrackingfn(DECODE_OP(iinstr));
        if (trackingfn) trackingfn(opt);
    }
    
    opt->current=instr; // Restore current instruction
}

/** Tracks register content for the current instruction */
void optimize_track(optimizer *opt) {
    instruction op=DECODE_OP(opt->current);
    if (op!=OP_INSERT) {
        opcodetrackingfn trackingfn = opcode_gettrackingfn(DECODE_OP(opt->current));
        if (trackingfn) trackingfn(opt);
    } else {
        optimize_trackforinsertion(opt);
    }
}

/** Checks if a block contains any insertions*/
bool optimize_hasinsertions(optimizer *opt, block *blk) {
    for (instructionindx i=blk->start; i<=blk->end; i++) {
        if (DECODE_OP(optimize_getinstructionat(opt, i))==OP_INSERT) return true;
    }
    return false;
}

/** Disassembles the current instruction */
void optimize_disassemble(optimizer *opt) {
    instruction instr = optimize_getinstruction(opt);
    debugger_disassembleinstruction(NULL, instr, opt->pc, NULL, NULL);
    printf("\n");
}

bool _checkdestusage(optimizer *opt, block *blk, registerindx rindx, dictionary *checked) {
    dictionary_insert(checked, MORPHO_INTEGER(blk->start), MORPHO_NIL); // Mark this dictionary as checked
    
    for (int i=0; i<blk->dest.capacity; i++) {
        value key = blk->dest.contents[i].key;
        block *dest;
        
        if (MORPHO_ISINTEGER(key) &&
            !dictionary_get(checked, key, NULL) && // Ensure the block hasn't been checked
            cfgraph_indx(&opt->graph, MORPHO_GETINTEGERVALUE(key), &dest)) {
            if (block_uses(dest, rindx)) return true;
            
            if (!block_writes(dest, rindx) &&
                _checkdestusage(opt, dest, rindx, checked)) return true;
        }
    }
    
    return false;
}

/** Checks usage of a register by subsequent blocks; returns true if it's used */
bool optimize_checkdestusage(optimizer *opt, block *blk, registerindx rindx) {
    dictionary checked;
    dictionary_init(&checked);
    bool success=_checkdestusage(opt, blk, rindx, &checked);
    dictionary_clear(&checked);
    return success; 
}

/** Optimizations performed at the end of a code block */
void optimize_dead_store_elimination(optimizer *opt, block *blk) {
    if (opt->verbose) printf("Ending block\n");
    
    for (int i=0; i<opt->rlist.nreg; i++) {
        instructionindx src;
        
        if (!optimize_isempty(opt, i) &&                // Does the register contain something?
            reginfolist_countuses(&opt->rlist, i)==0 && // Is it being used in the block?
            reginfolist_regcontents(&opt->rlist, i)!=REG_PARAMETER && // It's not a parameter
            !optimize_checkdestusage(opt, blk, i) &&    // Is it being used elsewhere?
            reginfolist_source(&opt->rlist, i, &src) && // Identify the instruction that wrote it
            block_contains(blk, src)) { // Ensure instruction is in this block
            instruction instr = optimize_getinstructionat(opt, src);
            
            bool deleted = optimize_deleteinstruction(opt, src); // Deletes the instruction, checking for side effects
            
            if (deleted && opt->verbose) {
                printf("Deleted instruction: ");
                debugger_disassembleinstruction(NULL, instr, src, NULL, NULL);
                printf("\n");
            }
        }
    }
}

int optimize_countinsertions(optimizer *opt, block *blk) {
    int n=0;
    for (instructionindx i=blk->start; i<=blk->end; i++) {
        instruction instr = optimize_getinstructionat(opt, i);
        if (DECODE_OP(instr)==OP_INSERT) n+=DECODE_A(instr);
    }
    return n;
}

void _fixannotation(optimizer *opt, instructionindx iindx, int ninsert) {
    varray_debugannotation *alist = &opt->prog->annotations;
    instructionindx i=0;
    
    for (unsigned int j=0; j<alist->count; j++) {
        debugannotation *ann = &alist->data[j];
        if (ann->type!=DEBUG_ELEMENT) continue;
            
        if (iindx>=i && iindx<i+ann->content.element.ninstr) {
            ann->content.element.ninstr+=ninsert;
            return;
        } else i+=ann->content.element.ninstr;
    }
}

/** Rebuilds a block inserting inserted code */
bool optimize_processinsertions(optimizer *opt, block *blk) {
    varray_instruction *code = &opt->prog->code;
    
    int ninsert = optimize_countinsertions(opt, blk);
    
    if (!varray_instructionresize(code, ninsert)) return false;
    
    // Move instructions after the block end
    instructionindx nmove = code->count - (blk->end+1);
    if (nmove) memmove(&code->data[blk->end+1+ninsert], &code->data[blk->end+1], sizeof(instruction)*nmove);
    
    instructionindx k=blk->end+ninsert; // Destination index
    // Loop backwards over block copying in insertions
    for (instructionindx i=blk->end; i>=blk->start; i--) {
        instruction instr = optimize_getinstructionat(opt, i);
        if (DECODE_OP(instr)==OP_INSERT) {
            int n=DECODE_A(instr);
            k-=n;
            memcpy(code->data+k+1, opt->insertions.data+DECODE_Bx(instr), n*sizeof(instruction));
            _fixannotation(opt, i, n);
        } else {
            code->data[k]=code->data[i];
            k--;
        }
    }
    
    code->count+=ninsert;
    blk->end+=ninsert;
    opt->insertions.count=0; // Clear insertions
    
    // Fix starting and ending indices for subsequent blocks
    for (int i=0; i<opt->graph.count; i++) {
        block *b = &opt->graph.data[i];
        if (b->start>blk->start) { b->start+=ninsert; b->end+=ninsert; } 
    }
    
    if (opt->verbose) {
        printf("Expanded block [%ti - %ti]\n", blk->start, blk->end);
        for (instructionindx i=blk->start; i<=blk->end; i++) {
            instruction instr = optimize_getinstructionat(opt, i);
            debugger_disassembleinstruction(NULL, instr, i, NULL, NULL);
            printf("\n");
        }
    }
    
    return true;
}

/** Sets the contents of registers from knowledge of the function signature */
void optimize_signature(optimizer *opt) {
    objectfunction *func = optimize_currentblock(opt)->func;
    
    value type;
    for (registerindx i=0; i<func->nargs; i++) {
        reginfolist_write(&opt->rlist, func->entry, i+1, REG_PARAMETER, 0);
        if (signature_getparamtype(&func->sig, i, &type)) {
            reginfolist_settype(&opt->rlist, i+1, type);
        }
    }
}

/* Todo: Remove dead function v */
void _copy(reginfolist *src, reginfolist *dest) {
    for (int i=0; i<src->nreg; i++) {
        if (dest->rinfo[i].contents==REG_EMPTY) { // If empty, just copy across the info from src
            dest->rinfo[i]=src->rinfo[i];
        } else {
            if (src->rinfo[i].contents!=dest->rinfo[i].contents) { // If the content is different set to value
                dest->rinfo[i].contents=REG_VALUE;
            } else if ((src->rinfo[i].contents==REG_GLOBAL || // Ensure if the contents have an index that it's the same
                       src->rinfo[i].contents==REG_CONSTANT ||
                       src->rinfo[i].contents==REG_UPVALUE ||
                       src->rinfo[i].contents==REG_REGISTER) &&
                       src->rinfo[i].indx!=dest->rinfo[i].indx) {
                dest->rinfo[i].contents=REG_VALUE;
            }
            
            // Ensure types match if present
            if (!MORPHO_ISEQUAL(src->rinfo[i].type,dest->rinfo[i].type)) dest->rinfo[i].type=MORPHO_NIL;
        }
    }
}

bool _isparam(int n, block **src, int i) {
    for (int k=0; k<n; k++) if (src[k]->rout.rinfo[i].contents==REG_PARAMETER) return true;
    return false;
}

bool _isequal(reginfo *a, reginfo *b) {
    if (a->contents!=b->contents) return false;
    if ((a->contents==REG_GLOBAL ||
         a->contents==REG_UPVALUE ||
         a->contents==REG_CONSTANT ||
         a->contents==REG_REGISTER) && a->indx!=b->indx) return false;
    return true;
}

void _determinecontents(int n, block **src, int i, reginfo *out) {
    reginfo info;
    if (n>0) info=src[0]->rout.rinfo[i];
    
    for (int k=0; k<n; k++) {
        if (!_isequal(&info, &src[k]->rout.rinfo[i])) return;
    }
    
    // Don't copy register tracking between blocks if there's more than one source
    // TODO: this seemed to cause problems. Is there a way to do so safely?
    if (n>1 && info.contents==REG_REGISTER) info.contents=REG_VALUE;
    
    if (info.contents!=REG_EMPTY) *out = info;
}

value _determinetype(int n, block **src, int i) {
    value type;
    if (n>0) type=src[0]->rout.rinfo[i].type;
    
    for (int k=1; k<n; k++) {
        if (!MORPHO_ISEQUAL(src[k]->rout.rinfo[i].type, type)) return MORPHO_NIL;
    }
    return type;
}

void _resolve(int n, block **src, reginfolist *dest) {
    for (int i=0; i<dest->nreg; i++) {
        if (_isparam(n, src, i)) continue;
        _determinecontents(n, src, i, &dest->rinfo[i]);
        
        value type=_determinetype(n, src, i);
        reginfolist_settype(dest, i, type);
    }
}

void optimize_restorestate(optimizer *opt, block *blk) {
    reginfolist_wipe(&opt->rlist, blk->func->nregs);
    
    optimize_signature(opt); // Restore function parameters
    
    int nentry = blk->src.count;
    if (!block_isentry(blk) &&
        nentry>0) {
        block *srcblk[nentry]; // Unpack and find source blocks from the dictionary
        
        for (int i=0, k=0; i<blk->src.capacity; i++) {
            value key = blk->src.contents[i].key;
            if (MORPHO_ISNIL(key)) continue;
            
            if (!cfgraph_indx(&opt->graph, MORPHO_GETINTEGERVALUE(key), &srcblk[k])) return;
            
            if (opt->verbose) {
                printf("Restoring from block %ti\n", srcblk[k]->start);
                reginfolist_show(&srcblk[k]->rout);
            }
            
            k++;
        }
        
        if (blk->src.count>1) {
            
        }
        
        _resolve(blk->src.count, srcblk, &opt->rlist);
    }
    
    if (opt->verbose) {
        printf("Restored registers\n");
        reginfolist_show(&opt->rlist);
    }
}

/** Optimize a given block */
bool optimize_block(optimizer *opt, block *blk) {
    opt->currentblk=blk;
    
    do {
        opt->nchanged=0;
        
        if (opt->verbose) printf("Optimizing block [%ti - %ti]:\n", blk->start, blk->end);
        
        optimize_restorestate(opt, blk);
        
        for (instructionindx i=blk->start; i<=blk->end; i++) {
            instruction instr = optimize_fetch(opt, i);
            if (opt->verbose) optimize_disassemble(opt);
            
            // Update usage. @warning: This MUST be before optimization strategies so that usage information from this instruction is correct
            optimize_usage(opt);
            
            // Apply relevant optimization strategies given the pass number
            if (strategy_optimizeinstruction(opt, opt->pass)) {
                optimize_usage(opt); // Conservatively mark anything new as used
            }
            
            // Check if an error occurred and quit if it did
            if (optimize_checkerror(opt)) return false;
            
            // Perform tracking to track register contents from the optimized instruction
            optimize_track(opt);
            
            if (opt->verbose) reginfolist_show(&opt->rlist);
        }
        
        if (optimize_hasinsertions(opt, blk)) {
            optimize_processinsertions(opt, blk);
        } else optimize_dead_store_elimination(opt, blk);
    } while (opt->nchanged>0);
    
    // Finalize block information
    block_computeusage(blk, opt->prog->code.data); // Recompute usage
    reginfolist_copy(&opt->rlist, &blk->rout); // Store register contents on output
    
    return true;
}

/* **********************************************************************
 * Optimization passes
 * ********************************************************************** */

/** Computes the usage of globals from a block by looping over and analyzing instructions */
void optimize_globalusageforblock(optimizer *opt, block *blk) {
    for (instructionindx i=blk->start; i<=blk->end; i++) {
        instruction instr = optimize_getinstructionat(opt, i);
        instruction op=DECODE_OP(instr);
        if (op==OP_LGL) globalinfolist_read(&opt->glist, DECODE_Bx(instr));
        else if (op==OP_SGL) globalinfolist_store(&opt->glist, DECODE_Bx(instr));
    }
}

/** Computes usage of global variables */
void optimize_globalusage(optimizer *opt) {
    globalinfolist_startpass(&opt->glist);
    for (int i=0; i<opt->graph.count && !optimize_checkerror(opt); i++) {
        optimize_globalusageforblock(opt, &opt->graph.data[i]);
    }
}

/** Run an optimization pass */
void optimize_pass(optimizer *opt, int n) {
    optimize_globalusage(opt);
    
    opt->pass=n;
    if (opt->verbose) printf("===Optimization pass %i===\n", n);
    for (int i=0; i<opt->graph.count && !optimize_checkerror(opt); i++) optimize_block(opt, &opt->graph.data[i]);
}

/* **********************************************************************
 * Optimizer 
 * ********************************************************************** */

/** Public interface to optimizer */
bool optimize(program *in) {
    optimizer opt;
    
    optimizer_init(&opt, in);
    
    if (opt.verbose) morpho_disassemble(NULL, in, NULL);
    
    // Build control flow graph
    cfgraph_build(in, &opt.graph, opt.verbose);
    
    // Perform optimization passes
    for (int i=0; i<3 && !optimize_checkerror(&opt); i++) {
        optimize_pass(&opt, i);
    }
    
    if (opt.verbose) globalinfolist_show(&opt.glist);
    
    // Layout final code and repair associated data structures
    if (!optimize_checkerror(&opt)) {
        layout(&opt);
    }
    
    optimize_clear(&opt);
    return true;
}

/* **********************************************************************
 * Initialization/Finalization
 * ********************************************************************** */

value typeint, typelist, typefloat, typestring, typebool, typeclosure, typerange, typetuple;

void bytecodeoptimizer_initialize(void) {
    morpho_setoptimizer(optimize);
    opcode_initialize();

    objectstring boollabel = MORPHO_STATICSTRING(BOOL_CLASSNAME);
    typebool = builtin_findclass(MORPHO_OBJECT(&boollabel));
    
    objectstring intlabel = MORPHO_STATICSTRING(INT_CLASSNAME);
    typeint = builtin_findclass(MORPHO_OBJECT(&intlabel));
    
    objectstring floatlabel = MORPHO_STATICSTRING(FLOAT_CLASSNAME);
    typefloat = builtin_findclass(MORPHO_OBJECT(&floatlabel));
    
    objectstring stringlabel = MORPHO_STATICSTRING(STRING_CLASSNAME);
    typestring = builtin_findclass(MORPHO_OBJECT(&stringlabel));
    
    objectstring closurelabel = MORPHO_STATICSTRING(CLOSURE_CLASSNAME);
    typeclosure = builtin_findclass(MORPHO_OBJECT(&closurelabel));
    
    objectstring rangelabel = MORPHO_STATICSTRING(RANGE_CLASSNAME);
    typerange = builtin_findclass(MORPHO_OBJECT(&rangelabel));
    
    objectstring listlabel = MORPHO_STATICSTRING(LIST_CLASSNAME);
    typelist = builtin_findclass(MORPHO_OBJECT(&listlabel));
    
    objectstring tuplelabel = MORPHO_STATICSTRING(TUPLE_CLASSNAME);
    typetuple = builtin_findclass(MORPHO_OBJECT(&tuplelabel));
}

void bytecodeoptimizer_finalize(void) {
}
