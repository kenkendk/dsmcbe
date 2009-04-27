This application simulates heat travel in a material.
This is an application of the type "Succesive Over Relaxation" (SOR).

The file "Shared.h" contains a number of #DEFINE directives,
that control how the simulation is performed.

The #DEFINE GRAPHICS can be used to get a visual display of how the
heat travels in the material, but it is hard to use with more than a few
SPU's because the amount of memory required grows very fast,
and the display machine has to keep a buffer of data.

This application can use multiple hardware threads, that are avalible
if the application is running on a Cell Blade center where two cell
processors are linked to form a 16 SPU machine.

You can run the program on a single machine with one of these commands:
./PPU <number of spu's>
./PPU <number of spu's> <number of hardware PPU threads>

Or on a cluster with one of these commands:
./PPU <machine id> <path to network file> <number of spu's>
./PPU <machine id> <path to network file> <number of spu's> <number of hardware PPU threads>
