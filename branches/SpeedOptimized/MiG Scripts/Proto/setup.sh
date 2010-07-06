#!/bin/bash

rm -rf ./network.txt &> /dev/null
touch ./network.txt

COUNTER=1
LIMIT=$1
let LIMIT=LIMIT+1
let PORT=10000+`date +%-M%S`

while [  $COUNTER -lt $LIMIT ]; do
	if [ $COUNTER -lt 10 ]; then
		HOSTID=`echo -n 0$COUNTER`
	else
		HOSTID=$COUNTER
	fi

	HOSTNAME=`echo -n PS3-$HOSTID`
	echo "$HOSTNAME $PORT" >> ./network.txt

     let COUNTER=COUNTER+1 
done
