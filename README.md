# local-analysis
# You can reproduce by the following step:

1. mkdir build

2. cd build

3. cmake ..

4. make -j4

5. /usr/lib/llvm-12/bin/opt -load local-analysis/FUSELAGELocal.so -local-analysis  -print-local-analysis ../tests/local-analysis/mm.ll > /dev/null

The output is like :

```
//Domian of Store inst : start from 1, output its upperbound
Store inst : C
  Domian : 
    i0: 999    i1: 999    i2: 999

//Deps : it means dstStore(i0, i1, i2) << srcStore(i2, i1, o)
Unself Deps : 
    store i32 %36, i32* %37, align 4  <<   store i32 %15, i32* %16, align 4
     0 : (1/1)i2       i0
     1 : (1/1)i1       i1
     2 : (0/0)i-2       i2
