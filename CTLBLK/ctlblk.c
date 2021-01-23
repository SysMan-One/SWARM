#define	__MODULE__	"CTLBLK"
#define	__IDENT__	"X.00-03"
#define	__REV__		"0.03.0"

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
**  CREATION DATE:  12-JAN-2021
**
**  MODIFICATION HISTORY:
**
**	23-JAN-2021	RRL	X.00-03 : Recoded timeout checking for PRIMARY MASTER
**--
*/

#ifndef	__ARCH__NAME__
#define	__ARCH__NAME__	"VAX"
#endif


#include	<stdio.h>
#include	<stdlib.h>
#include	<signal.h>
#include	<errno.h>
#include	<netinet/in.h>
#include	<netinet/udp.h>
#include	<time.h>
#include	<sys/socket.h>
#include	<arpa/inet.h>
#include	<poll.h>
#include	<pthread.h>
#include	<unistd.h>

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
	q_signet = {$ASCINI("224.1.1.1:1234")};



volatile int	g_exit_flag = 0,			/* Global flag 'all must to be stop'	*/
	g_trace = 0,					/* A flag to produce extensible logging	*/

	g_primary = 0,					/* Set instance as primary master */
	g_logsize = 0,					/* A size of the log file in octets */
	g_cbtmo = 7,					/* A TTL of CB record in the table */
	g_primatmo = 60*5;				/* Time before revoting of master */



volatile unsigned g_metric = 0,				/* A local metric of the CB instance ,
							** 0 - this instance is a Primary Master
							*/

	g_master = 0,					/* This is a dynamicaly changed  value
							  defines current instance mode:
							  0 - SLAVE
							  1 - PRIMARY/BACKUP MASTER mode
							*/
	g_instance_id = 0;				/* An Instance ID of CB/IB/Client */


int			g_signet_sd = -1;		/* A socket descriptior for the UDP multicasting */
struct sockaddr_in	g_signet_sk = {0};


const char g_magic [SWARM$SZ_MAGIC] = "$StarLet";	/* A magic is supposed to be used as fingerprint of the SWARM PDU */


OPTS optstbl [] =
{
	{$ASCINI("config"),	&q_confspec, ASC$K_SZ,	OPTS$K_CONF},

	{$ASCINI("trace"),	&g_trace, 0,		OPTS$K_OPT},
	{$ASCINI("logfile"),	&q_logfspec, ASC$K_SZ,	OPTS$K_STR},
	{$ASCINI("logsize"),	&g_logsize, 0,		OPTS$K_INT},
	{$ASCINI("primary"),	&g_primary, 0,		OPTS$K_OPT},

	{$ASCINI("signet"),	&q_signet, ASC$K_SZ,	OPTS$K_STR},
	{$ASCINI("cbtmo"),	&g_cbtmo, 0,		OPTS$K_INT},
	{$ASCINI("primatmo"),	&g_primatmo, 0,		OPTS$K_INT},

	OPTS_NULL
};




#define		SWARM$K_MAXCB	128			/* It's enough for demonstration purpose */
#define		SWARM$K_MAXCL	128			/* It's enough for demonstration purpose */

static struct	cb_rec	{				/* Record to keep information about of control block instances */
	unsigned		id;
	struct sockaddr_in	addr;
	struct timespec		last;
		unsigned	state,
				metric;
} g_cb_tbl [ SWARM$K_MAXCB ];
static	volatile int g_cb_tbl_nr = 0;			/* A number of elements in the CB table */

static struct	cl_rec	{				/* Record to keep info about clients */
	unsigned		id;
	struct sockaddr_in	addr;
	struct timespec		last;

		unsigned	state;
		int		temperature,
				light;

} g_cl_tbl [ SWARM$K_MAXCL ];
static	volatile int g_cl_tbl_nr = 0;			/* A number of elements in the CL table */




/*
 *   DESCRIPTION: Perform additional parsing of complex options and validate.
 *
 *   IMPLICITE INPUTS:
 *	....
 *
 *   IMPLICITE OUTPUTS:
 *
 *
 *   RETURNS:
 *	condition code
 */
int	config_validate	(void)
{
int	port = 0;
char	host[256] = {0}, buf[256] = {0};
struct timespec tp = {0};


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
	if ( !inet_pton(AF_INET, host, &g_signet_sk.sin_addr) )
		return  $LOG(STS$K_ERROR, "Cannot convert '%s' to internal representative", host);

	g_signet_sk.sin_family = AF_INET;
	g_signet_sk.sin_port   = htons(port);

	/* Compute a local "metric" it's should be an unique across the swarm's nodes */
	clock_gettime(CLOCK_MONOTONIC, &tp);
	g_instance_id = tp.tv_nsec;

	if ( g_primary )
		{
		/* This instance is administratively set to PRIMARY/MASTER mode */
		g_metric = SWARM$K_PRIMASTER;
		g_master = SWARM$K_STATE_UP;
		}
	else	{
		/* Slave mode */
		g_metric = tp.tv_nsec;
		g_master = SWARM$K_STATE_DOWN;
		}

	$LOG(STS$K_INFO, "Local metric is %d, master mode is %s", g_metric, g_master ? "ON" : "OFF");


	return	STS$K_SUCCESS;
}



/*
 *
 *  Description: Initalize a socket  and binding it to a given multicast group.
 *
 *  IMLICITE INPUT:
 *	runparams:	configuration vector
 *
 *  OUTPUTs:
 *	sock:		A socket descriptor
 *
 *  RETURNS:
 *	condition code
 *
 */
int	signet_init	( void )
{
int	status, on = 1;
struct ip_mreq	mreq = {0};

	mreq.imr_multiaddr.s_addr = g_signet_sk.sin_addr.s_addr;

	/*
	 * Allocate socket descriptor (sd), bind sd to socket
	 */
	if ( 0 > (g_signet_sd = socket(AF_INET, SOCK_DGRAM, 0)) )
		return	$LOG(STS$K_ERROR, "socket(AF_INET, SOCK_DGRAM), errno=%d", errno);

	if ( setsockopt(g_signet_sd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) )
		$LOG(STS$K_WARN, "setsockopt(%d, SO_REUSEADDR) : %s", g_signet_sd, strerror(errno));
	else	$IFTRACE(g_trace, __TFAC__ __TFAC__ "setsockopt(%d, SO_REUSEADDR) :  SUCCESS", g_signet_sd);

	if ( status = bind(g_signet_sd, (struct sockaddr *)&g_signet_sk, sizeof(g_signet_sk)) )
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

	return	$LOG(STS$K_SUCCESS, "[#%d] Linked with multicast group %s", g_signet_sd, inet_ntoa(mreq.imr_multiaddr));
}


/*
 *   DESCRIPTION: Create new or update existen record for Control Block instance
 *	in the global CB table.
 *
 *   INPUTS:
 *	pdu:	A SWARM PDU with CB data
 *	addr:	A network address of the CB instance
 *
 *   IMPLICITE INPUTS:
 *	g_cb_tbl
 *	g_cb_tbl_nr
 *
 *   OUTPUTS:
 *	NONE
 *
 *   IMPLICITE OUTPUTS:
 *
 *	g_cb_tbl
 *	g_cb_tbl_nr
 *
 *   RETURNS:
 *	NONE
 */
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
		if ( prec->id == ntohl(pdu->id) )	/* Not very elegant but it's works :-)  */
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
	prec->id = htonl(pdu->id);
	prec->addr = *addr;
	prec->metric = ntohl(pdu->cb.metric);
	clock_gettime(CLOCK_REALTIME, &prec->last);

	prec->state = SWARM$K_STATE_UP;

	g_cb_tbl_nr++;
}

/*
 *   DESCRIPTION: Scan global CB table for expired records and turn them OFF;
 *	change state of this instance to BACKUP MASTER based on metrics comparison.
 *	This routine is supposed to be called at regular interval.
 *
 *   INPUTS:
 *	NONE
 *
 *   IMPLICITE INPUTS:
 *	g_cb_tbl
 *	g_cb_tbl_nr
 *
 *   OUTPUTS:
 *	NONE
 *
 *   IMPLICITE OUTPUTS:
 *	g_master
 *
 *   RETURNS:
 *	NONE
 */
static inline void	__cb_tbl_check	( void )
{
int	i;
unsigned	metric = g_metric;
struct	cb_rec	*prec;
struct timespec now, delta = {g_cbtmo, 0}, delta2 = {g_primatmo, 0};
char	ipbuf[32];

	if ( !g_cb_tbl_nr )
		return;			/* Nothing to do */

	/* We suppose to check record's last update time against current_time-cbtmo */
	clock_gettime(CLOCK_REALTIME, &now);
	__util$sub_time(&now, &delta2, &delta2);
	__util$sub_time(&now, &delta, &delta);


	/* Run over the CB table ... */
	for ( i = 0, prec = g_cb_tbl; i < g_cb_tbl_nr; i++, prec++)
		{
		inet_ntop(AF_INET, &prec->addr.sin_addr, ipbuf, sizeof(ipbuf));
		$IFTRACE(g_trace, "[#%d] id=%04x %s:%d - metric is %d", i, prec->id, ipbuf, ntohs(prec->addr.sin_port), prec->metric);

		/* Check BACKUP MASTER instances .... */
		if ( 0 > __util$cmp_time(&prec->last, (prec->id == SWARM$K_PRIMASTER) ? &delta2 : &delta) )
			{
			__util$sub_time(&now, &prec->last, &delta);
			prec->state = SWARM$K_STATE_DOWN;
			continue;
			}

		metric = (metric > prec->metric) ? metric : prec->metric;
		}


	if ( g_metric == metric )	/* It's metric is of our CB instance ? */
		{
		/* Switch our mode to the MASTER */
		if ( (g_master != SWARM$K_STATE_UP) )
			{
			g_master = SWARM$K_STATE_UP;
			$LOG (STS$K_INFO, "Switch our state to BACKUP MASTER is ON");
			}
		}
	else if ( g_master == SWARM$K_STATE_UP )
		{
		/* Switch our mode to the non-MASTER */
		g_master = SWARM$K_STATE_DOWN;
		$LOG (STS$K_INFO, "Switch our state to BACKUP MASTER is OFF");
		}


}





/*
 *   DESCRIPTION: Create new or update existen record for Client (Indication Block) instance
 *	in the global CL table.
 *
 *   INPUTS:
 *	pdu:	A SWARM PDU with CL data
 *	addr:	A network address of the CL instance
 *
 *   IMPLICITE INPUTS:
 *	g_cl_tbl
 *	g_cl_tbl_nr
 *
 *   OUTPUTS:
 *	NONE
 *
 *   IMPLICITE OUTPUTS:
 *
 *	g_cl_tbl
 *	g_cl_tbl_nr
 *
 *   RETURNS:
 *	NONE
 */
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
		if ( prec->id == ntohl(pdu->id) )
			{
			/* Update existen record */
			prec->temperature = ntohl(pdu->cl.temperature);
			prec->light = ntohl(pdu->cl.light);

			clock_gettime(CLOCK_REALTIME, &prec->last);

			return;
			}
		}

	/* Is there free space in the table ?
	 * No - just return
	 */
	if ( i > SWARM$K_MAXCL )
		return;

	/* Fill new record with data */
	prec->id = ntohl(pdu->id);
	prec->addr = *addr;
	prec->temperature = ntohl(pdu->cl.temperature);
	prec->light = ntohl(pdu->cl.light);

	clock_gettime(CLOCK_REALTIME, &prec->last);

	g_cl_tbl_nr++;
}

/*
 *   DESCRIPTION: Scan global CL table for expired records and turn them OFF;
 *	This routine is supposed to be called at regular interval.
 *
 *   IMPLICITE INPUTS:
 *	g_cl_tbl
 *	g_cl_tbl_nr
 *
 *   IMPLICITE OUTPUTS:
 *	g_cl_tbl
 *
 *   RETURNS:
 *	NONE
 */

static inline void	__cl_tbl_check	( void )
{
int	i;
struct	cl_rec	*prec;
struct timespec now, delta = {g_cbtmo, 0};

	/* We suppose to check record's last update time against current_time-cbtmo */
	clock_gettime(CLOCK_REALTIME, &now);
	__util$sub_time(&now, &delta, &now);

	/* Run over the CB table ... */
	for ( i = 0, prec = g_cl_tbl; i < g_cl_tbl_nr; i++, prec++)
		{
		if ( 0 > __util$cmp_time(&prec->last, &now) )
			prec->state = SWARM$K_STATE_DOWN;
		}
}


/*
 *   DESCRIPTION: Perform computation of average vlues for temperature and light intensity
 *	across all active (bob-expired) CL records.
 *
 *   IMPLICITE INPUTS:
 *	g_cl_tbl
 *	g_cl_tbl_nr
 *
 *   OUTPUTS:
 *	temperature:	average temperature
 *	light:		light intensity
 *
 *   RETURNS:
 *	condition code
 *	STS$K_WARN - no data in te table (table is empty)
 *	STS$K_SUCCESS
 */
static inline int	__cl_tbl_calc	(
		int	*temperature,
		int	*light
				)
{
int	i, nr;
struct	cl_rec	*prec;
char	ipbuf[32];


	/* Preset to zero output arguments */
	*temperature = *light = 0;

	if ( !g_cl_tbl_nr )
		return	STS$K_WARN;


	/* Run over the CB table ... */
	for ( nr = i = 0, prec = g_cl_tbl; i < g_cl_tbl_nr; i++, prec++)
		{
		/* Process only active Client's record */
		if ( !(prec->state = SWARM$K_STATE_UP) )
			continue;

		inet_ntop(AF_INET, &prec->addr.sin_addr, ipbuf, sizeof(ipbuf));
		$IFTRACE(g_trace, "[#%d] id=%04x %s:%d - Temp=%d(C), Light=%d(Lumen)", i, prec->id, ipbuf, ntohs(prec->addr.sin_port), prec->temperature, prec->light);

		*temperature += prec->temperature;
		*light += prec->light;
		nr++;
		}

	if ( !nr )
		return	STS$K_WARN;


	$IFTRACE(g_trace, "Sum(Temp)=%d(C), Sum(Light)=%d(Lumen)  - %d record(-s)", *temperature, *light, nr);

	*temperature /= nr;
	*light /= nr;

	$IFTRACE(g_trace, "Average Temp=%d(C), Average Light=%d(Lumen)", *temperature, *light);

	return	STS$K_SUCCESS;
}





/*
 *   DESCRIPTION: Handle all incoming packets is comming over the multicast from CB and CL instances,
 *	maintain Client and Control Block tables.
 *
 *   INPUTS:
 *	NONE
 *
 *   OUTPUTS:
 *	NONE
 *
 */

int	th_in	(void *arg)
{
int	rc;
struct pollfd pfd = {g_signet_sd, POLLIN, 0};
char	buf[2048], ipbuf[32];
SWARM_PDU	*pdu = (SWARM_PDU *) buf;
struct	sockaddr_in rsock = {0};
int	slen = sizeof(struct sockaddr_in);
struct timespec now, last, delta = {g_cbtmo, 0};

	while ( !g_exit_flag)
		{
		clock_gettime(CLOCK_REALTIME, &now);
		__util$sub_time(&now, &delta, &now);

		__cb_tbl_check();						/* Scan for expired CB records */
		__cl_tbl_check();						/* Scan for expired Client records */


		/* Wait for input packets ... */
		if( 0 >  (rc = poll(&pfd, 1, 5000)) && (errno != EINTR) )
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
			$LOG(STS$K_ERROR, "[#%d] recv()->%d, .revents=%08x(%08x), errno=%d", pfd.fd, rc, pfd.revents, pfd.events, errno);
			continue;
			}

		/* Sanity checks ... */
		if ( memcmp(&pdu->magic, &g_magic, SWARM$SZ_MAGIC) )		/* Non-matched magic - just ignore packet */
			continue;


		inet_ntop(AF_INET, &rsock.sin_addr, ipbuf, sizeof(ipbuf));
		$IFTRACE(g_trace, "[#%d] Got PDU from id=%04x  %s:%d, req=%d", pfd.fd, ntohl(pdu->id), ipbuf, ntohs(rsock.sin_port), ntohs(pdu->req));

		switch ( ntohs(pdu->req) )
			{
			case	SWARM$K_REQ_UP:					/* Got : Instance is Up & Running PDU */
				__cb_tbl_creif(pdu, &rsock);			/* Cre/Upd CB instance record */
				break;

			case	SWARM$K_REQ_PARAMS:				/* A set of Parameters from Client */
				__cl_tbl_creif(pdu, &rsock);
				break;

			default:						/* Just ignore unknown/unhandled request */
				continue;
			}
		}


	pthread_exit(NULL);
	return	STS$K_SUCCESS;
}






/*
 *   DESCRIPTION: Handle all incoming packets is comming over the multicast from CB and CL instances.
 *	maintain Client and Control Block tables.
 *
 *    INPUTS:
 *	NONE
 *
 *   OUTPUTS:
 *	NONE
 *
 */

int	th_out	(void *arg)
{
int	rc, temperature, light, i;
SWARM_PDU	pdu = {0};
socklen_t	slen = sizeof(struct sockaddr_in);
const int	pdusz = sizeof(SWARM_PDU);

	for ( i = 0;  !g_exit_flag; i++ )
		{
		$IFTRACE(g_trace, "[#%d] -------------------------------------", g_signet_sd);

		memset(&pdu, 0, sizeof(pdu));
		memcpy(&pdu.magic, g_magic, SWARM$SZ_MAGIC);
		pdu.id = htonl(g_metric);

		/* Form and fill request : "We are UP & Running PDU" */
		pdu.req = htons(SWARM$K_REQ_UP);

		pdu.cb.metric = htonl(g_metric);

		if ( pdusz !=  (rc = sendto(g_signet_sd, &pdu, pdusz, 0, &g_signet_sk, slen)) )
			$LOG(STS$K_ERROR, "[#%d] sendto(%d octets)->%d, errno=%d", g_signet_sd, pdusz, rc, errno);
		else	$IFTRACE(g_trace, "[#%d] Sent UP", g_signet_sd);

		/*
		 * Follow works is performed if this instance in the PRIMARY/BACKUP MASTER mode
		 */
		if ( g_master )
			{
			/* Form and fill request : "Send to CB actual data" */
			pdu.req = htons(SWARM$K_REQ_DATAREQ);

			if (pdusz !=  (rc = sendto(g_signet_sd, &pdu, pdusz, 0, &g_signet_sk, slen)) )
				$LOG(STS$K_ERROR, "[#%d] sendto(%d octets)->%d, errno=%d", g_signet_sd, pdusz, rc, errno);
			else	$IFTRACE(g_trace, "[#%d] Sent DATAREQ", g_signet_sd);


			/* Prepare data to send all clients */
			if ( 1 & __cl_tbl_calc (&temperature, &light) )
				{
				/* Form and fill request : "Display a new set of data" */
				pdu.req = htons(SWARM$K_REQ_SETDATA);

				snprintf(pdu.cl.text, sizeof(pdu.cl.text) - 1, "[#%d] Temp=%d, Light=%d", i, temperature, light);

				clock_gettime(CLOCK_REALTIME, &pdu.cl.tm);
				*((unsigned long long *) &pdu.cl.tm)  = htobe64(*((unsigned long long *) &pdu.cl.tm));

				pdu.cl.temperature = htonl(temperature);
				pdu.cl.light = htonl(light);

				if (pdusz !=  (rc = sendto(g_signet_sd, &pdu, pdusz, 0, &g_signet_sk, slen)) )
					$LOG(STS$K_ERROR, "[#%d] sendto(%d octets)->%d, errno=%d", g_signet_sd, pdusz, rc, errno);
				else	$IFTRACE(g_trace, "[#%d] Sent SETDATA", g_signet_sd);
				}
			}


		/* Sleep for X seconds before next run ... */
		for ( struct timespec tmo = {5, 0}; nanosleep(&tmo, &tmo); );
		}

	pthread_exit(NULL);
	return	STS$K_SUCCESS;
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


	/* Initialize networking stuff ... */
	if ( !(1 & signet_init()) )
		return	-1;


	/* Start threads for send request and process incoming data */
	status = pthread_create(&tid, NULL, th_in, NULL);
	status = pthread_create(&tid, NULL, th_out, NULL);

	/* Loop, eat, sleep ... */
	while ( !g_exit_flag )
		{
		/* Do nothing ... */
		for ( struct timespec tmo = {7, 0}; nanosleep(&tmo, &tmo); );
		}



	$LOG(STS$K_INFO, "Shutdown with exit flag %d", g_exit_flag);
	return(0);
}
