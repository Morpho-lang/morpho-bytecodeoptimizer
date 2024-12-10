# Todo list

## Mechanism for detecting side effects

Some instructions MAY generate side effects (and are marked as doing so)

* lix - generate an error if the indx is out of range
* six - ""
* add etc. - may call a function
* call - called function may have side effects

Some optimizations on these are possible, but require the optimizer to check for side effects. We need a mechanism for this.

## Mechanism to insert additional instructions

## Inter-block propagation

Propagate constant and type information across blocks. Entry blocks should acquire functions

## Additional strategies

* Specialized instructions for common cases:
    * lixl instruction - efficiently lookup a list element.
        LIXL a, b, c => ra <- b (List)[c]
        => variant 1: Simply redirect to List_getindex:
                            morpho6 -O morpho6  python3 
            ListLookup      0.4      1.35     -       
        => variant 2: Assume c is an integer and perform indexing directly:
                            morpho6 -O morpho6  python3 
            ListLookup      0.31     1.38     -      
        => Hence most of the cost is in the method lookup

    * lixm - lookup a matrix element.

* Fast method resolution:
    * method - call designated method on object with N args. [Must check inheritance tree]

* Function inlining.

## Resolve edge cases

* Resolve cycles in register cross-references [causes a stack overflow]