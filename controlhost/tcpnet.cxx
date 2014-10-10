/*
** Implements Class ControlHost
**
** $Id: tcpnet.c,v 1.32 2007/12/03 15:45:38 ruud Exp $
*/

#ifdef  VXWORKS
#include "vxWorks.h"
#include "sys/types.h"
#include "sys/socket.h"
#include "netinet/in.h"
#include "arpa/inet.h"
#include "stdio.h"
#include "hostLib.h"
#include "resolvLib.h"
#include "sockLib.h"
#include "ioLib.h"

#ifdef  VX_USE_ZBUF
#include "zbufLib.h"
#include "zbufSockLib.h"
#endif

#else
#include <unistd.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <signal.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#endif

#include "choo.h"

#define NO_DATA_YET     EWOULDBLOCK
#define TEC_SO_BUF      (262144/2)
#define PSIZE           sizeof(PREFIX)
#define MAXLEN          (ControlHost::MAX_DATA)

/* get bsize bytes from socket
**  result:
**  CH_RET_SHUTDOWN - peer shutdown or some system error
**  CH_RET_DEFAULT  - connection is closed
**  0               - transfer is not complete yet, we have to keep trying
**  1               - done with it
*/

  int
ControlHost::getblock (int socket, void *buf, int bsize, int *pos)
{
  int restSize = bsize - *pos;
  int rc;

  Choo.wouldblock = 0;

  while (restSize>0)
  {
    int corrSize = (restSize>MAXLEN)?MAXLEN:restSize;

    rc = recv (socket, ((char *)buf) + *pos, corrSize, 0);
//    if (rc <= 0)
//      printf ("get_block: rc = %d socket: %d *pos: %d size: %d\n", rc, socket, *pos, bsize);

    if (rc == 0)
      return CH_RET_DEFAULT;    // connection is closed
    if (rc < 0)
    {
      if (errno == NO_DATA_YET)
      {
	Choo.wouldblock = 1;
	return 0;       // no data yet
      }
      else if (errno == EINTR)
	continue;
      else
	return CH_RET_SHUTDOWN; // some error occured
    }

    *pos     += rc;
    restSize -= corrSize;

    if (rc < corrSize)
      return 0;  /* transmission incomplete, continue after a while */
  }
  return 1;             // buffer is full
}

/* skip *count bytes from socket
**  result:
**  CH_RET_SHUTDOWN - peer shutdown or some system error
**  CH_RET_DEFAULT  - connection is closed
**      0           - skipping is not complete yet, we have to keep trying
**      1           - done with it
*/
  int
ControlHost::skipblock (int socket, int *count)
{
  Choo.wouldblock = 0;

  while (*count > 0)
  {
    char buf[1000];
    int rc; 
    int corrSize = (*count > (int)sizeof (buf)) ? sizeof (buf) : *count;

    rc = recv (socket, buf, corrSize, 0);
    if (rc == 0)
      return CH_RET_DEFAULT;    // connection is closed

    if (rc < 0)
    {
      if (errno == NO_DATA_YET)
      {
	Choo.wouldblock = 1;
	return 0;       // no data yet
      }
      else if (errno == EINTR)
	continue;
      else
	return CH_RET_SHUTDOWN; // some error occured
    }
    *count -= rc;
    if (rc < corrSize)
       return 0;        // transmission incomplete, continue after a while
  }

  return 1;             // done
}

/* put bsize bytes to socket
**  result:
**  CH_RET_SHUTDOWN - peer shutdown or some system error
**  CH_RET_DEFAULT  - connection is closed
**      0           - transfer is not complete yet, we have to keep trying
**      1           - done with it
*/
  int
ControlHost::putblock (int socket, const void *buf, int bsize, int *pos)
{
  int restSize = bsize - *pos;
  int rc;

// fine SEND_IN_PARALLEL
#ifdef  SEND_IN_PARALLEL
  int fragments = 0;
#endif
  
  Choo.wouldblock = 0;
  
  while (restSize>0)
  {
    int  corrSize = (restSize> MAXLEN)?MAXLEN:restSize;

#if defined (VX_USE_ZBUF) && defined (VXWORKS)
    //  We assume that we can mix send & zbufSockBufSend

    //  ZbufCopyMode is only set in the second half of put_tagged_bfull
    //  which on it's turn is called only by PutFullData & PutFullString.

    if (ZbufCopyMode)
    {
      if (CallBackMode)
      {
	corrSize = restSize;  // The whole message
	set_connection (socket, CONNECTION_WAIT);
	rc = zbufSockBufSend (socket, ((char *)buf) + *pos, corrSize, CallBackFun, CallBackArg, 0);
	set_connection (socket, CONNECTION_NOWAIT);
      } else
      if (WaitDoneMode)
      {
	corrSize = (restSize> TcpSendSpace)?TcpSendSpace:restSize;
	set_connection (socket, CONNECTION_WAIT);
	rc = zbufSockBufSend (socket, ((char *)buf) + *pos, corrSize, CallBackFun, CallBackArg, 0);
	set_connection (socket, CONNECTION_NOWAIT);

#ifdef  SEND_IN_PARALLEL
	fragments++;
#else
	semTake (WaitDoneSem, WAIT_FOREVER);
#endif
      }
      else
	rc = send (socket, ((char *)buf) + *pos, corrSize, 0);
    }
    else
      rc = send (socket, ((char *)buf) + *pos, corrSize, 0);

#else
    rc = send (socket, ((char *)buf) + *pos, corrSize, 0);
#endif

    if (rc == 0)
      return CH_RET_DEFAULT;    // connection is closed

    if (rc < 0)
    {
      if (errno == NO_DATA_YET)
      {
	Choo.wouldblock = 1;
	return 0;       // we have to wait
      }
      else if (errno == EINTR)
	  continue;
      else
      {
	perror ("PERROR Send");
	return CH_RET_SHUTDOWN;
      }
    }
    *pos     += rc;
    restSize -= corrSize;
    if (rc < corrSize)
      return 0;         // transmission incomplete, continue after a while
  }

#ifdef  SEND_IN_PARALLEL
  while (fragments--)
    semTake (WaitDoneSem, WAIT_FOREVER);
#endif

  return 1;             // done
}

  int
ControlHost::getbwait (int socket, void *buf, int bsize)
{
  int rc;
  int swaitdone = 0;
  int pos = 0;

  while ((rc = getblock (socket, buf, bsize, &pos)) == 0)
    if (!swaitdone && Choo.wouldblock)
    {
      set_connection (socket, CONNECTION_WAIT);
      swaitdone = 1;
    }
  if (swaitdone)
    set_connection (socket, CONNECTION_NOWAIT);
  return rc;
}

  int
ControlHost::putbwait (int socket, const void *buf, int bsize)
{
  int rc;
  int swaitdone = 0;
  int pos = 0;

  while ((rc = putblock (socket, buf, bsize, &pos)) == 0)
    if (!swaitdone && Choo.wouldblock)
    {
      set_connection (socket, CONNECTION_WAIT);
      swaitdone = 1;
    }
  if (swaitdone)
    set_connection (socket, CONNECTION_NOWAIT);
  return rc;
}

  int
ControlHost::skipbwait (int socket, int bsize)
{
  int rc;
  int swaitdone = 0;

  while ((rc = skipblock (socket, &bsize)) == 0)
    if (!swaitdone && Choo.wouldblock)
    {
      set_connection (socket, CONNECTION_WAIT);
      swaitdone = 1;
    }
  if (swaitdone)
    set_connection (socket, CONNECTION_NOWAIT);
  return rc;
}

  void
fillprefix (PREFIX *p, const char *tag, int size)
{
  strncpy (p->p_tag, tag, TAGSIZE);
  p->p_size = htonl (size);
}

  void
fromprefix (const PREFIX *p, char *tag, int* size)
{
  int sz = ntohl (p->p_size);

  memcpy (tag, p->p_tag, TAGSIZE);
  tag[TAGSIZE] = 0;
  *size = sz;
}

  int
ControlHost::put_tagged_block (int socket, const char *tag, const void *buf, int size, int *pos)
{
  int rc;
  PREFIX pref;

  if (*pos < (int)PSIZE)
  {
    fillprefix (&pref, tag, size);
    rc = putblock (socket, &pref, PSIZE, pos);
    if (rc <= 0)
      return rc;
  }
  if (size > 0 && *pos < (int)PSIZE + size)
  {
    *pos -= PSIZE;
    rc = putblock (socket, buf, size, pos);
    *pos += PSIZE;
    return rc;
  }
  else
    return 1;
}
 
  int
ControlHost::put_tagged_bwait (int socket, const char *tag, const void *buf, int size)
{
  int rc;
  PREFIX pref;

  fillprefix (&pref, tag, size);
  rc = putbwait (socket, &pref, PSIZE);
  if (rc > 0 && size > 0)
    rc = putbwait (socket, buf, size);
  return rc;
}

// The PutFullData & PutFullString methods will call this routine instead
// of the put_tagged_bwait for eventually set ZbufCopyMode

  int
ControlHost::put_tagged_bfull (int socket, const char *tag, const void *buf, int size)
{
  int rc;
  PREFIX pref;

  fillprefix (&pref, tag, size);
  rc = putbwait (socket, &pref, PSIZE);
  if (rc > 0 && size > 0)
  {

#if defined (VX_USE_ZBUF) && defined (VXWORKS)
    ZbufCopyMode = true;
#endif

    rc = putbwait (socket, buf, size);

#if defined (VX_USE_ZBUF) && defined (VXWORKS)
    ZbufCopyMode = false;
#endif

  }
  return rc;
}

// The PutFullData & PutFullString methods of the ControlShare will call this routine instead
// of the put_tagged_bwait

  int
ControlShare::put_tagged_share (int socket, const char *tag, const void *buf, int size)
{
  int rc;
  PREFIX pref;

#ifdef  VXWORKS
  semTake (SemId, WAIT_FOREVER);
#else
  if (Psem (SemId) == -1)
    fprintf (stderr, "Psem error in ControlShare\n");
#endif

  fillprefix (&pref, tag, size);
  rc = putshare (socket, &pref, PSIZE);
  if (rc > 0 && size > 0)
    rc = putshare (socket, buf, size);

#ifdef  VXWORKS
  semGive (SemId);
#else
  if (Vsem (SemId) == -1)
    fprintf (stderr, "Vsem error in ControlShare\n");
#endif

  return rc;
}

  int
ControlShare::putshare (int socket, const void *buf, int bsize)
{
  int restSize = bsize;
  int restBase = 0;

  while (restSize>0)
  {
    int corrSize = (restSize>MAXLEN)?MAXLEN:restSize;
    int rc = send (socket, (char *)buf+restBase, corrSize, 0);
    if (rc == 0)
      return CH_RET_DEFAULT;    // connection is closed

    if (rc < 0)
    {
      if (errno == EINTR)
	continue;
      else
      {
	perror ("PERROR Send");
	return CH_RET_SHUTDOWN;
      }
    }
    restSize -= corrSize;
    restBase += corrSize;
  }
  return 1;
}


/* Create a server socket on PORT accepting QUEUE_LENGTH connections
*/
    int
create_server_socket (int port, int queue_length)
{
    struct sockaddr_in sa ;
    int s;
    int on;
 
    if ((s = socket (AF_INET, SOCK_STREAM, 0)) < 0)
      return -1 ;
    
    on = 1;
    if (setsockopt (s, SOL_SOCKET, SO_REUSEADDR, (char *)&on, sizeof (on)) < 0)
      perror ("PERROR Setsockopt REUSEADDR");

#ifndef VXWORKS
    on = 1;
    if (setsockopt (s, IPPROTO_TCP, TCP_NODELAY, (char *)&on, sizeof (on)) < 0)
      perror ("PERROR Setsockopt TCP_NODELAY");

    on = TEC_SO_BUF;
    if (setsockopt (s, SOL_SOCKET,  SO_SNDBUF, (char *)&on, sizeof (on)) < 0)
      perror ("PERROR Setsockopt SO_SNDBUF failed");

    on = TEC_SO_BUF;
    if (setsockopt (s, SOL_SOCKET,  SO_RCVBUF, (char *)&on, sizeof (on)) < 0)
      perror ("PERROR Setsockopt SO_RCVBUF");
#endif

    bzero ((char *) &sa, sizeof (sa)) ;
    sa.sin_family = AF_INET ;
    sa.sin_addr.s_addr = htonl (INADDR_ANY) ;
    sa.sin_port = htons ((u_short)port) ;
 
    if (bind (s, (struct sockaddr *) &sa, sizeof (sa)) < 0)
      return -1 ;
    if (listen (s, queue_length) < 0)
      return -1 ;
    return s ;
}
 
// Create a client socket connected to PORT on HOSTNAME
    int
create_client_socket (const char * hostname, int port)
{
    struct sockaddr_in sa ;
    int s ;
    int on;
 
    {
      int ipaddr;

      if (hostname == (char *)0 || strcmp (hostname, "local") == 0)
	ipaddr = INADDR_ANY;
      else if ((ipaddr = ControlHost::IpNumber (hostname)) == 0)
	return -2;

      sa.sin_family      = AF_INET;
      sa.sin_addr.s_addr = ipaddr;
    }

    sa.sin_port = htons ((u_short) port) ;
 
    if ((s = socket (sa.sin_family, SOCK_STREAM, 0)) < 0)
	return -1 ;

#ifndef VXWORKS
    on = 1;
    if (setsockopt (s, IPPROTO_TCP, TCP_NODELAY, (char *)&on, sizeof (on)) < 0)
      perror ("PERROR Setsockopt TCP_NODELAY");

    on = TEC_SO_BUF;
    if (setsockopt (s, SOL_SOCKET,  SO_SNDBUF, (char *)&on, sizeof (on)) < 0)
      perror ("PERROR Setsockopt SO_SNDBUF failed");

    on = TEC_SO_BUF;
    if (setsockopt (s, SOL_SOCKET,  SO_RCVBUF, (char *)&on, sizeof (on)) < 0)
      perror ("PERROR Setsockopt SO_RCVBUF");
#endif

    if (connect (s, (struct sockaddr *) &sa, sizeof (sa)) < 0)
    {
	close (s) ;
	return -1 ;
    }
    on = 1;
    if (setsockopt (s, SOL_SOCKET, SO_KEEPALIVE, (char *)&on, sizeof (on)) < 0)
      perror ("PERROR Setsockopt KEEPALIVE");
    return s;
}

// Accept the connection on listening port
   int
accept_client (int sx, char *host, int *hostid)
{
   /* sx is the port with connection pending
   ** host is the buffer for connected host name
   ** returned value - newly created connected socket
   ** negative value means that connection failed
   */

   int size = sizeof (struct sockaddr_in);
   struct sockaddr_in to;
   int s;
   int on;
 
#ifdef VXWORKS
   s = accept (sx, (struct sockaddr *)&to, &size);
#else
   s = accept (sx, (struct sockaddr *)&to, (socklen_t *)&size);
#endif

   if (s >= 0)
   {
     struct hostent *he = NULL;
     unsigned norder;
 
#ifndef VXWORKS
     he = (struct hostent *) gethostbyaddr ((char *)&to.sin_addr.s_addr, size, AF_INET);
#endif

     *hostid = htonl (to.sin_addr.s_addr);
     if (!he || !he->h_name)
     {
        norder = *hostid;
	sprintf (host, "%u.%u.%u.%u%c",
                            norder >> 24,
                           (norder >> 16) & 0xff,
			   (norder >>  8) & 0xff,
			    norder        & 0xff, 0);
     }
     else
	strcpy (host, he->h_name);

     on = 1;
     if (setsockopt (s, SOL_SOCKET, SO_KEEPALIVE, (char *)&on, sizeof (on)) < 0)
       perror ("PERROR Setsockiot KEEPALIVE");
   }
   else
     *host = 0;
   return s;
}
 
/* Set blocking status of socket
*/
  int
set_connection (int socket, int value)
{
#ifdef VXWORKS
  int flags;
  if (value == CONNECTION_NOWAIT)
    flags = 1;
  else
    flags = 0;
  return ioctl (socket, FIONBIO, (int) &flags);
#else
  int flags = fcntl (socket, F_GETFL, -1);
#ifdef sun
  int mask = O_NONBLOCK;
#else
  int mask = FNDELAY;
#endif
  if (flags == -1)
    return -1;
  if (value == CONNECTION_NOWAIT)
    value = mask;
  if ((flags & mask) == value)
    return 1;
  return fcntl (socket, F_SETFL, flags ^ mask); /* xor */
#endif
}

