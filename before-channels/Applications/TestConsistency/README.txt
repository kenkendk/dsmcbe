This project is used during development to trigger certain conditions that needs special handling.
It is meant to provoke errors, such as race-conditions if they are present.
It works by issuing concurrent requests on a single shared object, and then
incrementing the value of the shared object in a predictable manner.
If the consistency model is not implemented correctly, this application
will detect that the acquired data is invalid.

You can run the program on a single machine with this command:
./PPU <number of spu's>

Or on a cluster with this command:
./PPU <number of spu's> <machine id> <path to network file> 