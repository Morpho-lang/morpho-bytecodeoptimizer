target_sources(bytecodeoptimizer
    PRIVATE
        cfgraph.c    cfgraph.h 
        eval.c       eval.h 
        layout.c     layout.h 
        morphocore.h
        opcodes.c    opcodes.h
        optimize.c   optimize.h  
        reginfo.c    reginfo.h 
        strategy.c   strategy.h
)