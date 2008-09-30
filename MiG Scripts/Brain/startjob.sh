#!/bin/bash

#Usage exec hostno spu_count

#echo `pwd`
#ls
#cat network.txt
#echo "Test"

echo "Running ./$1 CT.ppm CTResult.ppm $3 $2 network.txt"
./$1 CT.ppm CTResult.ppm $3 $2 network.txt
echo "Done running"
