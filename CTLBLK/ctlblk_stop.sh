#!/bin/bash

#
#  Performs a startup action before run a main procedure.
#

	killall -9 yadpi_run.sh >/dev/null
	killall -9 yadpi  >/dev/null

	exit 0
