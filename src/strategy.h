/** @file strategy.h
 *  @author T J Atherton
 *
 *  @brief Local optimization strategies
*/

#include "morphocore.h"
#include "optimize.h"

#define OP_ANY (OP_END + 1)

/** Strategy functions; return true if the strategy "succeeds" */
typedef bool (*optimizationstrategyfn) (optimizer *opt);

/** Definition of an optimization strategy */
typedef struct {
    instruction match;
    optimizationstrategyfn fn;
    int level; 
} optimizationstrategy;

/** Apply all relevant strategies at an instruction */
void strategy_apply(optimizer *opt, int maxlevel);
