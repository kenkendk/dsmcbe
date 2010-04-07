This project is used during development to trigger certain conditions that needs special handling.
It is meant to provoke errors, such as race-conditions if they are present.

You can run the program on a single machine with this command:
./PPU <number of spu's>

Or on a cluster with this command:
./PPU <machine id> <path to network file> <number of spu's>