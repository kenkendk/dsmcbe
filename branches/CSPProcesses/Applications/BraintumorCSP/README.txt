This application simulates radiation treament of a brain tumor.
This is an application of the type "Embarrasingly Parallel".

This application uses an input image in the PPM format.
There is a sample file called CT.ppm.

The output from the simulation is a PPM file that shows the results of the 
radiation simulation.

You can run the program on a single machine this command:
./BraintumorCSP <number of spu's> <number of threads on each spu> CT.ppm <output file>

Run the program without arguments to see the extended parameter set
