#!/bin/bash

#
#  Performs a startup action before run a main procedure.
#

	EXEDIR=$(cd $(dirname $0) && pwd)
	LOGDIR='/var/log/yadpi'

	$EXEDIR/yadpi_run.sh >$LOGDIR/yadpi.log &
	echo "PID of detached process is $!"

	exit 0
