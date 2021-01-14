/*
**++
**
**  FACILITY:  Swarm Manager
**
**  DESCRIPTION: Constant and data structure definition commonly used accros Swarm Manager modules.
**
**  AUTHORS: Ruslan R. (The BadAss Sysman) Laishev
**
**  CREATION DATE:  12-JAN-2021
**
**  MODIFICATION HISTORY:
**
**--
*/

#ifndef	__SWARM_DEFS__
#define __SWARM_DEFS__	1

#ifdef __cplusplus
extern "C" {
#endif


/* A list of control codes/requests is supposed to be used to carry control/data information over wires */
enum {
	SWARM$K_REQ_CB_UP,				/* Contorl Block instance is UP and Running */
	SWARM$K_REQ_CB_DATAREQ,				/* Contorl Block request data set from Client */
	SWARM$K_REQ_CL_DATASET,				/* A data set from Client */

	SWARM$K_REQ_EOL					/* End-Of-List marker */
};



enum	{
	SWARM$K_STATE_DOWN = 0,
	SWARM$K_STATE_UP = 1
};

#define	SWARM$SZ_MAGIC	8
#define	SWARM$T_MAGIC	"StarLet"

#pragma	pack (push, 1)

typedef	struct  __swarm_pdu__ {
	unsigned long long	magic;			/* NBO, A magic constant is supposed to be used to filtering non-relevant packets */
	unsigned short	req;				/* NBO, Request code, see SWARM$K_REQ_* constants */

	union {
	unsigned char	data[0];			/* A plaiceholder of the payload part of the PDU */

	struct {
		int	metric;
		} cb;


	struct {
		char	text[132];
		struct timespec tm;

		int	temperature,
			light;
		} cl;

	};

} SWARM_PDU;

#define	SWARM$SZ_PDUHDR	(offsetof(struct __swarm_pdu__, data))

#pragma	pack (pop)




#ifdef __cplusplus
}
#endif

#endif	/*	__SWARM_DEFS__	*/
