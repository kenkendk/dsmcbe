#!/bin/bash

./setup.sh $1

COUNTER=2
LIMIT=$1
let LIMIT=LIMIT+1
CWD=`pwd`

while [  $COUNTER -lt $LIMIT ]; do
	if [ $COUNTER -lt 10 ]; then
		HOSTID=`echo -n 0$COUNTER`
	else
		HOSTID=$COUNTER
	fi

	HOSTNAME=`echo -n PS3-$HOSTID`
	let MACHINEID=COUNTER-1

	ssh $HOSTNAME "cd ${PWD} && ./startjob.sh $2 $MACHINEID $3 2>$HOSTNAME.stderr 1>$HOSTNAME.stdout" &

	echo "Starting $HOSTNAME as machine $MACHINEID"

     let COUNTER=COUNTER+1 
done

#We sleep here so there is no delay in the timing sequence
# related to network setups
sleep 4
./startjob.sh $2 0 $3 $4
