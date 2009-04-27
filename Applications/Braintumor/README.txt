This application simulates radiation treament of a brain tumor.
This is an application of the type "Embarrasingly Parallel".

This application uses a pre-cut set of images in the PPM format.
These images are named CTxy.ppm where x and y correspond to their
placement in a grid.
They were produced from the file CT.ppm.

The application is not particularly flexible, but it is possible to change
some values in ppu.c, and have it load another image.

The output from the simulation is a PPM file that shows the results of the 
radiation simulation.

You can run the program on a single machine this command:
./PPU CT.ppm <output file> <number of spu's>

Or on a cluster with this command:
./PPU CT.ppm <output file> <number of spu's> <machine id> <path to network file> <number of spu's>
