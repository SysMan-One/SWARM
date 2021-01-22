#
#set -x
#set -d
#set -v


COPTS="-fPIC -I ../ -D_DEBUG=1 -D__TRACE__=1 -pthread "

SRCS="ctlblk.c ../utility_routines.c"
EXE="ctlblk"

build	()
{
	echo	"Compile with $1 gcc for $2 ..."

	$1gcc -o $EXE-$2 -w -D__ARCH__NAME__=\"$2\" $SRCS $COPTS 
	##$1strip $EXE-$2
}

	#build	"arm-linux-gnueabihf-"		"ARMhf"
	#build	"mips-linux-gnu-"		"MIPS"
	#build	"mipsel-linux-gnu-"		"MIPSel"
	build	""				"x86_64"
	#build	"/openwrt/staging_dir/toolchain-mipsel_24kc_gcc-8.3.0_musl/bin/mipsel-openwrt-linux-musl-" "MIPSel-24kc"
