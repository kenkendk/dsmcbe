cd /root/Stage1Prototein/.metadata/.plugins/com.ibm.celldt.environment.cellsimulator/1209547409443/runinfo
kill -9 $(cat PID)
if [ -f TAP_DEVICE ]
   then /opt/ibm/systemsim-cell/bin/snif -d $(cat TAP_DEVICE)
fi
rm -rf ../runinfo
cd ..
