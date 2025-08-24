# Experiment: `std::string` vs `std::string_view` Performance

As a rookie I sometimes think I'm smarter than the system :).
While reviewing my code in `main.cpp` I came up with this idea:


> *Why not use `std::string_view` for storing places instead of creating `std::string` objects?*  


Since the `std::string_view`s are created from a memory-mapped file, they stay valid for the whole program lifetime. It looked like a perfectly legal and elegant solution.

I implemented this in **`sv_vector.cpp`**. 

---


To my surprise, the program with `std::string_view` ran **3 times slower** compared to the original `std::string` version.  

My guess: **cache behavior**.  
Memory mapping of file **`measurements.txt`** is 16 GB. This is far larger than the CPU caches, and I take pointers from random places of it. By contrast, copying data into `std::string`s creates contiguous memory layouts.

---
I used `perf` tool to verify:

```bash
$ g++-13 -O3 -std=c++23 -DNDEBUG -march=native -fno-omit-frame-pointer -g -o app sv_vector.cpp

$ sudo perf stat -e cache-references,cache-misses ./app
```
### Result string_view:
```bash
     1.436.588.208      cpu_atom cache-references/                                              (0,12%)
     1.603.119.910      cpu_core/cache-references/                                              (99,88%)
       220.107.689      cpu_atom/cache-misses/           #   15,32% of all cache refs           (0,12%)
       218.086.168      cpu_core/cache-misses/           #   13,60% of all cache refs           (99,88%)

      70,045137310 seconds time elapsed

      66,130576000 seconds user
       1,666442000 seconds sys
```
This time it wasn't that slow, but 13% cache misses is terrible. However, I was curious about performance of main.cpp. The same commands gave me the following results:
### Result string:
```bash
 Performance counter stats for './app':

       517.516.096      cpu_atom/cache-references/                                              (0,04%)
       986.078.286      cpu_core/cache-references/                                              (99,96%)
       138.760.856      cpu_atom/cache-misses/           #   26,81% of all cache refs           (0,04%)
       193.319.707      cpu_core/cache-misses/           #   19,60% of all cache refs           (99,96%)

      59,848164153 seconds time elapsed

      58,316348000 seconds user
       0,936973000 seconds sys
```
Surprisingly, here the misses are even worse, but the speed is better. Why it is so? 


Many of the "misses" in the `std::string` case are L1/L2 misses that still hit L3 quickly.
In the `std::string_view` case, some misses go all the way to DRAM and cost more time then allocating objects.


This topic is really interesting, and I am happy that first time it ran so slow, so I could make my first practical dive into caching.

The lesson I got is:
## Premature optimization is the...