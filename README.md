Deploy source tree localy:
$ git clone https://gitlab.com/SysMan-One/swarm

To build images:

$ cd INDBLK
$ ./build.sh
  Will be built executable indblk-x86_64

$ cd CTLBLK
$ ./build.sh
Will be built executable ctlblk-x86_64


To run instance:

$ ./indblk-x86_64 [-config=indblk.conf]
or/and
$ ./ctlblk-x86_64 [-config=ctlblk.conf]
