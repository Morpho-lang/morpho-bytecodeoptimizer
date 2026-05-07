# Todo list

## Mechanism for detecting side effects

Some instructions MAY generate side effects (and are marked as doing so)

* lix - generate an error if the indx is out of range
* six - ""
* add etc. - may call a function
* call - called function may have side effects

Some optimizations on these are possible, but require the optimizer to check for side effects. We need a mechanism for this.

## Additional strategies

* Redirection for lix and six on various types.

* Function inlining.

* Fused branch and test instructions?

* Improve type inference across blocks.

* Function call parameter type inference.

* Return value inference. [Need to provide signatures for builtin functions too].

* Compact instruction format with reduced registers? Could squeeze 4 six-bit args rather than 3 8-bit args.

    lix rout, obj, i, j    ; rout <- obj[i,j]

    -> This would save a lot of mov instructions to get arguments into correct place. [Ising energy function]

* Avoid use of temporary objects. Could a stack based allocator help?

* Replace mov of a shadowed register with the original register.

* Range in for..in elimination.

## Outstanding bugs

* Resolve cycles in register cross-references [causes a stack overflow]

## Current results

    other:
                        morpho6 -O morpho6 
    tests/Ising         0.28     0.29    
    tests/Fibonacci     0.23     0.24    
    tests/DeltaBlue     0.24     0.23  

    tests/Ising         0.27     0.28    
    tests/Fibonacci     0.22     0.22    
    tests/DeltaBlue     0.19     0.21      

    language: 
                    morpho6 -O morpho6 
    tests/TupleCreation 0.21     1.54    
    tests/MethodCall    0.31     0.33    
    tests/Metafunction  2.82     2.92    
    tests/For           0.13     0.16    
    tests/ListCreation  2.16     2.29    
    tests/Matrix        1.52     2.45    
    tests/Loop          0.03     0.03    

    problems: 
                        morpho6 -O morpho6 
    tests/RelaxCube     0.04     0.09    
    tests/Tactoid       0.75     0.79    
    tests/ImplicitMesh  0.93     0.99    
    tests/MeshGen       0.67     0.65    
    tests/Adhesion      2.5      2.41 

    tests/RelaxCube     0.04     0.09    
    tests/Tactoid       0.68     0.72    
    tests/ImplicitMesh  0.78     0.83    
    tests/MeshGen       0.55     0.53    
    tests/Adhesion      1.98     2.03    

    clbg: 
                        morpho6 -O morpho6 
    tests/BinaryTrees   1.95     1.95    
    tests/NBody         1.79     1.96    
    tests/TooSimple     0.69     0.7     
    tests/Mandelbrot    1.26     1.34    
    tests/Fasta         0.93     0.93    
    tests/SpectralNorm  1.71     1.74    
    tests/Fannkuch      1.75     1.77    

    tests/BinaryTrees   1.55     1.52    
    tests/NBody         1.46     1.58    
    tests/TooSimple     0.57     0.59    
    tests/Mandelbrot    1.02     1.1     
    tests/Fasta         0.75     0.77    
    tests/SpectralNorm  1.46     1.49    
    tests/Fannkuch      1.47     1.47    

    bytecodeoptimizer:
                        morpho6 -O morpho6 
    Pow                 0.98     1.97    
    ForIn               0.94     1.12    
    Fannkuch            2.59     3.83    
    Filament            2.85     3.23    
    ListLookup          0.47     1.36    
    MethodLookup        0.44     0.58    

    Pow                 0.72     1.47    
    ForIn               0.75     0.89    
    Fannkuch            2.19     3.29    
    Filament            2.42     2.84    
    ListLookup          0.4      1.2     
    MethodLookup        0.39     0.5     


    =====

    . Finish the cheap local wins.
Do branch elimination here, especially constant-condition bif/biff, algebraic identities, compare-then-branch simplification, and cleanup of redundant loads/moves. This gives immediate wins and also reduces noise before harder analyses.

2. Fix inter-block propagation as a real dataflow pass.
This is the structural step. Without it, anything loop-related or path-sensitive will stay weak. I would make this a forward fixpoint analysis over the CFG for:
• register contents
• exact types
• constant values where safe
• simple copy facts

3. Add targeted inter-procedural propagation, but keep it narrow at first.
I would not jump straight to broad IPA. Start with:
• direct known callees only
• propagate argument types into callee entry state
• compute a summary per function: return type, side effects, maybe small purity/class facts
-Yes. That sequence is sensible.

I’d phrase it a bit more tightly as:

1. Finish the cheap local canonicalizations.
This gives immediate wins and simplifies later analyses. That includes:
• algebraic identities
• constant conditional branch elimination
• unreachable block cleanup
• jump threading / empty block removal
• cleanup around mov and repeated lct

This stage should also normalize bytecode shape so later passes see fewer variants.

2. Replace the current inter-block propagation with a real fixpoint dataflow pass.
That is the real foundation. Until this is done, the optimizer will keep losing facts at joins and loops. I would make this cover:
• register contents
• exact constant values where possible
• exact types where possible
• induction-variable style facts if you want loop optimizations soon after

3. Add interprocedural propagation and specialization.
This is where call-site information starts to matter. The highest-value first step is not full general IPA, but:
• identify direct constant callees
• push known argument types into callee analysis
• possibly clone/specialize small functions by argument type shape
• then inline selectively

That gets you most of the benefit without needing a whole-program theorem prover.

4. Add domain-specific reductions like Range lowering.
At that point the infrastructure is finally strong enough to do it cleanly. Range reduction will depend on:
• stable type facts across blocks
• call-site/callee understanding for enumerate
• loop-shape recognition

So it belongs after 2 and alongside or after 3.

The only change I’d suggest is that branch elimination should not just be “peephole.” Once you have even modest constant propagation across blocks, branch folding becomes much stronger. So stage 1 can do the obvious local cases, but the serious version should be revisited after stage 2.

So the practical roadmap is:

1. Cheap local simplifications and CFG cleanup.
2. Proper inter-block dataflow to fixpoint.
3. Interprocedural type propagation and specialization/inlining.
4. High-level reductions like Range loop lowering.

==================

1. Remove dead copies created by consumer-side register replacement.
Evidence: arithmetic​_identities​.optimized​.txt contains patterns like mov r2, r0 followed by print r0, where the destination copy is no longer used.
Likely area: strategy.c and optimize.c.
Improvement: after strategy​_register​_replacement, re-check whether the producer mov became dead and delete it in the same pass/block.

2. Make method-resolution rewrites less churn-heavy.
Evidence: aggregate invoke -> method conversion is large, but many rewrites insert lct <fn>; method ...; lct <label> scaffolding and sometimes increase code size.
Likely area: strategy.c.
Improvement: only apply strategy​_method​_resolution when the restored label is actually needed later, or when the resolved form is expected to enable another profitable rewrite.

3. Improve loop-header join precision.
Evidence: for​_in​.morpho folds setup aggressively, but loop-body invoke and branch structure remain largely unchanged because facts collapse at the backedge.
Likely area: reginfo.c and optimize.c.
Improvement: preserve more type information across joins, especially for stable class/type facts and loop-invariant values.

4. Strengthen the type join lattice before trying richer value propagation.
Evidence: current joins quickly degrade to plain v or tv even when predecessor facts are compatible at the type level.
Likely area: reginfo.c.
Improvement: join exact/subtype facts more intelligently instead of requiring exact type equality everywhere.

5. Add a post-rewrite cleanup pass for duplicate lct/mov residue.
Evidence: snapshots show many transformations succeed but leave nearby structural noise, especially after insertions and register propagation.
Likely area: strategy.c.
Improvement: add a final low-cost canonicalization sweep focused on nop, self-copy, dead mov, repeated lct, and now-unused temporaries.

6. Revisit duplicate-load heuristics for code size versus later simplification.
Evidence: mov count rises substantially (+24146) while lct falls, which is often fine but not always better if the move does not unlock more cleanup.
Likely area: strategy.c.
Improvement: prefer replacement with an existing register only when that alias is likely to survive long enough to pay off.

7. Add reporting around which strategies fire most and what net deltas they cause.
Evidence: the snapshot database gives strong external evidence, but it is still hard to attribute gains/regressions precisely.
Likely area: strategy.c and optimize.h.
Improvement: count strategy applications and optionally record per-strategy instruction deltas in verbose mode.

8. Make method-resolution and metafunction reductions more selective in loops.
Evidence: first-iteration setup often improves, but repeated loop bodies still carry dynamic forms and some inserted scaffolding.
Likely area: strategy.c and strategy.c.
Improvement: bias these rewrites toward cases where the result becomes loop-invariant or can be hoisted.

9. Add targeted regression checks based on the snapshot patterns you now have.
Evidence: the snapshot corpus already exposed concrete canonicalization misses.
Improvement: keep a few representative files as optimizer regression fixtures:
• arithmetic​_identities​.morpho
• for​_in​.morpho
• method​_lookup​.morpho
• testsuite​/tests​/for​_in​/forin​_index​.morpho

10. Defer richer constant/value propagation until cleanup and join precision improve.
Evidence: the optimizer already changes 930 files and shrinks most of them; the next bottleneck is not lack of transformations, but loss of precision and residual churn.
Improvement: prioritize “cleaner existing rewrites” and “better joins” before adding more peephole rules.

========

1. OP​_​CALLR
Call a known resolved callable already in a register, without the extra method-binding scaffolding.
Use case:
• after invoke -> method resolution
• when the callee is already known and the receiver/args are laid out

Shape:
• callr r​Fn, nargs, nopt
Benefit:
• lets optimization target a tighter canonical form after method resolution

2. OP​_​METHODL
Resolve and call a method using an interned label constant in one step.
Use case:
• common lct <label>; invoke
Shape:
• methodl r​Recv​Base, label​Const, nargs, nopt
Benefit:
• removes explicit label materialization into a register
• good for hot loops where the label is constant but full resolution is not possible

3. OP​_​LIXC
Load a single indexed component with a tighter contract for stable container types.
Use case:
• heavy repeated indexing in matrix/vector kernels
Shape:
• lixc r​Out, r​Obj, r​Index
Benefit:
• a place for a fast path on common numeric/container objects

4. OP​_​LIX2
Direct two-index access for matrix-like objects.
Use case:
• repeated nested index setup in kernels
Shape:
• lix2 r​Out, r​Obj, r​I, r​J
Benefit:
• avoids building or reusing extra temporaries for multi-index access
• matches your earlier note about richer instruction formats

5. OP​_​CALLM
Call a known method target with receiver already paired.
Use case:
• after method resolution when you know the exact method function and receiver
Shape:
• callm r​Method, r​Recv​Base, nargs, nopt
Benefit:
• avoids re-binding or generic invoke path
• clearer target for specialization than current method/invoke mix

6. OP​_​ADDI / OP​_​MULI / similar immediate forms
Use case:

========

 LICM / loop​-invariant hoisting: best immediate next pass. The corpus shows repeated invariant lct for functions, selectors, range helpers, and constants inside hot loops.
• Cleanup after folds: especially for cases like Tuple​Creation, where a successful fold leaves dead argument setup behind.
• IPA / call​-boundary type propagation: very promising after LICM. The annotated Fannkuch result strongly suggests parameter type propagation into callees will pay off.
• Inlining of tiny direct callees: especially for patterns like trivial f(x) { return x } in hot loops and metafunction benchmarks.
• Specialized call opcodes: method1 and call1 look especially justified by the corpus; call2, call0, and method0 are also attractive.
• Mutable​-constructor lowering: List​Creation shows a clear gap versus tuples. The likely direction is specialized list construction or temporary-list analysis, not tuple-style constant folding.
• Temporary​-object analysis: identify non-escaping temporaries and route eligible constructors to arena-based or specialized allocation paths.
• Late dead​-function pruning: probably as a byproduct of IPA rather than an early standalone pass.
• Property​-slot lowering: long-term high-payoff idea if exact receiver type plus stable layout can turn lpr/spr into indexed access.

What The Evidence Says
• Foundational intraprocedural work is largely paying off already:
   • inter-block dataflow
   • loop-sensitive joins
   • dispatch specialization
   • dead-class pruning
• The bottleneck has shifted from discovering facts to exploiting them.
• Benchmarks suggest:
   • big wins on structural/dynamic overhead cases
   • modest wins on numeric kernels
   • compile-time cost is still too high for short programs, so optimizer self-cost will matter later

Recommended Order
1. LICM
2. post​-fold cleanup / rematerialization cleanup
3. IPA for parameter and return types
4. tiny​-function inlining
5. specialized call​/method opcodes
6. temporary​-object / specialized allocation work
7. property​-slot lowering


======

VM design lessons:

A lot. The optimizer work has already surfaced several VM design lessons.

What The Current Work Says
• Cross-block and loop-stable facts matter a lot.
The VM and bytecode should make it easy to preserve and exploit simple facts like constant values, exact types, known callees, and invariant loads.
• Common call shapes dominate.
1-arg calls and methods are extremely common, and optional args are rare. A redesigned VM should not make the hottest call path pay for the most general one.
• Dynamic dispatch is worth specializing.
The big invoke -> method wins show that method lookup overhead is real and that exact receiver/selector knowledge is very valuable.
• Object/property lookup is probably too expensive.
If property access goes through dictionaries, that is likely a structural runtime tax. A redesigned VM should strongly consider slot-based object layouts.
• Temporary allocation matters.
Benchmarks like tuple/list creation suggest constructor and short-lived object overhead are important. A redesign should think hard about temporary allocation paths from the start.
• Mutability distinctions matter.
Tuples and lists want different optimizations. A good VM design should expose immutable vs mutable construction cleanly.
• Loop-body overhead is where many remaining wins live.
The bytecode and runtime should make hoisting, direct calls, and reduced staging natural rather than exceptional.
• Whole-program optimization cost matters.
For short programs, optimizer latency can outweigh runtime gains. A redesign should consider how much performance should come from cheap execution-path improvements vs expensive compile-time reasoning.

Likely VM Design Implications
• Specialized call opcodes for common arities, especially 0, 1, and probably 2.
• A faster direct-call/method path with less generic staging.
• More compact opcode encodings for common instruction shapes.
• Slot-based field/property access where layout is known.
• Explicit fast paths for common constructors and maybe temporary-object allocation.
• Better separation between general dynamic machinery and common monomorphic cases.
• Bytecode forms that make invariants and direct callees easier to encode and preserve.

What Not To Throw Away
• The current optimizer work is not wasted even if the VM changes.
It has already told you:
• which patterns dominate real code
• which abstractions are expensive
• where type and call-boundary information matter most
• which optimizations are structurally high value

So if you redesign the VM, the main lesson is:
• optimize the common dynamic cases into first-class VM concepts
• do not build the whole execution model around the most general case and hope the optimizer can recover everything later
