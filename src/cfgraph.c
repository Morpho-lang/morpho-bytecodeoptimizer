/** @file cfgraph.c
 *  @author T J Atherton
 *
 *  @brief Control flow graph
*/

#include "morphocore.h"
#include "cfgraph.h"
#include "opcodes.h"

DEFINE_VARRAY(instructionindx, instructionindx)
DEFINE_VARRAY(block, block)
DEFINE_VARRAY(value, value)

typedef unsigned int blockindx;

/* **********************************************************************
 * Basic blocks
 * ********************************************************************** */

/** Initializes a basic block structure */
void block_init(block *b, objectfunction *func) {
    b->start=INSTRUCTIONINDX_EMPTY;
    b->end=INSTRUCTIONINDX_EMPTY;
    b->func=func;
    
    reginfolist_init(&b->rout, func->nregs);
    
    dictionary_init(&b->src);
    dictionary_init(&b->dest);
    dictionary_init(&b->uses);
    dictionary_init(&b->writes);
}

/** Clears a basic block structure */
void block_clear(block *b) {
    reginfolist_clear(&b->rout);
    
    dictionary_clear(&b->src);
    dictionary_clear(&b->uses);
    dictionary_clear(&b->writes);
}

/* --------------
 * Register usage
 * -------------- */

/** Declare that a block uses a given register as input */
void block_setuses(block *b, registerindx r) {
    dictionary_insert(&b->uses, MORPHO_INTEGER((int) r), MORPHO_NIL);
}

/** Check if a block uses a given register */
bool block_uses(block *b, registerindx r) {
    return dictionary_get(&b->uses, MORPHO_INTEGER((int) r), NULL);
}

/** Declare that a block overwrites a given register */
void block_setwrites(block *b, registerindx r) {
    dictionary_insert(&b->writes, MORPHO_INTEGER((int) r), MORPHO_NIL);
}

/** Check if a block overwrites a given register */
bool block_writes(block *b, registerindx r) {
    return dictionary_get(&b->writes, MORPHO_INTEGER((int) r), NULL);
}

/* -----------
 * Block usage
 * ----------- */

void _wipe(dictionary *dict) { // Wipe a dictionary
    for (int i=0; i<dict->capacity; i++) dict->contents[i].key=MORPHO_NIL;
}

void _usagefn(registerindx i, void *ref) { // Set usage
    block *blk = (block *) ref;
    if (!block_writes(blk, i)) block_setuses(blk, i);
}

/** Computes the instruction usage from a block by looping over and analyzing instructions */
void block_computeusage(block *blk, instruction *ilist) {
    _wipe(&blk->writes);
    _wipe(&blk->uses);
    
    for (instructionindx i=blk->start; i<=blk->end; i++) {
        instruction instr = ilist[i];
        opcodeflags flags = opcode_getflags(DECODE_OP(instr));
        
        if (flags & OPCODE_OVERWRITES_A) block_setwrites(blk, DECODE_A(instr));
        if (flags & OPCODE_OVERWRITES_B) block_setwrites(blk, DECODE_B(instr));
        
        if (flags & OPCODE_USES_A) _usagefn(DECODE_A(instr), blk);
        if (flags & OPCODE_USES_B) _usagefn(DECODE_B(instr), blk);
        if (flags & OPCODE_USES_C) _usagefn(DECODE_C(instr), blk);
        
        if (flags & OPCODE_USES_RANGEBC) {
            for (int i=DECODE_B(instr); i<=DECODE_C(instr); i++) _usagefn(i, blk);
        }
        
        // A few opcodes have unusual usage and provide a tracking function
        opcodeusagefn usagefn=opcode_getusagefn(DECODE_OP(instr));
        if (usagefn) usagefn(instr, blk, _usagefn, blk);
    }
}

/* ----------------------
 * Source and dest blocks
 * ---------------------- */

/** Sets source blocks */
void block_setsource(block *b, instructionindx indx) {
    dictionary_insert(&b->src, MORPHO_INTEGER((int) indx), MORPHO_NIL);
}

/** Sets destination blocks */
void block_setdest(block *b, instructionindx indx) {
    dictionary_insert(&b->dest, MORPHO_INTEGER((int) indx), MORPHO_NIL);
}

/** Determines of a block is the entry point of the function */
bool block_isentry(block *b) {
    return (b->func) && (b->func->entry == (indx) b->start);
}

/** Get a constant from a block */
value block_getconstant(block *b, indx i) {
    if (i>b->func->konst.count) return MORPHO_NIL;
    return b->func->konst.data[i];
}

/* **********************************************************************
 * Control flow graph data structure
 * ********************************************************************** */

/** Initializes an cfgraph data structure */
void cfgraph_init(cfgraph *graph) {
    varray_blockinit(graph);
}

/** Clears an cfgraph data structure */
void cfgraph_clear(cfgraph *graph) {
    for (int i=0; i<graph->count; i++) block_clear(&graph->data[i]);
    varray_blockclear(graph);
}

/* Print the keys in a dictionary with a label */
void _cfgraph_printdict(char *label, dictionary *dict) {
    if (dict->count==0) return;
    printf("( %s: ", label);
    for (int i=0; i<dict->capacity; i++) {
        if (MORPHO_ISNIL(dict->contents[i].key)) continue;
        morpho_printvalue(NULL, dict->contents[i].key);
        printf(" ");
    }
    printf(") ");
}

/** Shows code blocks in a cfgraph */
void cfgraph_show(cfgraph *graph) {
    for (int i=0; i<graph->count; i++) {
        block *blk = graph->data+i;
        printf("Block %u [%td, %td] ", i, blk->start, blk->end);
        
        _cfgraph_printdict("Source", &blk->src);
        _cfgraph_printdict("Dest", &blk->dest);
        _cfgraph_printdict("Uses", &blk->uses);
        _cfgraph_printdict("Writes", &blk->writes);
        printf("\n");
    }
}

int _blockcmp(const void *a, const void *b) {
    block *aa = (block *) a;
    block *bb = (block *) b;
    return ((int) aa->start) - ((int) bb->start);
}

/** Sort a cfgraph */
void cfgraph_sort(cfgraph *graph) {
    qsort(graph->data, graph->count, sizeof(block), _blockcmp);
}

/** Find a block in a sorted cfgraph */
bool cfgraph_findsrtd(cfgraph *graph, instructionindx start, block **out) {
    block key = { .start = start };
    block *srch = bsearch(&key, graph->data, graph->count, sizeof(block), _blockcmp);
    if (out) *out=srch;
    return srch;
}

/* **********************************************************************
 * Control flow graph builder
 * ********************************************************************** */

/** Data structure to hold temporary information while building the cf graph */
typedef struct {
    program *in;
    cfgraph *out;
    
    dictionary blkindx; /** Dictionary of block indices */
    varray_instructionindx worklist;
    
    dictionary components; /** Dictionary of functions and metafunctions */
    varray_value compontentworklist;
    
    objectfunction *currentfn;
    
    bool verbose;
} cfgraphbuilder;

/** Initializes an optimizer data structure */
void cfgraphbuilder_init(cfgraphbuilder *bld, program *in, cfgraph *out, bool verbose) {
    bld->in=in;
    bld->out=out;
    varray_instructionindxinit(&bld->worklist);
    dictionary_init(&bld->blkindx);
    dictionary_init(&bld->components);
    varray_valueinit(&bld->compontentworklist);
    bld->verbose=verbose;
}

/** Clears an optimizer data structure */
void cfgraphbuilder_clear(cfgraphbuilder *bld) {
    varray_instructionindxclear(&bld->worklist);
    dictionary_clear(&bld->blkindx);
    dictionary_clear(&bld->components);
    varray_valueclear(&bld->compontentworklist);
}

/** Adds a block to the worklist, also recording its presence in the blkindx dictionary */
void cfgraphbuilder_push(cfgraphbuilder *bld, instructionindx start) {
    // Ensure existing blocks are never processes twice
    if (dictionary_get(&bld->blkindx, MORPHO_INTEGER(start), NULL)) return;
    dictionary_insert(&bld->blkindx, MORPHO_INTEGER(start), MORPHO_NIL);
    
    varray_instructionindxadd(&bld->worklist, &start, 1);
}

/** Pops a block item off the worklist */
bool cfgraphbuilder_pop(cfgraphbuilder *bld, instructionindx *item) {
    return varray_instructionindxpop(&bld->worklist, item);
}

/** Count the total number of instructions in the input program */
int cfgraphbuilder_countinstructions(cfgraphbuilder *bld) {
    return bld->in->code.count;
}

/** Fetches the instruction at index i */
instruction cfgraphbuilder_fetch(cfgraphbuilder *bld, instructionindx i) {
    return bld->in->code.data[i];
}

/** Sets the current function */
void cfgraphbuilder_setcurrentfn(cfgraphbuilder *bld, objectfunction *func) {
    bld->currentfn=func;
}

/** Gets the current function */
objectfunction *cfgraphbuilder_currentfn(cfgraphbuilder *bld) {
    return bld->currentfn;
}

/** Checks if a block indx is in the blkindex dictionary  */
bool cfgraphbuilder_checkblock(cfgraphbuilder *bld, indx start) {
    value val;
    return dictionary_get(&bld->blkindx, MORPHO_INTEGER(start), &val);
}

/** Lookup a block indx from a given block index  */
bool cfgraphbuilder_lookupblockindx(cfgraphbuilder *bld, indx start, indx *out) {
    value val;
    
    if (!(dictionary_get(&bld->blkindx, MORPHO_INTEGER(start), &val) &&
          MORPHO_ISINTEGER(val))) return false;
    
    if (out) *out = MORPHO_GETINTEGERVALUE(val);
    return true;
}

/** Lookup a block from the block index list */
bool cfgraphbuilder_lookupblock(cfgraphbuilder *bld, indx start, block **out) {
    indx bindx;
    bool success=cfgraphbuilder_lookupblockindx(bld, start, &bindx);
    if (success) {
        if (out) *out = &bld->out->data[bindx];
    }
    return success;
}

/** Searches the block list for any block with an instruction indx **that lies within it** */
bool cfgraphbuilder_findinblock(cfgraph *graph, instructionindx indx, block **blk) {
    for (blockindx i=0; i<graph->count; i++) {
        if (indx>=graph->data[i].start &&
            indx<=graph->data[i].end) {
            if (blk) *blk = graph->data+i;
            return true;
        }
    }
    return false;
}

/** Adds a block to the control flow graph */
bool cfgraphbuilder_addblock(cfgraphbuilder *bld, block *blk) {
    bool success=varray_blockadd(bld->out, blk, 1);
    dictionary_insert(&bld->blkindx, MORPHO_INTEGER(blk->start), MORPHO_INTEGER(bld->out->count-1));
    return success;
}

/** Adds a component to the worklist if it has not already been processed */
void cfgraphbuilder_pushcomponent(cfgraphbuilder *bld, value cmp) {
    if (dictionary_get(&bld->components, cmp, NULL)) return;
    varray_valueadd(&bld->compontentworklist, &cmp, 1);
}

/** Pops a component off the worklist */
bool cfgraphbuilder_popcomponent(cfgraphbuilder *bld, value *cmp) {
    return varray_valuepop(&bld->compontentworklist, cmp);
}

/* **********************************************************************
 * Build basic blocks
 * ********************************************************************** */

/** Splits a block at instruction 'split' */
void cfgraphbuilder_split(cfgraphbuilder *bld, block *blk, instructionindx split) {
    if (blk->start==split) return; // No need to split
    
    blk->end=split-1; // Update the end of this block
    cfgraphbuilder_push(bld, split);
}

/** Finds whether the branch points to or wthin an existing block and either splits it as necessary
    or creates a new block */
void cfgraphbuilder_branchto(cfgraphbuilder *bld, instructionindx start) {
    block *old;
    
    if (cfgraphbuilder_lookupblock(bld, start, &old) ||
        cfgraphbuilder_findinblock(bld->out, start, &old)) {
        cfgraphbuilder_split(bld, old, start);
    } else {
        cfgraphbuilder_push(bld, start);
    }
}

/** Finds and processes a branch table*/
void cfgraphbuilder_branchtable(cfgraphbuilder *bld, indx kindx) {
    value target = bld->currentfn->konst.data[kindx];
    
    if (MORPHO_ISDICTIONARY(target)) {
        dictionary *dict = &MORPHO_GETDICTIONARY(target)->dict;
        
        for (int i=0; i<dict->capacity; i++) {
            if (!MORPHO_ISNIL(dict->contents[i].key) &&
                MORPHO_ISINTEGER(dict->contents[i].val)) {
                cfgraphbuilder_branchto(bld, MORPHO_GETINTEGERVALUE(dict->contents[i].val));
            }
        }
    }
}

/** Creates a new basic block starting at a given instruction */
void cfgraphbuilder_buildblock(cfgraphbuilder *bld, instructionindx start) {
    block blk;
    block_init(&blk, cfgraphbuilder_currentfn(bld));
    blk.start=start;
    
    instructionindx i;
    for (i=start; i<cfgraphbuilder_countinstructions(bld); i++) {
        instruction instr = cfgraphbuilder_fetch(bld, i);
        opcodeflags flags = opcode_getflags(DECODE_OP(instr));
        
        // Some opcodes enerate a block immediately afterwards
        if (flags & OPCODE_NEWBLOCKAFTER) cfgraphbuilder_branchto(bld, i+1);
        
        // Branches generate a block at the branch target
        if (flags & OPCODE_BRANCH) {
            int branchby = DECODE_sBx(instr);
            cfgraphbuilder_branchto(bld, i+1+branchby);
        }

        // Branch tables generate blocks at their targets
        if (flags & OPCODE_BRANCH_TABLE) {
            indx kindx = DECODE_Bx(instr);
            cfgraphbuilder_branchtable(bld, kindx);
        }
        
        // Terminate at a block ending instruction
        if (flags & OPCODE_ENDSBLOCK) break;
        
        // Check if we have reached the start of an existing or planned block
        if (cfgraphbuilder_checkblock(bld, i+1)) break;
    }
        
    blk.end=i; // Record end point
        
    cfgraphbuilder_addblock(bld, &blk);
}

/** Finds the destination block dest and add src to its source list */
void cfgraphbuilder_setsrc(cfgraphbuilder *bld, instructionindx src, instructionindx dest) {
    block *blk;
    if (cfgraphbuilder_lookupblock(bld, dest, &blk)) block_setsource(blk, src);
}

/** Determines the destination blocks for a given block  */
void cfgraphbuilder_blockdest(cfgraphbuilder *bld, block *blk) {
    instruction instr = cfgraphbuilder_fetch(bld, blk->end); // Only need to look at last instruction
    instruction op = DECODE_OP(instr);
    opcodeflags flags = opcode_getflags(op);
    
    if (flags & OPCODE_TERMINATING) return; // Terminal blocks have no destination
    
    if (flags & OPCODE_BRANCH) {
        instructionindx dest = blk->end+1+DECODE_sBx(instr);
        block_setdest(blk, dest);
        cfgraphbuilder_setsrc(bld, blk->start, dest);
        
        if (!(flags & OPCODE_NEWBLOCKAFTER)) return; // Unconditional branches link only to their dest
    }
    
    block_setdest(blk, blk->end+1); // Link to following block
    cfgraphbuilder_setsrc(bld, blk->start, blk->end+1);
}

/* **********************************************************************
 * Find functions and methods within other components
 * ********************************************************************** */

/** Pushes a function's entry block to the control flow graph worklist */
void cfgraphbuilder_pushfunctionentryblock(cfgraphbuilder *bld, objectfunction *func) {
    cfgraphbuilder_setcurrentfn(bld, func);
    cfgraphbuilder_push(bld, func->entry);
}

/** Checks if a value contains a component*/
bool cfgraphbuilder_iscomponent(value val) {
    return MORPHO_ISFUNCTION(val) || MORPHO_ISMETAFUNCTION(val) || MORPHO_ISCLASS(val);
}

/** Searches a class for components and adds them */
void cfgraphbuilder_searchclass(cfgraphbuilder *bld, objectclass *klss) {
    for (unsigned int i=0; i<klss->methods.capacity; i++) {
        if (!MORPHO_ISNIL(klss->methods.contents[i].key)) {
            value val = klss->methods.contents[i].val;
            if (cfgraphbuilder_iscomponent(val)) cfgraphbuilder_pushcomponent(bld, val);
        }
    }
}

/** Searches a metafunction for components and adds them */
void cfgraphbuilder_searchmetafunction(cfgraphbuilder *bld, objectmetafunction *mf) {
    for (unsigned int i=0; i<mf->fns.count; i++) {
        value val = mf->fns.data[i];
        if (cfgraphbuilder_iscomponent(val)) cfgraphbuilder_pushcomponent(bld, val);
    }
}

/** Searches a function for functions in its constant table; these are added to the component list */
void cfgraphbuilder_searchfunction(cfgraphbuilder *bld, objectfunction *func) {
    for (unsigned int i=0; i<func->konst.count; i++) {
        value konst = func->konst.data[i];
        if (cfgraphbuilder_iscomponent(konst)) cfgraphbuilder_pushcomponent(bld, konst);
    }
}

/** Processes a component to find functions, methods and additional blocks */
void cfgraphbuilder_processcomponent(cfgraphbuilder *bld, value comp) {
    dictionary_insert(&bld->components, comp, MORPHO_NIL);
    
    if (MORPHO_ISFUNCTION(comp)) {
        objectfunction *func = MORPHO_GETFUNCTION(comp);
        if (bld->verbose) printf("Processing function '%s'\n", MORPHO_ISSTRING(func->name) ? MORPHO_GETCSTRING(func->name) : "<fn>");
        cfgraphbuilder_pushfunctionentryblock(bld, func);
        cfgraphbuilder_searchfunction(bld, func);
    } else if (MORPHO_ISMETAFUNCTION(comp)) {
        cfgraphbuilder_searchmetafunction(bld, MORPHO_GETMETAFUNCTION(comp));
    } else if (MORPHO_ISCLASS(comp)) {
        cfgraphbuilder_searchclass(bld, MORPHO_GETCLASS(comp));
    }
}

/* **********************************************************************
 * Build control flow graph
 * ********************************************************************** */

/** Builds a control flow graph; the blocks are sorted in order  */
void cfgraph_build(program *in, cfgraph *out, bool verbose) {
    cfgraphbuilder bld;
    
    cfgraphbuilder_init(&bld, in, out, verbose);
    
    cfgraphbuilder_pushcomponent(&bld, MORPHO_OBJECT(in->global));
    
    value component;
    while (cfgraphbuilder_popcomponent(&bld, &component)) { // Loop over components
        cfgraphbuilder_processcomponent(&bld, component);
        
        // Process blocks generated
        instructionindx item;
        while (cfgraphbuilder_pop(&bld, &item)) {
            cfgraphbuilder_buildblock(&bld, item);
        }
    }
    
    // Identify sources and destinations for each code block
    for (int i=0; i<out->count; i++) {
        block_computeusage(&out->data[i], bld.in->code.data);
        cfgraphbuilder_blockdest(&bld, &out->data[i]);
    }
    
    cfgraphbuilder_clear(&bld);
    
    cfgraph_sort(out);
    
    if (bld.verbose) cfgraph_show(out);
}
