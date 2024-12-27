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