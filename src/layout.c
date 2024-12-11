/** @file layout.c
 *  @author T J Atherton
 *
 *  @brief Layout final program from control flow graph
*/

#include "morphocore.h"
#include <morpho/debug.h>
#include "layout.h"
#include "cfgraph.h"
#include "opcodes.h"
#include "optimize.h"

/* **********************************************************************
 * Block composer
 * ********************************************************************** */

typedef struct {
    cfgraph *graph;
    program *in;
    
    cfgraph outgraph;
    varray_instruction out;
    dictionary outtables;
    
    dictionary map;
} blockcomposer;

/** Initialize composer structure */
void blockcomposer_init(blockcomposer *comp, program *in, cfgraph *graph) {
    comp->in=in;
    comp->graph=graph;
    
    cfgraph_init(&comp->outgraph);
    varray_instructioninit(&comp->out);
    dictionary_init(&comp->outtables);
    
    dictionary_init(&comp->map);
}

/** Clear composer structure */
void blockcomposer_clear(blockcomposer *comp) {
    cfgraph_clear(&comp->outgraph);
    varray_instructionclear(&comp->out);
    dictionary_clear(&comp->outtables);
    
    dictionary_clear(&comp->map);
}

/** Retrieve instruction at a given index */
instruction blockcomposer_getinstruction(blockcomposer *comp, instructionindx i) {
    return comp->in->code.data[i];
}

/** Add an instruction to the output program */
instructionindx blockcomposer_addinstruction(blockcomposer *comp, instruction instr) {
    return (instructionindx) varray_instructionwrite(&comp->out, instr);
}

/** Add an instruction to the output program */
void blockcomposer_setinstructionat(blockcomposer *comp, instructionindx i, instruction instr) {
    if (i>=comp->out.count) return;
    comp->out.data[i]=instr;
}

/** Adds a block to the composer data structure */
void blockcomposer_addblock(blockcomposer *comp, block *old, block *new) {
    varray_blockadd(&comp->outgraph, new, 1);
    dictionary_insert(&comp->map, MORPHO_INTEGER(old->start), MORPHO_INTEGER(new->start));
}

/** Adds a branch table to the composer data structure */
void blockcomposer_addbranchtable(blockcomposer *comp, value table) {
    dictionary_insert(&comp->outtables, table, MORPHO_NIL);
}

/** Find a block in the source graph */
bool blockcomposer_findsrc(blockcomposer *comp, instructionindx start, block **out) {
    return cfgraph_findsrtd(comp->graph, start, out);
}

/** Maps a block id to the new block id */
bool blockcomposer_map(blockcomposer *comp, instructionindx old, instructionindx *new) {
    value val;
    bool success=dictionary_get(&comp->map, MORPHO_INTEGER(old), &val);
    
    if (success && new) {
        *new = MORPHO_GETINTEGERVALUE(val);
    }
    
    return success;
}

/** Flattens the contents of a dictionary into  */
int blockcomposer_dictflatten(dictionary *dict, int nmax, instructionindx *indx) {
    int k=0;
    for (int i=0; i<dict->capacity && k<nmax; i++) {
        if (!MORPHO_ISNIL(dict->contents[i].key)) {
            indx[k]=(instructionindx) MORPHO_GETINTEGERVALUE(dict->contents[i].key);
            k++;
        }
    }
    
    return k;
}

void _fixbrnch(blockcomposer *comp, instruction last, instructionindx newend, instructionindx dest) {
    instructionindx newdest;
    if (blockcomposer_map(comp, dest, &newdest)) {
        instruction newinstr = ENCODE_LONG(DECODE_OP(last), 
                                           DECODE_A(last),
                                           newdest - newend -1);
        blockcomposer_setinstructionat(comp, newend, newinstr);
    }
}

/** Fixes branch instructions */
void blockcomposer_fixbranch(blockcomposer *comp, block *old, block *new) {
    instruction last = blockcomposer_getinstruction(comp, old->end);
    if (!((opcode_getflags(DECODE_OP(last)) & (OPCODE_BRANCH | OPCODE_BRANCH_TABLE)) )) return; // Only process branches
    
    instructionindx dest[2] = { INSTRUCTIONINDX_EMPTY, INSTRUCTIONINDX_EMPTY };
    int n=blockcomposer_dictflatten(&old->dest, 2, dest);
    
    if (DECODE_OP(last)==OP_B || DECODE_OP(last)==OP_POPERR) {
        _fixbrnch(comp, last, new->end, dest[0]);
        
    } else if (DECODE_OP(last)==OP_BIF || DECODE_OP(last)==OP_BIFF) {
        if (dest[0]!=old->end+1) {
            _fixbrnch(comp, last, new->end, dest[0]);
        } else {
            _fixbrnch(comp, last, new->end, dest[1]);
        }
        
    } else if (DECODE_OP(last)==OP_PUSHERR) {
        indx kindx = DECODE_Bx(last);
        value btable = block_getconstant(old, kindx);
        
        blockcomposer_addbranchtable(comp, btable);
    }
}

/** Fixes a branch table */
void blockcomposer_fixbranchtable(blockcomposer *comp, dictionary *table) {
    for (int i=0; i<table->capacity; i++) {
        value key = table->contents[i].key;
        if (MORPHO_ISNIL(key)) continue;
        
        indx old = MORPHO_GETINTEGERVALUE(table->contents[i].val);
        instructionindx new;
        
        if (blockcomposer_map(comp, old, &new)) {
            dictionary_insert(table, key, MORPHO_INTEGER(new));
        }
    }
}

/** Fixes the function corresponding */
void blockcomposer_fixfunction(blockcomposer *comp, objectfunction *func) {
    instructionindx entry;
    if (blockcomposer_map(comp, func->entry, &entry)) func->entry=entry;
}

/** Processes a block by copying instructions from a source block  */
void blockcomposer_processblock(blockcomposer *comp, block *blk) {
    block out;
    block_init(&out, blk->func);
    
    out.start=comp->out.count;
    
    for (instructionindx i=blk->start; i<=blk->end; i++) {
        instruction instr = blockcomposer_getinstruction(comp, i);
        if (DECODE_OP(instr)!=OP_NOP) {
            blockcomposer_addinstruction(comp, instr);
        }
    }
    
    out.end=comp->out.count-1;
    
    blockcomposer_addblock(comp, blk, &out);
    
    if (block_isentry(blk)) blockcomposer_fixfunction(comp, blk->func);
}

/* **********************************************************************
 * Remove unused instructions
 * ********************************************************************** */

/** Sorts the control flow graph */
void layout_sortcfgraph(optimizer *opt) {
    cfgraph_sort(&opt->graph);
}

/** Deletes unusud instructions */
void layout_deleteunused(optimizer *opt) {
    varray_instruction *code = &opt->prog->code;
    
    indx blkindx=0;
    block *blk = &opt->graph.data[blkindx];
    
    // Loop over instructions
    for (indx i=0; i<code->count; i++) {
        if (i>blk->end) { // Check whether we need to move to the next block
            blkindx++;
            blk = &opt->graph.data[blkindx];
        }
        
        // Check if this instruction is in the current block and if not delete it
        if (!(i>=blk->start && i<=blk->end)) optimize_replaceinstructionat(opt, i, ENCODE_BYTE(OP_NOP));
    }
}

/* **********************************************************************
 * Fix annotations
 * ********************************************************************** */

typedef struct {
    program *in;
    
    indx aindx; // Counter for annotation list
    instructionindx iindx; // Instruction counter
    
    varray_debugannotation out; // Annotations processed
} annotationfixer;

void annotationfixer_init(annotationfixer *fix, program *p) {
    fix->in = p;
    fix->aindx = 0;
    fix->iindx = 0;
    varray_debugannotationinit(&fix->out);
}

void annotationfixer_clear(annotationfixer *fix, program *p) {
    varray_debugannotationclear(&fix->out);
}

/** Gets the current annotation */
debugannotation *annotationfixer_current(annotationfixer *fix) {
    return &fix->in->annotations.data[fix->aindx];
}

/** Get next annotation and update annotation counters */
void annotationfixer_advance(annotationfixer *fix) {
    debugannotation *ann=annotationfixer_current(fix);
    
    if (ann->type==DEBUG_ELEMENT) fix->iindx+=ann->content.element.ninstr;
    
    fix->aindx++;
}

/** Are we at the end of annotations ? */
bool annotationfixer_atend(annotationfixer *fix) {
    return !(fix->aindx < fix->in->annotations.count);
}

/** Gets the instruction at a given index */
instruction annotationfixer_getinstructionat(annotationfixer *fix, instructionindx i) {
    return fix->in->code.data[i];
}

/** Count the number of nops between two instructions */
int annotationfixer_countnops(annotationfixer *fix, instructionindx start, int ninstr) {
    int count=0;
    for (instructionindx i=start; i<start+ninstr; i++) {
        instruction instr = annotationfixer_getinstructionat(fix, i);
        if (DECODE_OP(instr)==OP_NOP) count++;
    }
    return count;
}

/** Loops over annotations, fixing the reference count */
void layout_fixannotations(optimizer *opt) {
    annotationfixer fix;
    annotationfixer_init(&fix, opt->prog);
    int ntotal = 0;
    
    if (opt->verbose) {
        printf("===Fixing annotations\nOld annotations:\n");
        debugannotation_showannotations(&fix.in->annotations);
        morpho_disassemble(NULL, fix.in, NULL);
    }
    
    for (;
         !annotationfixer_atend(&fix);
         annotationfixer_advance(&fix)) {
        debugannotation *ann=annotationfixer_current(&fix);
        
        if (ann->type==DEBUG_ELEMENT) {
            int nnops = annotationfixer_countnops(&fix, fix.iindx, ann->content.element.ninstr);
            
            int ninstr = ann->content.element.ninstr - nnops;
            
            if (ninstr) { // Don't copy across empty instructions.
                debugannotation new = *ann;
                new.content.element.ninstr = ninstr;
                varray_debugannotationadd(&fix.out, &new, 1);
            }
            
            ntotal += ninstr;
        } else { // Copy across non element records
            varray_debugannotationadd(&fix.out, ann, 1);
        }
    }

    // Swap old and new annotations
    varray_debugannotation tmp = fix.in->annotations;
    fix.in->annotations = fix.out; fix.out=tmp;
    
    if (opt->verbose) {
        printf("New annotations:\n");
        debugannotation_showannotations(&fix.in->annotations);
    }
}

/* **********************************************************************
 * Layout optimized blocks
 * ********************************************************************** */

/** Layout and consolidate output program */
void layout_consolidate(optimizer *opt) {
    blockcomposer comp;
    blockcomposer_init(&comp, opt->prog, &opt->graph);
    
    // Copy across blocks
    for (unsigned int i=0; i<comp.graph->count; i++) {
        blockcomposer_processblock(&comp, comp.graph->data+i);
    }
    
    // Fix branch instructions
    for (unsigned int i=0; i<comp.graph->count; i++) {
        blockcomposer_fixbranch(&comp, comp.graph->data+i, comp.outgraph.data+i);
    }
    
    // Fix branch tables
    for (int i=0; i<comp.outtables.capacity; i++) {
        value key = comp.outtables.contents[i].key;
        if (MORPHO_ISDICTIONARY(key)) {
            blockcomposer_fixbranchtable(&comp, &MORPHO_GETDICTIONARY(key)->dict);
        }
    }
    
    // Swap old and new code
    varray_instruction tmp = comp.in->code;
    comp.in->code=comp.out; comp.out=tmp;
    
    blockcomposer_clear(&comp);
}

/* **********************************************************************
 * Layout
 * ********************************************************************** */

/** Layout the destination program, repairing data structures as necessary */
void layout(optimizer *opt) {
    layout_sortcfgraph(opt);
    layout_deleteunused(opt);
    layout_fixannotations(opt);
    layout_consolidate(opt);
}
