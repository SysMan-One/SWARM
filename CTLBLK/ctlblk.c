#define	__MODULE__	"CTLBLK"
#define	__IDENT__	"X.00-02"
#define	__REV__		"0.02.0"

#ifdef	__GNUC__
	#ident			__IDENT__
#endif

/* We don't interesting in the some warnings */
#pragma GCC diagnostic ignored  "-Wparentheses"
#pragma GCC diagnostic ignored	"-Wdiscarded-qualifiers"
#pragma GCC diagnostic ignored	"-Wmissing-braces"
//#pragma GCC diagnostic ignored	"-Wfdollars-in-identifiers"

/*
**++
**
**  FACILITY:  Swarm Manager, Control Block
**
**  DESCRIPTION: This module implement a Swarm Management functions
**
**  BUILD:
**
**  USAGE:
**
**  AUTHORS: Ruslan R. (The BadAss SysMan) Laishev
**
**  CREATION DATE:  17-FEB-2020
**
**  MODIFICATION HISTORY:
**
**	 2-NOV-2020	RRL	Recoded using AVL Tree API (LIBAVL).
**--
*/

#ifndef	__ARCH__NAME__
#define	__ARCH__NAME__	"VAX"
#endif


#define _GNU_SOURCE
#include	<signal.h>
#include	<sched.h>
#include	<sys/types.h>
#include	<sys/stat.h>
#include	<errno.h>
#include	<netinet/in.h>
#include	<netinet/ip.h>
#include	<netinet/udp.h>
#include	<sys/time.h>
#include	<time.h>
#include	<sys/socket.h>
#include	<arpa/inet.h>
#include	<poll.h>

/*
* Defines and includes for enable extend trace and logging
*/
#define		__FAC__	"CTLBLK"
#define		__TFAC__ __FAC__ ": "
#include	"utility_routines.h"

#include	"swarm_defs.h"				/* General constant and data structures for the Swarm, The */



							/* A storage for global configuration options */
ASC	q_logfspec = {0},
	q_confspec = {0},
	q_signet = {$ASCINI("224.1.1.0:1234")};



int	g_exit_flag = 0,				/* Global flag 'all must to be stop'	*/
	g_trace = 1,					/* A flag to produce extensible logging	*/

	g_primary = 0,					/* Set instance as primary master */
	g_logsize = 0,					/* A size of the log file in octets */
	g_cbtmo = 7;					/* A TTL of CB record in the table */

int	g_metric = 0,					/* A local metric of the CB instance ,
							** 0 - this instance is a Primary Master
							*/

	g_master = 0;



int			g_signet_sd = -1;		/* A socket descriptior for the UDP multicasting */
struct sockaddr_in	g_signet = {0};


const char g_magic [SWARM$SZ_MAGIC] = "$StarLet";


OPTS optstbl [] =
{
	{$ASCINI("config"),	&q_confspec, ASC$K_SZ,	OPTS$K_CONF},

	{$ASCINI("trace"),	&g_trace, 0,		OPTS$K_OPT},
	{$ASCINI("logfile"),	&q_logfspec, ASC$K_SZ,	OPTS$K_STR},
	{$ASCINI("master"),	&g_primary, 0,		OPTS$K_OPT},

	{$ASCINI("signet"),	&q_signet, 0,		OPTS$K_STR},
	{$ASCINI("cbtmo"),	&g_cbtmo, 0,		OPTS$K_INT},

	OPTS_NULL
};



/* Follow stuff are for debug/development purpose only	*/

typedef	struct __sym_rec__ {
	unsigned long long	val;		/* binary value or mask		*/
	unsigned char		len, *sym;	/* ASCII counted string		*/
} SYM_REC;

#define	$SYM_REC_INI(s) {s, sizeof(#s)-1, #s}
#define	$SYM_REC_EOL	{0, 0, NULL}

static SYM_REC	__ip_protos_vals [] = {

	$SYM_REC_INI( IPPROTO_ICMP),
	$SYM_REC_INI( IPPROTO_IGMP),
	$SYM_REC_INI( IPPROTO_IPIP),
	$SYM_REC_INI( IPPROTO_TCP),
	$SYM_REC_INI( IPPROTO_EGP),
	$SYM_REC_INI( IPPROTO_PUP),
	$SYM_REC_INI( IPPROTO_UDP),
	$SYM_REC_INI( IPPROTO_IDP),
	$SYM_REC_INI( IPPROTO_TP),
	$SYM_REC_INI( IPPROTO_DCCP),
	$SYM_REC_INI( IPPROTO_IPV6),
	$SYM_REC_INI( IPPROTO_RSVP),
	$SYM_REC_INI( IPPROTO_GRE),
	$SYM_REC_INI( IPPROTO_ESP),
	$SYM_REC_INI( IPPROTO_AH),
	$SYM_REC_INI( IPPROTO_MTP ),
	$SYM_REC_INI( IPPROTO_BEETPH ),
	$SYM_REC_INI( IPPROTO_ENCAP ),
	$SYM_REC_INI( IPPROTO_PIM),
	$SYM_REC_INI( IPPROTO_COMP),
	$SYM_REC_INI( IPPROTO_SCTP),
	$SYM_REC_INI( IPPROTO_UDPLITE),
	$SYM_REC_INI( IPPROTO_MPLS),

	$SYM_REC_EOL		/* EOL	*/
};


/**
 * @brief __util$mask2sym - translate mask of bits to human readable ASCII string
 *
 * @param dst	- mask, 64-bits unsigned long long
 * @param tbl	- a table of bit = bit's name
 * @param src	- a buffer to accept output
 * @param srcsz	- a size of the buffer
 *
 * @return	- length of actual data in the output buffer
 */
static int	mask2sym	(
	unsigned long long	 mask,
		SYM_REC		*tbl,
		unsigned char	*out,
			int	 outsz
			)
{
int	outlen = 0;
SYM_REC	*sym = tbl;

	for ( sym = tbl; sym->val && sym->sym; sym++)
		{
		if ( !(mask & sym->val) )
			continue;

		if ( outsz > sym->len )
			{
			if ( outlen )
				{
				*(out++) = '|';
				outlen++;
				outsz--;
				}

			memcpy(out, sym->sym, sym->len);
			out	+= sym->len;
			outlen	+= sym->len;
			outsz	-= sym->len;
			}
		else	break;
		}

	*out = '\0';

	return	outlen;
}


/**
 * @brief __util$val2sym - translate value to human readable ASCII string
 *
 * @param dst	- value, 64-bits unsigned long long
 * @param tbl	- a table of bit = bit's name
 * @param src	- a buffer to accept output
 * @param srcsz	- a size of the buffer
 *
 * @return	- length of actual data in the output buffer
 */
static int	val2sym	(
	unsigned long long	 val,
		SYM_REC		*tbl,
		unsigned char	*out,
			int	 outsz
			)
{
int	outlen = 0;
SYM_REC	*sym = tbl;

	for ( sym = tbl; sym->len && sym->sym; sym++)
		{
		if ( (val != sym->val) )
			continue;

		if ( outsz > sym->len )
			{
			memcpy(out, sym->sym, sym->len);
			out	+= sym->len;
			outlen	+= sym->len;

			break;
			}
		}

	*out = '\0';

	return	outlen;
}



/*
 *   DESCRIPTION: Perform additional parsing of complex options and validate.
 *
 *   IMPLICTE INPUTS:
 *	....
 *
 *   IMPLICTE OUTPUTS:
 *
 *
 *   RETURNS:
 *	condition code
 */
int	config_validate	(void)
{
int	port = 0;
char	host[256] = {0}, buf[256] = {0};

	/*
	 * Validate & parse /SIGNET=<ip:port> pair
	 */
	if ( 2 != sscanf($ASCPTR(&q_signet), "%[^:\n]:%[^/\n]/%[^\n]", host, buf) )
		return	$LOG(STS$K_ERROR, "Cannot parse '%s'",$ASCPTR(&q_signet));

	port = atoi(buf);
	port = port ? port : 1234;

	/*
	 * Initialize assotiated with the multicast network socket structure
	 */
	if ( !inet_pton(AF_INET, host, &g_signet.sin_addr) )
		return  $LOG(STS$K_ERROR, "Cannot convert '%s' to internal representative", host);

	g_signet.sin_family = AF_INET;
	g_signet.sin_port   = htons(port);


	/* Compute a local "metrtic" it's shoud be an unique across the swarm's nodes */
	if ( g_primary )
		g_metric = 0;
	else	{
		struct timespec tp = {0};

		clock_gettime(CLOCK_MONOTONIC, &tp);
		g_metric = tp.tv_nsec;
		}


	return	STS$K_SUCCESS;
}



/*
 *
 *  Description: Initalize a socket  and binding it to a given multicast group.
 *
 *  Imlicit input:
 *	runparams:	configuration vector
 *
 *  Output:
 *	sock:		A socket descriptor
 *
 *  Return:
 *	condition code
 *
 */
int	signet_init	( void )
{
int	status, on = 1;
struct ip_mreq	mreq = {0};

	mreq.imr_multiaddr.s_addr = g_signet.sin_addr.s_addr;

	/*
	 * Allocate socket descriptor (sd), bind sd to socket
	 */
	if ( 0 > (g_signet_sd = socket(AF_INET, SOCK_DGRAM, 0)) )
		return	$LOG(STS$K_ERROR, "socket(AF_INET, SOCK_DGRAM), errno=%d", errno);

	if ( setsockopt(g_signet_sd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) )
		$LOG(STS$K_WARN, "setsockopt(%d, SO_REUSEADDR) : %s", g_signet_sd, strerror(errno));
	else	$IFTRACE(g_trace, __TFAC__ __TFAC__ "setsockopt(%d, SO_REUSEADDR) :  SUCCESS", g_signet_sd);

	if ( status = bind(g_signet_sd, (struct sockaddr *)&g_signet, sizeof(g_signet)) )
		{
		close(g_signet_sd);
		return	$LOG(STS$K_ERROR, "bind(%d, ...)->%d, errno=%d", g_signet_sd, status, errno);
		}

	/*
	 * Link to multicast group
	 */
	if ( status = setsockopt( g_signet_sd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) )
		{
		close(g_signet_sd);
		return	$LOG(STS$K_ERROR, "setsockopt(%d)->%d, errno=%d", g_signet_sd, status, errno);
		}

	return	$LOG(STS$K_SUCCESS, "[#%d]Linked with multicast group %s", g_signet_sd, inet_ntoa(mreq.imr_multiaddr));
}



#define		SWARM$K_MAXCB	128			/* It's enough for demonstration purpose */
#define		SWARM$K_MAXCL	128			/* It's enough for demonstration purpose */

static struct	cb_rec	{				/* Record to keep information about of control block instances */
	struct sockaddr_in	addr;
	struct timespec		last;
		int	state,
			metric;
} g_cb_tbl [ SWARM$K_MAXCB ];
static	int g_cb_tbl_nr = 0;				/* A number of elements in the CB table */

static struct	cl_rec	{				/* Record to keep info about clients */
	struct sockaddr_in	addr;
	struct timespec		last;

		int	state,
			temperature,
			light;

} g_cl_tbl [ SWARM$K_MAXCL ];
static	int g_cl_tbl_nr = 0;				/* A number of elements in the CL table */


static inline void	__cb_tbl_creif	(
		SWARM_PDU *pdu,
		struct sockaddr_in *addr
				)
{
int	i;
struct	cb_rec	*prec;

	/* Run over the CB table ... */
	for ( i = 0, prec = g_cb_tbl; i < g_cb_tbl_nr; i++, prec++)
		{
		if ( !memcmp(&prec->addr, addr, sizeof(struct sockaddr_in)) )
			{
			/* Update existen record */
			prec->metric = ntohl(pdu->cb.metric);
			clock_gettime(CLOCK_REALTIME, &prec->last);

			prec->state =  SWARM$K_STATE_UP;

			return;
			}
		}

	/* Is there free space in the table ?
	 * No - just return
	 */
	if ( i > SWARM$K_MAXCB )
		return;

	/* Fill new record with data */
	prec->addr = *addr;
	prec->metric = ntohl(pdu->cb.metric);
	clock_gettime(CLOCK_REALTIME, &prec->last);

	prec->state = SWARM$K_STATE_UP;
}


static inline void	__cb_tbl_check	( void )
{
int	i;
struct	cb_rec	*prec;
struct timespec now, delta = {g_cbtmo, 0};

	/* We suppose to check record's last update time against current_time-cbtmo */
	clock_gettime(CLOCK_REALTIME, &now);
	__util$sub_time(&now, &delta, &now);

	/* Run over the CB table ... */
	for ( i = 0, prec = g_cb_tbl; i < g_cl_tbl_nr; i++, prec++)
		{
		if ( 0 > __util$cmp_time(&prec->last, &now) )
			prec->state = SWARM$K_STATE_DOWN;
		}
}



static inline void	__cl_tbl_creif	(
		SWARM_PDU *pdu,
		struct sockaddr_in *addr
				)
{
int	i;
struct	cl_rec	*prec;

	/* Run over the CB table ... */
	for ( i = 0, prec = g_cl_tbl; i < g_cl_tbl_nr; i++, prec++)
		{
		if ( !memcmp(&prec->addr, addr, sizeof(struct sockaddr_in)) )
			{
			/* Update existen record */
			prec->temperature = pdu->cl.temperature;
			prec->light = pdu->cl.light;

			clock_gettime(CLOCK_REALTIME, &prec->last);

			return;
			}
		}

	/* Is there free space in the table ?
	 * No - just return
	 */
	if ( i > SWARM$K_MAXCB )
		return;

	/* Fill new record with data */
	prec->temperature = pdu->cl.temperature;
	prec->light = pdu->cl.light;

	clock_gettime(CLOCK_REALTIME, &prec->last);
}


static inline void	__cl_tbl_check	( void )
{
int	i;
struct	cl_rec	*prec;
struct timespec now, delta = {g_cbtmo, 0};

	/* We suppose to check record's last update time against current_time-cbtmo */
	clock_gettime(CLOCK_REALTIME, &now);
	__util$sub_time(&now, &delta, &now);

	/* Run over the CB table ... */
	for ( i = 0, prec = g_cl_tbl; i < g_cb_tbl_nr; i++, prec++)
		{
		if ( 0 > __util$cmp_time(&prec->last, &now) )
			prec->state = SWARM$K_STATE_DOWN;
		}
}






/*
 *   DESCRIPTION: Handle all incoming packets is comming over the multicast from CB and CL instances.
 *	maintain Client and Control Block tables.
 *
 *    INPUTS:
 *	NONE
 *
 *   OUTPUT:
 *	NONE
 *
 */

int	th_in	(void *arg)
{
int	rc;
struct pollfd pfd = {g_signet_sd, POLLIN, 0};
char	buf[512];
SWARM_PDU	*pdu = (SWARM_PDU *) &buf[sizeof(struct udphdr)];
struct	sockaddr_in rsock = {0};
int	slen = sizeof(struct sockaddr_in);


	while ( !g_exit_flag)
		{
		/* Wait for input packets ... */
		if( 0 >  (rc = poll(&pfd, 1, 1000)) && (errno != EINTR) )
			return	$LOG(STS$K_ERROR, "[#%d] poll/select()->%d, errno=%d", pfd.fd, rc, errno);
		else if ( (rc < 0) && (errno == EINTR) )
			{
			return	$LOG(STS$K_WARN, "[#%d] poll/select()->%d, errno=%d", pfd.fd, rc, errno);
			continue;
			}


		/* Retrieve data from socket buffer	*/
		slen = sizeof(struct sockaddr_in);
		rc = recvfrom(pfd.fd, buf, sizeof(buf), 0, &rsock, &slen);

		if ( (0 >= rc) && (errno != EINPROGRESS) )
			{
			$LOG(STS$K_ERROR, "[#%d] recv(1 octet)->%d, .revents=%08x(%08x), errno=%d", pfd.fd, rc, pfd.revents, pfd.events, errno);
			continue;
			}


		$DUMPHEX(buf, rc);


		/* Sanity checks ... */
		if ( memcmp(&pdu->magic, &g_magic, SWARM$SZ_MAGIC) )		/* Non-matched magic - just ignore packet */
			continue;

		switch ( ntohs(pdu->req) )
			{
			case	SWARM$K_REQ_CB_UP:				/* Cre/Upd CB instance record */
				__cb_tbl_creif(pdu, &rsock);
				break;


			case	SWARM$K_REQ_CL_DATASET:				/* Cre/update dataset record of the Client */
				__cl_tbl_creif(pdu, &rsock);

			default:						/* Just ignore unknown/unhandled request */
				continue;
			}
		}

}






/*
 *   DESCRIPTION: Handle all incoming packets is comming over the multicast from CB and CL instances.
 *	maintain Client and Control Block tables.
 *
 *    INPUTS:
 *	NONE
 *
 *   OUTPUT:
 *	NONE
 *
 */

int	th_out	(void *arg)
{
int	rc, len;
struct pollfd pfd = {g_signet_sd, POLLIN, 0};
char	buf[512];
SWARM_PDU	*pdu = (SWARM_PDU *) buf;
struct	sockaddr_in rsock = {0};
int	slen = sizeof(struct sockaddr_in);


	while ( !g_exit_flag)
		{
		memset(buf, 0, sizeof(buf));
		memcpy(&pdu->magic, g_magic, SWARM$SZ_MAGIC);

		/* Form and fill request : "We are UP & Running PDU" */
		pdu->req = htons(SWARM$K_REQ_CB_UP);

		pdu->cb.metric = htonl(g_metric);

		if ( len !=  (rc = sendto(g_signet_sd, pdu, len = sizeof(SWARM_PDU), 0, &g_signet, slen)) )
			$LOG(STS$K_ERROR, "[#%d] sendto(%d octets)->%d, errno=%d", g_signet_sd, len, rc, errno);


		/* Form and fill request : "Send to CB actual data" */
		pdu->req = htons(SWARM$K_REQ_CB_DATAREQ);

		if (len !=  (rc = sendto(g_signet_sd, pdu, len = sizeof(SWARM_PDU), 0, &g_signet, slen)) )
			$LOG(STS$K_ERROR, "[#%d] sendto(%d octets)->%d, errno=%d", g_signet_sd, len, rc, errno);

		/* Prepare data to send all clients */




		/* Sleep for X seconds before next run ... */
		for ( rc = 5; rc = sleep(rc); );
		}

}



int	main	(int argc, char* argv[])
{
int	status;
pthread_t	tid;

	$LOG(STS$K_INFO, "Rev: " __IDENT__ "/"  __ARCH__NAME__   ", (built  at "__DATE__ " " __TIME__ " with CC " __VERSION__ ")");

	/*
	 * Process command line arguments
	 */
	__util$getparams(argc, argv, optstbl);

	if ( $ASCLEN(&q_logfspec) )
		{
		__util$deflog($ASCPTR(&q_logfspec), NULL);

		$LOG(STS$K_INFO, "Rev: " __IDENT__ "/"  __ARCH__NAME__   ", (built  at "__DATE__ " " __TIME__ " with CC " __VERSION__ ")");
		}

	if ( g_trace )
		__util$showparams(optstbl);


	if ( !(1 & config_validate()) )
		return	-1;


	if ( !(1 & signet_init()) )
		return	-1;



	/* Start main capture thread ... */
	status = pthread_create(&tid, NULL, th_in, NULL);
	status = pthread_create(&tid, NULL, th_out, NULL);

	/* Loop, eat, sleep ... */
	while ( !g_exit_flag )
		{

		for ( status = 3; status = sleep(status); );
		}


	return(0);
}
