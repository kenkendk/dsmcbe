This application folds prototein strings.
The default prototein looks like this:
PPPHHHPPP

The H and P represents the two different amino acids.
Each extra letter/acid will cause the running time to 
increase by a factor of approximately three.

You can run the program on a single machine with one of these commands:
./PPU <number of spu's>
./PPU <number of spu's> <prototein> 

Or on a cluster with one of these commands:
./PPU <machine id> <path to network file> <number of spu's>
./PPU <machine id> <path to network file> <number of spu's> <prototein>