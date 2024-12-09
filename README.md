# morpho-bytecodeoptimizer

This experimental package provides a bytecode optimizer for morpho that aims to make morpho programs run faster.

## Installation and Prerequisites

To install the package, clone this repository onto your computer in any convenient place:

    git clone https://github.com/morpho-lang/morpho-bytecodeoptimizer.git

then add the location of this repository to your .morphopackages file.

    echo PACKAGEPATH >> ~/.morphopackages 

where PACKAGEPATH is the location of the git repository.

You need to compile the extension, which you can do by cd'ing to the repository's base folder and typing

    mkdir build
    cd build
    cmake -DCMAKE_BUILD_TYPE=Release .. 
    make install

The package can then be loaded into a morpho program using the `import` keyword.

    import bytecodeoptimizer

You must run the program with the -O flag set:

    morpho6 -O myprog.morpho
