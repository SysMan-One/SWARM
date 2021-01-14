#
#
#
SRCS+=" ../utility_routines.c"
SRCS+=" yadpi_msgs.c yadpi_flow.c yadpi_comm.c yadpi_main.c "
SRCS+=" avl.c"
SRCS+=" /media/sf_Works/SecurityCode/AVProto/avproto.c"

INCS+=" -I../"
INCS+=" -I/media/sf_Works/RBTREE/"
INCS+=" -I/media/sf_Works/SecurityCode/AVProto/"
INCS+=" -I/media/sf_Works/SecurityCode/vdpi/"


CFLAGS+=" -w" 
CFLAGS+=" -m64" 
CFLAGS+=" -fPIC" 
CFLAGS+=" -D_DEBUG=1 -D__TRACE__=1 -D__ARCH__NAME__=\"x86_64\" "

LIBS+=" -pthread"
LIBS+=" -lpcap"
LIBS+=" -lndpi"

OBJ=""
OBJS ""


set -x
set -v
#	gcc -o yadpi $CFLAGS $SRCS $INCS $LIBS

#	for i in $SRCS
#		do  	
#			gcc $CFLAGS $i $INCS $LIBS
#		done;

	for i in $SRCS
		do	
			OBJS+="$(basename -s .c $i) ";

			gcc -c $CFLAGS $i $INCS -o "$(basename -s .c $i)";
		done;

	g++ -m64 -o yadpi $OBJS $LIBS
