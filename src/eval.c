/** @file eval.c
 *  @author T J Atherton
 *
 *  @brief Evaluate subprograms
*/

#include "morphocore.h"
#include "optimize.h"
#include "eval.h"

/** Evaluate a program 
    @param[in] opt - optimizer
    @param[in] list - instruction list terminated by OP_END
    @param[in] dest - destination register to extract after execution
    @param[out] out - result of execution */
bool optimize_evalsubprogram(optimizer *opt, instruction *list, registerindx dest, value *out) {
    bool success=false;
    objectfunction *storeglobal=opt->temp->global; // Retain the old global function
    
    objectfunction temp=*opt->currentblk->func; // Keep all the function's info, e.g. constant table
    temp.entry=0;
    
    opt->temp->global=&temp; // Patch in our function
    
    varray_instruction *code = &opt->temp->code;
    code->count=0; // Clear the program
    for (instruction *ins = list; ; ins++) { // Load the list of instructions into the program
        varray_instructionwrite(code, *ins);
        if (DECODE_OP(*ins)==OP_END) break;
    }
    
    if (morpho_run(opt->v, opt->temp)) { // Run the program and extract output
        if (out && dest< opt->v->stack.count) {
            *out = opt->v->stack.data[dest];
            if (MORPHO_ISOBJECT(*out)) vm_unbindobject(opt->v, *out);
        }
        success=true;
    }
    opt->temp->global=storeglobal; // Restore the global function
    
    return success;
}
