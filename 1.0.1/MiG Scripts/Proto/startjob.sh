#!/bin/bash

#Usage exec hostno spu_count

#echo `pwd`
#ls
#cat network.txt
#echo "Test"

echo "Running ./$1 $2 network.txt $3 $4"
./$1 $2 network.txt $3 $4
echo "Done running"
