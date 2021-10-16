# fuselage-experiment

ScheduleOptimization LLVM pass - based on LLVM 12

ScheduleOptimization pass generates a schedule tree from data dependences and iteration domains by running isl scheduling optimizer.

To build :

`mkdir build && cd build && cmake .. && make -j4 `
To test (after build) : 

`make test`
