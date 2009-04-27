This project is the simplest usage of DSMCBE, and only starts the SPU,
prints a message and exits.

The DSMCBE usage is limited to use the startup procedure and a spin lock
for waiting.

You can run the program on a single machine with this command:
./PPU <number of spu's>

Or on a cluster with this command:
./PPU <number of spu's> <machine id> <path to network file> 