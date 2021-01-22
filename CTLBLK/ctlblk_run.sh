#!/bin/bash

#
#  Run programm, check exit status,  restart in loop  ...
#

	EXEDIR=$(cd $(dirname $0)  && pwd)
	

while : 

do
        $EXEDIR/ctlblk -config=$EXEDIR/ctlblk.conf

        stv=$?

        case    $stv in
                1) 	echo "Normal successfull completion"; exit 0;;
		255)	echo "Restarting ...";;
                *)	echo "Exit code = $stv";;
        esac

        sleep   7
done

