/** @file eval.h
 *  @author T J Atherton
 *
 *  @brief Basic blocks
*/

#ifndef eval_h
#define eval_h

#include "morphocore.h"
#include "optimize.h"

/** Evaluates a program given as a raw instruction list */
bool optimize_evalsubprogram(optimizer *opt, instruction *list, registerindx dest, value *out);

#endif
