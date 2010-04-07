#!/bin/bash

#Usage exec hostno spu_count

#echo `pwd`
#ls
#cat network.txt
#echo "Test"


#If we get the output from the run, we can see how much memory there really is
free -k

echo "Running ./$1 $2 network.txt $3"
./$1 $2 network.txt $3
echo "Done running"
