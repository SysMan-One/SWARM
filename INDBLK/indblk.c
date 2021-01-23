#define	__MODULE__	"INDBLK"
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
**  FACILITY:  Swarm Manager, Indication Block
**
**  DESCRIPTION: This module implement an Indication Block functionality:
**	- process incoming request from Control Block
**	- display has been received data
**	- send local metrics (temperature and light) as an answer to the CB's request
**
**
**  BUILD:
**
**  USAGE:
**
**  AUTHORS: Ruslan R. (The BadAss SysMan) Laishev
**
**  CREATION DATE:  18-JAN-2021
**
**  MODIFICATION HISTORY:
**
**	23-JAN-2021	RRL	X.00-03 :  Improved local temperature and light parameters generation.
**
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
#define		__FAC__	"INDBLK"
#define		__TFAC__ __FAC__ ": "
#include	"utility_routines.h"

#include	"swarm_defs.h"				/* General constant and data structures for the Swarm, The */


							/* A storage for global configuration options */
ASC	q_logfspec = {0},
	q_confspec = {0},
	q_signet = {$ASCINI("224.1.1.1:1234")};



volatile int	g_exit_flag = 0,			/* Global flag 'all must to be stop'	*/
	g_trace = 0,					/* A flag to produce extensible logging	*/

	g_logsize = 0;					/* A size of the log file in octets */


int			g_signet_sd = -1;		/* A socket descriptior for the UDP multicasting */
struct sockaddr_in	g_signet_sk = {0};

volatile unsigned	g_instance_id = 0;		/* An Instance ID of CB/IB/Client */


OPTS optstbl [] =
{
	{$ASCINI("config"),	&q_confspec, ASC$K_SZ,	OPTS$K_CONF},

	{$ASCINI("trace"),	&g_trace, 0,		OPTS$K_OPT},
	{$ASCINI("logfile"),	&q_logfspec, ASC$K_SZ,	OPTS$K_STR},
	{$ASCINI("logsize"),	&g_logsize, 0,		OPTS$K_INT},

	{$ASCINI("signet"),	&q_signet, ASC$K_SZ,	OPTS$K_STR},

	OPTS_NULL
};

/* Global Client/Indication Block parameters */
ASC	g_disptext = {0};				/* Last gotten text from CB */
int	g_temperature,					/* Local temperature */
	g_light;					/* Light intensivity */
struct timespec g_tm;					/* Time stamp from CB */




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


	clock_gettime(CLOCK_MONOTONIC, &tp);
	g_instance_id = tp.tv_nsec;

	return	STS$K_SUCCESS;
}



/*
 *
 *  Description: Initalize a socket  and binding it to a given multicast group.
 *
 *  IMLICITE INPUTS:
 *	runparams:	configuration vector
 *
 *  OUTPUTS:
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

int	th_in	(void *arg)
{
int	rc;
struct pollfd pfd = {g_signet_sd, POLLIN, 0};
char	buf[2048], ipbuf[32];
SWARM_PDU	*pdu = (SWARM_PDU *) buf;
struct	sockaddr_in rsock = {0};
int	slen = sizeof(struct sockaddr_in);
const int	pdusz = sizeof(SWARM_PDU);

	while ( !g_exit_flag)
		{
		/* Wait for input packets ... */
		if( 0 >  (rc = poll(&pfd, 1, 3000)) && (errno != EINTR) )
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
		if ( memcmp(&pdu->magic, SWARM$T_MAGIC, SWARM$SZ_MAGIC) )	/* Non-matched magic - just ignore packet */
			continue;

		inet_ntop(AF_INET, &rsock.sin_addr, ipbuf, sizeof(ipbuf));
		$IFTRACE(g_trace, "[#%d] Got PDU from id=%04x %s:%d, req=%d", pfd.fd, ntohl(pdu->id), ipbuf, ntohs(rsock.sin_port), ntohs(pdu->req));

		switch ( ntohs(pdu->req) )
			{
			case	SWARM$K_REQ_DATAREQ:				/* CB request local data to send */
				$LOG(STS$K_INFO, "[#%d] Got DATAREQ from id=%04x %s:%d, req=%d", pfd.fd, ntohl(pdu->id), ipbuf, ntohs(rsock.sin_port), ntohs(pdu->req));

				memset(pdu, 0, sizeof(SWARM_PDU));
				memcpy(pdu->magic, SWARM$T_MAGIC, SWARM$SZ_MAGIC);

				/* Form and fill request : "Our parameters is ! " */
				pdu->req = htons(SWARM$K_REQ_PARAMS);
				pdu->id = htonl(g_instance_id);

				pdu->cl.temperature = htonl(g_temperature);
				pdu->cl.light = htonl(g_light);

				if ( pdusz !=  (rc = sendto(g_signet_sd, pdu, pdusz, 0, &g_signet_sk, slen)) )
					$LOG(STS$K_ERROR, "[#%d] sendto(%d octets)->%d, errno=%d", g_signet_sd, pdusz, rc, errno);

				break;

			case	SWARM$K_REQ_SETDATA:				/* Got data to be display */
				$LOG(STS$K_INFO, "[#%d] Got SETDATA from id=%04x %s:%d, req=%d", pfd.fd, ntohl(pdu->id), ipbuf, ntohs(rsock.sin_port), ntohs(pdu->req));

				__util$str2asc(pdu->cl.text, &g_disptext);
				g_tm.tv_sec = ntohl(pdu->cl.tm.tv_sec);
				g_tm.tv_nsec = ntohl(pdu->cl.tm.tv_nsec);

				break;

			default:
				break;
			}
		}


	pthread_exit(NULL);
	return	STS$K_SUCCESS;
}


int	main	(int argc, char* argv[])
{
int	status, seed, nr;
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

	/* Startthreads for send request and process incoming data */
	status = pthread_create(&tid, NULL, th_in, NULL);


	/* A stuff for random ... */
	nr = seed = getpid();

	srand(seed);
	for (nr &= 0xFFff; nr--;)
		status =  rand();

	/* Loop, eat, sleep ... */
	while ( !g_exit_flag )
		{
		/* Just for demonstration purpose ... */
		g_temperature = rand() % 40;
		g_light = rand() % 99;

		$LOG(STS$K_INFO, "Display='%.*s', Temp=%d(C), Light=%d(Lumen)", $ASC(&g_disptext), g_temperature, g_light);

		for ( struct timespec tmo = {7, 0}; nanosleep(&tmo, &tmo); );
		}


	$LOG(STS$K_INFO, "Shutdown with exit flag %d", g_exit_flag);
	return(0);
}
