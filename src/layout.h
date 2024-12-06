/** @file layout.h
 *  @author T J Atherton
 *
 *  @brief Layout final program from control flow graph
*/

#ifndef layout_h
#define layout_h

#include "optimize.h"

void layout_sortcfgraph(optimizer *opt);
void layout_deleteunused(optimizer *opt);
void layout_fixannotations(optimizer *opt);
void layout_consolidate(optimizer *opt);

void layout(optimizer *opt);

#endif
