/*
** Control Host OO interface implementation.
**
** $Id: choo.c,v 1.72 2008/01/21 11:50:07 ruud Exp $
*/

#undef  SWAP_ON_BIG_ENDIAN

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
#include "errno.h"

#ifdef  VX_USE_ZBUF
#include "zbufLib.h"
#include "zbufSockLib.h"
#endif

#else
#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <sys/time.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>
#endif

#include "choo.h"

bool ControlHost:: Except = false;
bool ControlShare::Except = false;

static const int CONVERT_TO_NULL = -16; // Not really an error

// Return (rc) values
static const int RC_DEFAULT         = {CH_RET_DEFAULT};         // From libch -1
static const int RC_SHUTDOWN        = (CH_RET_SHUTDOWN);        // From libch -2
static const int RC_NO_CONNECTION   = {CH_RET_NO_CONNECTION};   // From libch -3

static const int RC_SELECT_CALL     = (RC_NO_CONNECTION -1);
static const int RC_WRONG_SYNTAX    = (RC_SELECT_CALL   -1);
static const int RC_WRONG_METHOD    = (RC_WRONG_SYNTAX  -1);
static const int RC_WRONG_MALLOC    = (RC_WRONG_METHOD  -1);

static const int SHARE_SEM_KEY      = (0x29121946);

static bool subsSyntax (const char *format);
static bool swapSyntax (const char *format, int argnum);
static int  swap       (const char *format, int argnum, void *dst,
			const void *src, int size);

int  create_server_socket (int port, int queue_length);
int  create_client_socket (const   char *hostname, int port);
int  accept_client        (int sx, char *hostname, int *hostid);


ControlServ::ControlServ (int port)
{
  ServerPort   = port;
  ServerSocket = create_server_socket (port, 1);
}

ControlServ::~ControlServ ()
{
  if (ServerSocket >= 0)
  {
    shutdown (ServerSocket, 2);
    close    (ServerSocket);
  }
}

  ControlHost *
ControlServ::AcceptClient ()
{
  ControlHost *ch = new ControlHost (ServerSocket);
  return (ch);
}

ControlShare::ControlShare (std::string host, std::string nick)
{
  Host = host;
  ShareSocket = create_client_socket (host.c_str(), DISPATCH_PORT);
  if (ShareSocket < 0)
  {
    handleError ("Contructor", Host, "No connection");
    return;
  }

  set_connection (ShareSocket, CONNECTION_WAIT);

  if (nick != "")
  {
    int rc = put_tagged_share (ShareSocket, DISPTAG_MyId, nick.c_str(), strlen (nick.c_str()));
    if (rc < 0)
      handleError ("Contructor", Host, "Error in MyId");
  }
#ifdef  VXWORKS
  SemId = semMCreate (SEM_Q_FIFO|SEM_DELETE_SAFE);
#else
  SemId = Osem (SHARE_SEM_KEY);
#endif
}

ControlShare::~ControlShare ()
{
  if (ShareSocket >= 0)
  {
    shutdown (ShareSocket, 2);
    close    (ShareSocket);
  }
  ShareSocket = -1;

#ifdef  VXWORKS
  (void)semDelete (SemId);
#endif
}

  void
ControlShare::Throw (bool except)
{
  Except = except;
}

  int
ControlShare::Connected (void)
{
  if (ShareSocket >= 0)
    (void)put_tagged_share (ShareSocket, DISPTAG_Version, CHOO_VERSION, strlen (CHOO_VERSION));
  return (ShareSocket);
}

  int
ControlShare::PutFullData (const std::string tag, const void *buf, int nbytes)
{
  if (ShareSocket < 0)
  {
    handleError ("PutFullData/String", Host, tag);
    return (-1);
  }

  int rc = put_tagged_share (ShareSocket, tag.c_str(), buf, nbytes);
  if (rc < 0)
    handleError ("PutFullData/String", Host, tag);
  return (rc);
}

  int
ControlShare::PutFullString (const std::string tag, const std::string str)
{
  return (PutFullData (tag, (const void *)str.c_str(), strlen (str.c_str())));
}

  void
ControlShare::handleError (std::string argProc, std::string argDisp, std::string argAttr)
{
  if (Except)
    throw Exception (argProc, argDisp, argAttr);
}

  ControlHost &
ControlHost::operator<< (TagStream tagstream)
{
  PutFullString (tagstream.tag, Redirect);
  Redirect = "";
  return (*this);
}

  ControlHost &
ControlHost::operator<< (std::string ostr)
{
  Redirect += ostr;
  return (*this);
}

  ControlHost &
ControlHost::operator<< (int oint)
{
  // XXX Dit kan anders
  char        buf [MAX_NAME];
  std::string ostr;

  sprintf (buf, "%d", oint);
  ostr = buf;
  Redirect += ostr;
  return (*this);
}

ControlHost::ControlHost (std::string host, int mode, std::string nickname, std::string tag)
{
  Host          = host;
  Choo.dataline = -1;
  Choo.status   = s_idle;
  Choo.getpos   = 0;
  Choo.putpos   = 0;
  UnlockBase    = 0;

#ifdef  VXWORKS
#ifdef  VX_USE_ZBUF
  CallBackFun   = NULL;
  CallBackArg   = 0;
  ZbufCopyMode  = false;
  CallBackMode  = false;
  WaitDoneMode  = false;
  WaitDoneSem   = 0;
#endif
#endif

  if (host == "")
    return;

  Choo.dataline = create_client_socket (host.c_str(), DISPATCH_PORT);
  if (Choo.dataline < 0)
    handleError (this, BadInit, RC_NO_CONNECTION, "Contructor", host);
  else
    (void) set_connection (Choo.dataline, CONNECTION_NOWAIT);

  int rc = Connected ();
  if (rc < 0)
    handleError (this, BadInit, RC_NO_CONNECTION, "Contructor", host);

  if (nickname != "")
  {
    MyId (nickname);
    if (rc < 0)
      handleError (this, BadMyId, RC_NO_CONNECTION, "Contructor", host);
  }

  if ((mode&CHOO_READ) && (tag != ""))
  {
    rc = Subscribe (tag);
    if (rc < 0)
      handleError (this, BadSubscribe, RC_NO_CONNECTION, "Contructor", host);
  }
  if ((mode&CHOO_READ) && (!(mode&CHOO_NEXT)))
  {
    rc = SendMeAlways ();
    if (rc < 0)
      handleError (this, BadSendMeAlways, RC_NO_CONNECTION, "Contructor", host);
  }
  ZbufWaitDone (true);
}

ControlHost::ControlHost (std::string host)
{
  Host          = host;
  Choo.dataline = -1;
  Choo.status   = s_idle;
  Choo.getpos   = 0;
  Choo.putpos   = 0;
  UnlockBase    = 0;

#ifdef  VXWORKS
#ifdef  VX_USE_ZBUF
  CallBackFun   = NULL;
  CallBackArg   = 0;
  ZbufCopyMode  = false;
  CallBackMode  = false;
  WaitDoneMode  = false;
  WaitDoneSem   = 0;
#endif
#endif

  if (host != "")
  {
    Choo.dataline = create_client_socket (host.c_str(), DISPATCH_PORT);
    if (Choo.dataline < 0)
      handleError (this, BadInit, RC_NO_CONNECTION, "Contructor", host);
    else
      (void) set_connection (Choo.dataline, CONNECTION_NOWAIT);
  }
  ZbufWaitDone (true);
}

ControlHost::ControlHost (std::string host, int port)
{
  Host          = host;
  Choo.dataline = -1;
  Choo.status   = s_idle;
  Choo.getpos   = 0;
  Choo.putpos   = 0;
  UnlockBase    = 0;

#ifdef  VXWORKS
#ifdef  VX_USE_ZBUF
  CallBackFun   = NULL;
  CallBackArg   = 0;
  ZbufCopyMode  = false;
  CallBackMode  = false;
  WaitDoneMode  = false;
  WaitDoneSem   = 0;
#endif
#endif

  Choo.dataline = create_client_socket (host.c_str(), port);
  if (Choo.dataline < 0)
    handleError (this, BadInit, RC_NO_CONNECTION, "Contructor", host);
  else
    (void) set_connection (Choo.dataline, CONNECTION_NOWAIT);
  ZbufWaitDone (true);
}

ControlHost::ControlHost (int socket)
{
  Host          = "server";
  Choo.dataline = -1;
  Choo.status   = s_idle;
  Choo.getpos   = 0;
  Choo.putpos   = 0;
  UnlockBase    = 0;

#ifdef  VXWORKS
#ifdef  VX_USE_ZBUF
  CallBackFun   = NULL;
  CallBackArg   = 0;
  ZbufCopyMode  = false;
  CallBackMode  = false;
  WaitDoneMode  = false;
  WaitDoneSem   = 0;
#endif
#endif

  char clientname [256];    // Not used XXX
  int  clientid;            // Not used XXX

  Choo.dataline = accept_client (socket, clientname, &clientid);
  if (Choo.dataline < 0)
    handleError (this, BadInit, RC_NO_CONNECTION, "Contructor", "server");
  else
    (void) set_connection (Choo.dataline, CONNECTION_NOWAIT);   // XXX
  ZbufWaitDone (true);
}

ControlHost::~ControlHost ()
{
  if (Choo.dataline >= 0)
  {
    shutdown (Choo.dataline, 2);
    close    (Choo.dataline);
  }
  Choo.dataline = -1;
  if (Choo.status == s_free)
    unlock_data ();
  Choo.status = s_idle;
}

  int
ControlHost::Connected (void)
{
  if (Choo.dataline >= 0)
    (void) put_tagged_bwait (Choo.dataline, DISPTAG_Version, CHOO_VERSION, strlen (CHOO_VERSION));
  return (Choo.dataline);
}

  void
ControlHost::Throw (bool except)
{
  Except = except;
}

  int
ControlHost::AddSwapInfo (const std::string tag, const std::string swp)
{
  TS ts;

  if (!SwapSyntax (swp))
    return (handleError (this, BadAddSwapInfo, RC_WRONG_SYNTAX, "AddSwapInfo", swp));

  ts.Tag = tag;
  ts.Swp = swp;

  TagSwapList.push_front (ts);
  return (0);
}

  int
ControlHost::Subscribe (const std::string subscr)
{
  int rc = RC_NO_CONNECTION;

  if (!subsSyntax (subscr.c_str()))
    return (handleError (this, BadSubscribe, RC_WRONG_SYNTAX, "Subscribe", subscr));

  if (Choo.dataline >= 0)
    rc = put_tagged_bwait (Choo.dataline, DISPTAG_Subscribe, subscr.c_str(), strlen (subscr.c_str()));
  return (handleError (this, BadSubscribe, rc, "Subscribe", subscr));
}

  int
ControlHost::SendMeNext ()
{
  int rc = RC_NO_CONNECTION;

  if (Choo.dataline >= 0)
    rc = put_tagged_bwait (Choo.dataline, DISPTAG_Gime, NULL, 0);
  return (handleError (this, BadSendMeNext, rc, "SendMeNext"));
}

  int
ControlHost::SendMeAlways ()
{
  int rc = RC_NO_CONNECTION;

  if (Choo.dataline >= 0)
    rc = put_tagged_bwait (Choo.dataline, DISPTAG_Always, NULL, 0);
  return (handleError (this, BadSendMeAlways, rc, "SendMeAlways"));
}

  int
ControlHost::MyId (const std::string nickname)
{
  int rc = RC_NO_CONNECTION;

  if (Choo.dataline >= 0)
    rc = put_tagged_bwait (Choo.dataline, DISPTAG_MyId,
			     nickname.c_str(), strlen (nickname.c_str()));
  return (handleError (this, BadMyId, rc, "MyId", nickname));
}

  int
ControlHost::CloseAll (const std::string nickname)
{
  int rc = RC_NO_CONNECTION;

  if (Choo.dataline >= 0)
    rc = put_tagged_bwait (Choo.dataline, DISPTAG_CloseAll,
			     nickname.c_str(), strlen (nickname.c_str()));
  return (handleError (this, BadMyId, rc, "CloseAll", nickname));
}

  int
ControlHost::UniqueId (const std::string id)
{
  int rc = RC_NO_CONNECTION;

  if (Choo.dataline >= 0)
    rc = PutFullString (DISPTAG_UniqueId, id);
  return (handleError (this, BadUniqueId, rc, "UniqueId", id));
}

  int
ControlHost::WhereIs (const std::string  disphost, const std::string nickname,
			    std::string &replybuf, int lenrbuf)
{
  char str [MAX_DATA];

  int rc  = whereis (disphost.c_str (), nickname.c_str (), str, lenrbuf);
  if (rc >= 0)
    replybuf = str;
  return (handleError (CHOO_NULL, BadWhereIs, rc, "WhereIs", nickname));
}

  int
ControlHost::WaitHead (std::string &tag, int &nbytes)
{
  char str [MAX_NAME];

  int rc  = headproc (str, &nbytes, 1);
  if (rc >= 0)
    tag = str;
  return (handleError (this, BadWaitHead, rc, "WaitHead"));
}

  int
ControlHost::CheckHead (std::string &tag, int &nbytes)
{
  char str [MAX_NAME];

  int rc  = headproc (str, &nbytes, 0);
  if (rc >= 0)
    tag = str;
  return (handleError (this, BadCheckHead, rc, "CheckHead"));
}

  int
ControlHost::GetFullData (void *data, int nbytes)
{
  int rc = get_data ((char *)data, nbytes);
  return (handleError (this, BadGetFullData, rc, "GetFullData"));
}

  int
ControlHost::GetSwapData (const std::string tag, void *data, int nbytes)
{
  int rc = get_data ((char *)data, nbytes);
  if (rc < 0)
    return(handleError (this, BadGetSwapData, rc, "GetSwapData", tag));

#ifdef  SWAP_ON_BIG_ENDIAN
  if (ntohs (0x1234) == 0x1234)
#else
  if (ntohs (0x1234) != 0x1234)
#endif

  {
    std::list <TS>::iterator index;

    for (index = TagSwapList.begin(); index != TagSwapList.end(); ++index)
      if (index->Tag == tag)
      {
	(void)swap (index->Swp.c_str(), 1, (void *)data, (const void *)data, nbytes);
	break;
      }
  }
  return (rc);
}

  int
ControlHost::GetFullString (std::string &stl)
{

#ifdef __GNU_LIBRARY__
  int  n  = Choo.size_active;
  char str [n+1];
  int  rc = get_string (str, n+1);
#else
  char str [MAX_DATA];
  int  rc = get_string (str, MAX_DATA-1);
#endif

  if (rc >= 0)
    stl = str;
  return (handleError (this, BadGetFullString, rc, "GetFullString"));
}

  int
ControlHost::PutSwapData (const std::string tag, const void *buf, int nbytes)
{

#ifdef __GNU_LIBRARY__
  char lbuf [nbytes];
#else
  char lbuf [MAX_DATA];
#endif

  bool swapped = false;
  int  rc;

  if (Choo.dataline < 0)
    handleError (this, BadPutSwapData, RC_NO_CONNECTION, "PutSwapData", tag);

#ifdef  SWAP_ON_BIG_ENDIAN
  if (ntohs (0x1234) == 0x1234)
#else
  if (ntohs (0x1234) != 0x1234)
#endif

  {
    std::list <TS>::iterator index;

    for (index = TagSwapList.begin(); index != TagSwapList.end(); ++index)
      if (index->Tag == tag)
      {
	(void)swap (index->Swp.c_str(), 1, (void *)lbuf, (const void *)buf, nbytes);
	swapped = true;
	break;
      }
  }

  if (swapped)
    rc = put_tagged_bwait (Choo.dataline, tag.c_str(), lbuf, nbytes);
  else
    rc = put_tagged_bwait (Choo.dataline, tag.c_str(),  buf, nbytes);

  return (handleError (this, BadPutSwapData, rc, "PutSwapData", tag));
}

#define MAXLEN  (ControlHost::MAX_DATA)

  int
ControlHost::PutFullDaq (const std::string tag, const void *buf, int nbytes)
{
  int    rc     = RC_NO_CONNECTION;
  int    socket = Choo.dataline;
  char  *head   = (char *)buf- sizeof (PREFIX);

  if (socket < 0)
    return (handleError (this, BadPutFullData, rc, "PutFullDaq/No sock", tag));

  set_connection (socket, CONNECTION_WAIT);

  fillprefix ((PREFIX *)head, tag.c_str(), nbytes);
  rc = newblock (socket, head, nbytes + sizeof (PREFIX));
  if (rc < 0)
    return (handleError (this, BadPutFullData, rc, "PutFullDaq", tag));

  set_connection (socket, CONNECTION_NOWAIT);
  return 1;
}

  int
ControlHost::PutFullDcs (const std::string tag, const void *buf, int nbytes)
{
  int    rc     = RC_NO_CONNECTION;
  int    socket = Choo.dataline;
  char  *head;

  if (socket < 0)
    return (handleError (this, BadPutFullData, rc, "PutFullDcs/No sock", tag));

  set_connection (socket, CONNECTION_WAIT);

  head = (char *)malloc (nbytes + sizeof (PREFIX));
  memcpy     (head + sizeof (PREFIX), buf, nbytes);
  fillprefix ((PREFIX *)head, tag.c_str(), nbytes);
  rc = newblock (socket, head, nbytes + sizeof (PREFIX));
  free (head);
  if (rc < 0)
    return (handleError (this, BadPutFullData, rc, "PutFullDcs", tag));

  set_connection (socket, CONNECTION_NOWAIT);
  return 1;
}

  int
ControlHost::PutFullDaq (const std::string tag, const std::string str)
{
  return (PutFullDaq (tag, (const void *)str.c_str(), strlen (str.c_str())));
}

  int
ControlHost::PutFullDcs (const std::string tag, const std::string str)
{
  return (PutFullDcs (tag, (const void *)str.c_str(), strlen (str.c_str())));
}

  int
ControlHost::newblock (int socket, const void *buf, int bsize)
{
  int  restSize = bsize;
  int  restBase = 0;
  int  rc;

  if (bsize == 0)
    return (1);

  while (restSize>0)
  {
    int  corrSize = (restSize> MAXLEN)?MAXLEN:restSize;

#ifdef  VXWORKS
    if (WaitDoneMode)
    {
      corrSize = (restSize> TcpSendSpace)?TcpSendSpace:restSize;
      rc = zbufSockBufSend (socket, ((char *)buf) + restBase, corrSize, CallBackFun, CallBackArg, 0);
      semTake (WaitDoneSem, WAIT_FOREVER);
    }
    else
      rc = send (socket, ((char *)buf) + restBase, corrSize, 0);
#else
      rc = send (socket, ((char *)buf) + restBase, corrSize, 0);
#endif

    if (rc != corrSize)
    {
      if (rc == 0)
      {
	printf ("Send returned zero\n");
//      printf ("restSize: %d corrSize: %d rc: %d\n", restSize, corrSize, rc);
	return CH_RET_DEFAULT;  // connection is closed
      }
      if (rc < 0)
      {
	if (errno == EINTR)
	  continue;
	else
	{
	  perror ("PERROR Send returned negative value");
//        printf ("restSize: %d corrSize: %d rc: %d\n", restSize, corrSize, rc);
	  return CH_RET_SHUTDOWN;
	}
      }
      printf ("Send returned less then asked\n");
//    printf ("restSize: %d corrSize: %d rc: %d\n", restSize, corrSize, rc);
    }
    restBase += rc;
    restSize -= rc;
  }
  return 1;
}

  int
ControlHost::PutFullData (const std::string tag, const void *buf, int nbytes)
{
// Back to hybride
//#ifdef  VXWORKS
//  return (PutFullDcs (tag, (const void *)buf, nbytes));
//#else
  int rc = RC_NO_CONNECTION;

  if (Choo.dataline >= 0)
    rc = put_tagged_bfull (Choo.dataline, tag.c_str(), buf, nbytes);
  return (handleError (this, BadPutFullData, rc, "PutFullData/String", tag));
//#endif
}

  int
ControlHost::PutFullString (const std::string tag, const std::string str)
{
  return (PutFullData (tag, (const void *)str.c_str(), strlen (str.c_str())));
}

  int
ControlHost::PutPartData (const std::string  tag, const void *buf, int lenbuf, int &pos)
{
  int rc = RC_NO_CONNECTION;

  if (Choo.dataline >= 0)
    rc = put_tagged_block (Choo.dataline, tag.c_str(), (const void *)buf, lenbuf, &pos);
  return (handleError (this, BadPutPartData, rc, "PutPartData/String", tag));
}

  int
ControlHost::PutPartData (const std::string  tag, const void *buf, int lenbuf)
{
  int rc = RC_NO_CONNECTION;

  if (Choo.dataline >= 0)
    rc = put_tagged_block (Choo.dataline, tag.c_str(), (const void *)buf, lenbuf, &Choo.putpos);
  if (rc)
    Choo.putpos = 0;
  return (handleError (this, BadPutPartData, rc, "PutPartData/String", tag));
}

  int
ControlHost::PutPartString (const std::string tag, const std::string str, int &pos)
{
  return (PutPartData (tag, (const void *)str.c_str(), strlen (str.c_str()), pos));
}

  int
ControlHost::PutPartString (const std::string tag, const std::string str)
{
  return (PutPartData (tag, (const void *)str.c_str(), strlen (str.c_str())));
}

  ControlHost *
ControlHost::SelectRead (int timeout,  ...)
{
  int             rc;
  va_list         args;
  fd_set          sockfds;
  struct timeval  seltime;
  ControlHost    *ch [MAX_CONN];

  FD_ZERO (&sockfds);

  va_start (args, timeout);

  int ind = 0;
  while ((ch [ind] = va_arg (args, ControlHost *)))
  {
    if (ch [ind]->Choo.dataline < 0)
      return ((ControlHost *)(long)handleError (CHOO_NULL, BadSelectRead, RC_NO_CONNECTION, "SelectRead", "Bad Arguments"));

    FD_SET (ch [ind]->Choo.dataline, &sockfds);
    ind++;
  }
  va_end (args);
  int max = ind;

again:
  seltime.tv_sec  = 0;
  seltime.tv_usec = timeout;

  if (timeout == -1)
    rc = select (MAX_CONN, &sockfds, NULL, NULL, NULL);
  else
    rc = select (MAX_CONN, &sockfds, NULL, NULL, &seltime);

  if (rc < 0)
  {
    if (errno == EINTR)
      goto again;
    return ((ControlHost *)(long)handleError (CHOO_NULL, BadSelectRead, RC_SELECT_CALL, "SelectRead"));
  }

  for (ind=0; ind<max; ind++)
    if (FD_ISSET(ch [ind]->Choo.dataline, &sockfds))
      return (ch [ind]);

  return (NULL);
}

  ControlHost *
ControlHost::SelectWrite (int timeout,  ...)
{
  int             rc;
  va_list         args;
  fd_set          sockfds;
  struct timeval  seltime;
  ControlHost    *ch [MAX_CONN];

  FD_ZERO (&sockfds);

  va_start (args, timeout);

  int ind = 0;
  while ((ch [ind] = va_arg (args, ControlHost *)))
  {
    if (ch [ind]->Choo.dataline < 0)
      return ((ControlHost *)(long)handleError (CHOO_NULL, BadSelectWrite, RC_NO_CONNECTION, "SelectWrite", "Bad Arguments"));

    FD_SET (ch [ind]->Choo.dataline, &sockfds);
    ind++;
  }
  va_end (args);
  int max = ind;

again:
  seltime.tv_sec  = 0;
  seltime.tv_usec = timeout;

  if (timeout == -1)
    rc = select (MAX_CONN, NULL, &sockfds, NULL, NULL);
  else
    rc = select (MAX_CONN, NULL, &sockfds, NULL, &seltime);

  if (rc < 0)
  {
    if (errno == EINTR)
      goto again;
    return ((ControlHost *)(long)handleError (CHOO_NULL, BadSelectWrite, RC_SELECT_CALL, "SelectWrite", "Bad Select"));
  }

  for (ind=0; ind<max; ind++)
    if (FD_ISSET(ch [ind]->Choo.dataline, &sockfds))
      return (ch [ind]);

  return (NULL);
}

  ControlHost *
ControlHost::SelectRead (int timeout, ControlHost *selch [])
{
  int             rc;
  fd_set          sockfds;
  struct timeval  seltime;

  FD_ZERO (&sockfds);

  int ind = 0;
  while (selch [ind])
  {
    if (selch [ind]->Choo.dataline < 0)
      return ((ControlHost *)(long)handleError (CHOO_NULL, BadSelectRead, RC_NO_CONNECTION, "SelectRead", "Bad Arguments"));

    FD_SET (selch [ind]->Choo.dataline, &sockfds);
    ind++;
  }
  int max = ind;

again:
  seltime.tv_sec  = 0;
  seltime.tv_usec = timeout;

  if (timeout == -1)
    rc = select (MAX_CONN, &sockfds, NULL, NULL, NULL);
  else
    rc = select (MAX_CONN, &sockfds, NULL, NULL, &seltime);

  if (rc < 0)
  {
    if (errno == EINTR)
      goto again;
    return ((ControlHost *)(long)handleError (CHOO_NULL, BadSelectRead, RC_SELECT_CALL, "SelectRead"));
  }

  for (ind=0; ind<max; ind++)
    if (FD_ISSET(selch [ind]->Choo.dataline, &sockfds))
      return (selch [ind]);

  return (NULL);
}

  ControlHost *
ControlHost::SelectWrite (int timeout, ControlHost *selch [])
{
  int             rc;
  fd_set          sockfds;
  struct timeval  seltime;

  FD_ZERO (&sockfds);

  int ind = 0;
  while (selch [ind])
  {
    if (selch [ind]->Choo.dataline < 0)
      return ((ControlHost *)(long)handleError (CHOO_NULL, BadSelectWrite, RC_NO_CONNECTION, "SelectWrite", "Bad Arguments"));

    FD_SET (selch [ind]->Choo.dataline, &sockfds);
    ind++;
  }
  int max = ind;

again:
  seltime.tv_sec  = 0;
  seltime.tv_usec = timeout;

  if (timeout == -1)
    rc = select (MAX_CONN, NULL, &sockfds, NULL, NULL);
  else
    rc = select (MAX_CONN, NULL, &sockfds, NULL, &seltime);

  if (rc < 0)
  {
    if (errno == EINTR)
      goto again;
    return ((ControlHost *)(long)handleError (CHOO_NULL, BadSelectWrite, RC_SELECT_CALL, "SelectWrite"));
  }

  for (ind=0; ind<max; ind++)
    if (FD_ISSET(selch [ind]->Choo.dataline, &sockfds))
      return (selch [ind]);

  return (NULL);
}

  int
ControlHost::Swap (const std::string format, void *dst, const void *src, int usize)
{
  return (swap (format.c_str(), 1, dst, src, usize));
}

    bool
ControlHost::SwapSyntax (const std::string format)
{
  return (swapSyntax (format.c_str(), 1));
}

  int
ControlHost::IpNumber (const std::string hostname)
{
  int  ipnumber = 0;
  char host [MAX_NAME];

  strcpy (host, hostname.c_str());

  if (hostname == "")
  {
    if (gethostname (host, sizeof (host)) != 0)
      return (0);
#ifdef  VXWORKS
    return (hostGetByName (host));
#endif
  }

#ifdef  sun
  // Because saclay sun doen't like gethostbyname
  if ((ipnumber = inet_addr (host)) == -1)
    ipnumber = 0;
#else
#ifdef  VXWORKS
  if ((ipnumber = inet_addr (host)) == -1)
    ipnumber = 0;
#else
  struct hostent *hp;

  if ((hp = gethostbyname (host)) != NULL)
    memcpy ((char *)&ipnumber, hp->h_addr, sizeof (int));
#endif
#endif

  return (ipnumber);
}

  std::string
ControlHost::IpDecimalDot (int ip)
{
  char host [MAX_NAME];

  if (ntohs (0x1234) == 0x1234)
    sprintf (host, "%u.%u.%u.%u%c",
      (ip >> 24) & 0xff, (ip >> 16) & 0xff, (ip >>  8) & 0xff, (ip >>  0) & 0xff, 0);
  else
    sprintf (host, "%u.%u.%u.%u%c",
      (ip >>  0) & 0xff, (ip >>  8) & 0xff, (ip >> 16) & 0xff, (ip >> 24) & 0xff, 0);

  std::string res = host;
  return (res);
}

/*
**************** Static routines *****************
*/

  static void
swapDouble (double *to, double *from, int size)
  // Copy n bytes as extra large
  // NOTE: from and to may be equal!
{
  struct swapdouble
  {
    char  byte_zero;
    char  byte_one;
    char  byte_two;
    char  byte_three;
    char  byte_four;
    char  byte_five;
    char  byte_six;
    char  byte_seven;
  };

  union double_byte
  {
    double sw_double;
    char   sw_byte[8];
  } double_byte;

  size >>= 3;

  if (size > 0)
  do
  {
    double_byte.sw_double  = *from;
    ((struct swapdouble *)to)->byte_zero  = double_byte.sw_byte[7];
    ((struct swapdouble *)to)->byte_one   = double_byte.sw_byte[6];
    ((struct swapdouble *)to)->byte_two   = double_byte.sw_byte[5];
    ((struct swapdouble *)to)->byte_three = double_byte.sw_byte[4];
    ((struct swapdouble *)to)->byte_four  = double_byte.sw_byte[3];
    ((struct swapdouble *)to)->byte_five  = double_byte.sw_byte[2];
    ((struct swapdouble *)to)->byte_six   = double_byte.sw_byte[1];
    ((struct swapdouble *)to)->byte_seven = double_byte.sw_byte[0];

    from++; to++;
  } while(--size);
}

  static void
swapLong (int *to, int *from, int size)
  // Copy n bytes as integers
  // NOTE: from and to may be equal!
{
  struct swaplong
  {
    char  byte_zero;
    char  byte_one;
    char  byte_two;
    char  byte_three;
  };

  union long_byte
  {
    int   sw_long;
    char  sw_byte[4];
  } long_byte;

  size >>= 2;

  if (size > 0)
  do
  {
    long_byte.sw_long  = *from;
    ((struct swaplong *)to)->byte_zero  = long_byte.sw_byte[3];
    ((struct swaplong *)to)->byte_one   = long_byte.sw_byte[2];
    ((struct swaplong *)to)->byte_two   = long_byte.sw_byte[1];
    ((struct swaplong *)to)->byte_three = long_byte.sw_byte[0];
    from++; to++;
  } while(--size);
}

  static void
swapShort (short *to, short *from, int size)
  // Copy n bytes as shorts
  // NOTE: from and to may be equal!
{
  struct swapshort
  {
    char  byte_zero;
    char  byte_one;
  };

  union short_byte
  {
    short sw_short;
    char  sw_byte[2];
  } short_byte;

  size >>= 1;

  if (size > 0)
  do
  {
    short_byte.sw_short = *from;
    ((struct swapshort *)to)->byte_zero  = short_byte.sw_byte[1];
    ((struct swapshort *)to)->byte_one   = short_byte.sw_byte[0];
    from++; to++;
  } while(--size);
}

    static int
strip (const char *format, char* simp, char *rest)
{
    //   Format starts with a (
    // - Search corresponding )
    // - Strip the () and output inner string via simp
    // - Read number after stripped ) => return value
    // - Output rest of the format via rest

    char  temp [ControlHost::MAX_NAME];
    int   cnt = 0;
    int   num = -1;

    strcpy (temp, ++format);
    char *f = temp;

    while (char c = *f)
    {
      if (c == '(')
	cnt++;
      if (c == ')')
	if (cnt-- == 0)
	  break;
      f++;
    }

    *f++ = 0;
    strcpy (simp, temp);

    while (!isdigit ((int)*f)) f++;
    sscanf (f, "%d", &num);
    while ( isdigit ((int)*f)) f++;

    if (*f)
      strcpy (rest, f);
    else
      strcpy (rest, "");

    return (num);
}


    static int
swap (const char *format, int argnum, void *dst, const void *src, int usize)
{
    // Swap the src buffer of usize conform the format string to the dst buffer.
    // The parmeters dst & src may be the same.
    // In general argnum is the number of times the format is applied.
    // In the most outer loop the argnum will always be one

	  char *optr = (      char *)dst;
    const char *iptr = (const char *)src;

    char simp [ControlHost::MAX_NAME];
    char rest [ControlHost::MAX_NAME];
    int  times;
    int  tsize;
    int  ssize = 0;

    static int  recurs = 0;     // A pitty we need this XXX
    static char last   = 0;     // To be changed        XXX

    while (isspace ((int)*format)) format++;
    if (!*format)
	return (ssize);

    recurs++;

    if (*format == '(')
    {
      times = strip (format, simp, rest);
      for (int i=0; i<argnum; i++)
      {
	tsize  = swap (simp, times, optr, iptr, usize);
	ssize += tsize;
	optr  += tsize;
	iptr  += tsize;
	tsize  = swap (rest,     1, optr, iptr, usize);
	ssize += tsize;
	optr  += tsize;
	iptr  += tsize;
      }
    }
    else
    {
      char c = *format++;
      int  n;

      while  (isspace  ((int)*format)) format++;

      if (*format)
      {
	last = 0;

	sscanf (format, "%d", &n);
	while  ( isdigit ((int)*format)) format++;

	for (int i=0; i<argnum; i++)
	{
	  // printf ("swap: %c Times: %3d\n", c, n);    XXX
	  switch (c)
	  {
	    default:
	      fprintf (stderr, "Wrong data type %c in swap format string\n", c);
	      recurs--;
	      return (0);
	      break;

	    case 'x': case 'X':
	    case 'd': case 'D':
	      tsize = sizeof (double) * n;
	      swapDouble ((double *)optr, (double *)iptr, tsize);
	      break;

	    case 'l': case 'L':
	    case 'f': case 'F':
	      tsize = sizeof (long ) * n;
	      swapLong  ((int *)optr, (int *)iptr, tsize);
	      break;

	    case 's': case 'S':
	      tsize = sizeof (short) * n;
	      swapShort ((short *)optr, (short *)iptr, tsize);
	      break;

	    case 'c': case 'C':
	      tsize = sizeof (char ) * n;
	      if (optr != iptr)
		memcpy (optr, iptr, tsize);
	      break;
	  }

	  ssize += tsize;
	  optr  += tsize;
	  iptr  += tsize;
	  tsize  = swap (format, 1, optr, iptr, usize);
	  ssize += tsize;
	  optr  += tsize;
	  iptr  += tsize;
	}
      }
      else
	last = c;
    }

    // On the first level and there is a last type,
    // so convert the rest of the message.

    if ((recurs == 1) && last)
    {
      char fmt [ControlHost::MAX_NAME];
      int  div = sizeof (int);

      switch (last)
      {
	case 'd': case 'D':   div = sizeof (double);  break;
	case 'x': case 'X':   div = sizeof (double);  break;
	case 'f': case 'F':   div = sizeof (long);    break;
	case 'l': case 'L':   div = sizeof (long);    break;
	case 's': case 'S':   div = sizeof (short);   break;
	case 'c': case 'C':   div = sizeof (char);    break;
      }
      // Building the last format string
      sprintf (fmt, "%c%d", last, (usize-ssize)/div);

      tsize  = swap (fmt, 1, optr, iptr, usize-ssize);
      ssize += tsize;
    }
    recurs--;
    return (ssize);
}

    static bool
swapSyntax (const char *format, int argnum)
{
    char simp [ControlHost::MAX_NAME];
    char rest [ControlHost::MAX_NAME];
    int  times;

    while (isspace ((int)*format)) format++;
    if (!*format)
	return (true);

    if (*format == '(')
    {
      times = strip (format, simp, rest);
      if (times <= 0)
	return (false);

      for (int i=0; i<argnum; i++)
      {
	if (!swapSyntax (simp, times))
	  return (false);

	if (!swapSyntax (rest, 1))
	  return (false);
      }
    }
    else
    {
      char c = *format++;
      int  n;

      switch (c)
      {
	default:
	  fprintf (stderr, "Wrong data type %c in swap format string\n", c);
	  return (false);
	  break;

	case 'd':   case 'x':   case 'f':   case 'l':   case 's':   case 'c':
	case 'D':   case 'X':   case 'F':   case 'L':   case 'S':   case 'C':
	  break;
      }

      while  (isspace  ((int)*format)) format++;

      if (*format)
      {
	if (sscanf (format, "%d", &n) != 1)
	  return (false);

	while  ( isdigit ((int)*format)) format++;

	for (int i=0; i<argnum; i++)
	  if (!swapSyntax (format, 1))
	    return (false);
      }
    }
    return (true);
}

  static bool
subsSyntax (const char *format)
{
  if (!*format)
    return (true);

  while (*format)
  {
    while (isspace ((int)*format)) format++;
    if (!*format)
	return (true);

    while (isalpha ((int)*format))
    {
      switch (*format)
      {
	default:
	  return (false);
	case 'a': case 's': case 'm': case 'w':
//      case 'A': case 'S': case 'M': case 'W':
	  break;
      }
      format++;
    }

    while (isspace ((int)*format)) format++;
    if (!*format)
	return (false);

    while (*format && !isspace ((int)*format)) format++;
  }

  return (true);
}

  int
ControlHost::headproc (char *tag, int *size, int wait)
{
  int rc  = 0;

  if (Choo.dataline < 0)
    return RC_NO_CONNECTION;

  if (Choo.status != s_idle)
  {
    if (Choo.size_active == 0)
      Choo.status = s_idle;
    else
      return RC_DEFAULT; // we have to finish with the previous data
  }

  do
  {
    if (wait)
      rc = getbwait (Choo.dataline, ((char *) &Choo.pref) + Choo.getpos, sizeof (PREFIX) - Choo.getpos);
    else
    {
      int setrc = set_connection (Choo.dataline, CONNECTION_NOWAIT);
      rc = getblock (Choo.dataline, &Choo.pref, sizeof (PREFIX), &Choo.getpos);
      if (setrc == 0)
	set_connection (Choo.dataline, CONNECTION_WAIT);
    }
    if (rc > 0)
    {
      Choo.getpos = 0;
      fromprefix (&Choo.pref, tag, &Choo.size_active);
      rc = 1;
    }

    if (rc < 0)
      return RC_DEFAULT;
    else if (rc == 0 && !wait)
      return 0;
  } while (rc <= 0);

  Choo.status = s_data;

  if (Choo.size_active < 0)
  {
    Choo.data_ptr = getShared (Choo.dataline);

    if (Choo.data_ptr == NULL)
    {
       Choo.status = s_idle;
       return RC_DEFAULT;
    }
    Choo.status      = s_free;
    Choo.real_unlock = 1;
    Choo.size_active = -Choo.size_active;
  }
  *size = Choo.size_active;
  return 1;
}
 
  int
ControlHost::get_data (void *buf, int lim)
{
  int rc;

  if (Choo.status == s_idle)
    return RC_DEFAULT; // no header yet

  if (lim < 0)
    return RC_DEFAULT;

  if (Choo.status == s_free)
  {
    if (lim > Choo.size_active)
	lim = Choo.size_active;

    memcpy (buf, Choo.data_ptr, lim);

    rc = UnlockData (Choo.data_ptr);
    if (rc < 0)
      return (-1);
    Choo.status = s_idle;
    return 1;
  }

  if (Choo.size_active == 0)
  {
    Choo.status = s_idle;
    return 1;   // there is nothing to get, so we already got it
  }

  if (lim > Choo.size_active)
      lim = Choo.size_active;
 
  if (lim > 0)
    rc = getbwait (Choo.dataline, buf, lim);
  else
    rc = 1;

  if (rc > 0 && lim < Choo.size_active)
    rc = skipbwait (Choo.dataline, Choo.size_active-lim);

  Choo.status = s_idle;
  return rc;
}
 
  int
ControlHost::get_string (char *buf, int lim)
{
  if (lim > Choo.size_active)
      lim = Choo.size_active+1;
  if (lim > 0)
    lim--;
  if (lim >= 0)
    buf[lim] = '\0';

  int rc = get_data (buf, lim);
  if (rc <= 0)
    return rc;
  return 1;
}
 
  void
ControlHost::unlock_data (void)
{
  if (Choo.status == s_free)
  {
    Choo.status = s_idle;

    if (Choo.real_unlock)
    {
      int rc = UnlockData (Choo.data_ptr);
      if (rc < 0)
	fprintf (stderr, "Error LigierUnlock in unlock_data\n");
      return;
    }

    if (Choo.data_ptr == NULL)
      fprintf (stderr, "Error zero data pointer in unlock_data\n");
    free   (Choo.data_ptr);
  }
}
 
#define NEW_WHEREIS
#ifdef  NEW_WHEREIS
  int
ControlHost::whereis (const char *host, const char *id, char *reply, int maxreplen)
{
  ControlHost *ch      = new ControlHost (host);
  int          tmpsock = ch->Connected ();
  int  rc;

  if (maxreplen > 0)
    *reply = 0;

  if (tmpsock < 0)
    return -1;

  rc = ch->put_tagged_bwait (tmpsock, DISPTAG_WhereIs, id, strlen (id));
  if (rc < 0)
  {
    delete (ch);
    return -1;
  }

  if (maxreplen >= (int)(MAX_NAME - sizeof (PREFIX)))
      maxreplen  = (int)(MAX_NAME - sizeof (PREFIX));

  maxreplen--;

  {
    char  dumstr [MAX_NAME];
    char *dumptr = dumstr;
    PREFIX pref;

    rc = ch->getbwait (tmpsock, &pref, sizeof (PREFIX));
    if (rc <= 0)
    {
      delete (ch);
      return -1;
    }

    if (strncmp (pref.p_tag, DISPTAG_WhereIs, TAGSIZE))
    {
      delete (ch);
      return -1;
    }

    if (ntohl(pref.p_size) == 0)
    {
      delete (ch);
      return false;
    }

    rc = ch->getbwait (tmpsock, dumstr, ntohl(pref.p_size));
    if (rc <= 0)
    {
      delete (ch);
      return -1;
    }
    strncpy (reply, dumptr, ntohl(pref.p_size));
  }
  delete (ch);
  return true;
}

#else

  int
ControlHost::whereis (const char *host, const char *id, char *reply, int maxreplen)
{
  ControlHost *ch = new ControlHost (host);
  int  tmpsock = ch->Connected ();

  bool exists  = false;
  int  rc;

  if (maxreplen > 0)
    *reply = 0;
 
  if (tmpsock < 0)
    return -1;
  
  rc = ch->put_tagged_bwait (tmpsock, DISPTAG_WhereIs, id, strlen (id));
  if (rc < 0)
  {
    delete (ch);
    return -1;
  }

  if (maxreplen >= (int)(MAX_DATA - sizeof (PREFIX)))
      maxreplen  = (int)(MAX_DATA - sizeof (PREFIX));

  maxreplen--;

  {
    int   dummy;
    char  dumstr [MAX_DATA];
    char *dumptr = dumstr;
    bool  newDisp = false;

    set_connection (tmpsock, CONNECTION_WAIT);
    for (rc=1; maxreplen > 0 && (rc=read (tmpsock, dumptr, maxreplen)) > 0;
	maxreplen -= rc,
	dumptr    += rc,
	exists     = true,
       *dumptr = 0)
    {
      int rcl = rc;
      if (rcl > TAGSIZE)
	  rcl = TAGSIZE;

      if (!strncmp (dumstr, DISPTAG_WhereIs, rcl))
      {
	newDisp = true;
	if (rc > TAGSIZE)
	  exists = true;
	else
	  exists = false;
	rc = 0;
	break;
      }
    }

    if (rc > 0)
      while ((rc=read (tmpsock, (char *)&dummy, 1)) > 0)
	exists = true;

    dumptr = dumstr;
    if (newDisp)
      dumptr += sizeof (PREFIX);

    strncpy (reply, dumptr, maxreplen);
  }
  delete (ch);
  return exists;
}
#endif

  int
ControlHost::handleError (ControlHost  *argThis,   ErrorType argType,
	 int argCode, const std::string argProc, std::string argAttr)
{
  if (argCode >= 0)
    return (argCode);

  if (argCode==CONVERT_TO_NULL) {
    argCode = 0;
  } else if (argCode==RC_SELECT_CALL) {
    argAttr += " Error in select call";
  } else if (argCode==RC_NO_CONNECTION) {
    argAttr += " No connection";
  } else if (argCode==RC_WRONG_SYNTAX) {
    argAttr += " Wrong syntax";
  } else if (argCode==RC_WRONG_MALLOC) {
    argAttr += " Wrong malloc";
  }

  if (Except)
    throw Exception (argThis, argType, argCode, argProc, argAttr);
  return (argCode);
}

#ifdef  VXWORKS
#ifdef  VX_USE_ZBUF
  void
ControlHost::ZbufCallBack (VOIDFUNCPTR fun, int arg)
{
  if (fun == NULL)
  {
    CallBackFun  = fun;
    CallBackArg  = arg;
    CallBackMode = false;
    WaitDoneMode = false;
  }
  else
  {
    CallBackFun  = fun;
    CallBackArg  = arg;
    CallBackMode = true;
    WaitDoneMode = false;
  }
}

  void
ControlHost::ZbufWaitDone (bool on)
{
  if (WaitDoneSem)
  {
    (void)semDelete (WaitDoneSem);
    WaitDoneSem = 0;
  }

  if (on)
  {
    WaitDoneSem = semCCreate (SEM_Q_FIFO, 0);
    if (WaitDoneSem == NULL)
    {
      fprintf (stderr, "Error in semCCreate\n");
      return;
    }
    CallBackFun  = (VOIDFUNCPTR)callBack;
    CallBackArg  = (int)WaitDoneSem;

    CallBackMode = false;
    WaitDoneMode = true;

    extern int     tcp_sendspace;
    TcpSendSpace = tcp_sendspace;
  }
  else
  {
    CallBackFun  = (VOIDFUNCPTR)0;
    CallBackArg  = (int)0;
    WaitDoneMode = false;
    CallBackMode = false;
  }
}

  void
callBack (caddr_t buf, int argsem)
{
  semGive ((SEM_ID)argsem);
}
#endif

  void
usleep (unsigned microsec)
{
  struct timeval timeout;

  timeout.tv_sec  = microsec/1000000;
  timeout.tv_usec = microsec%1000000;

  select (0, NULL, NULL, NULL, &timeout);
}
#else
  void
ControlHost::ZbufWaitDone (bool on)
{
}
#endif

std::ostream  &operator<< (std::ostream &s, ControlHost::Exception &error)
{
  return (s << "ControlHost  exception (" << error.attr << ") in method: " << error.proc << " ligier: " << error.disp);
}

std::string  &operator<< (std::string &s, ControlHost::Exception &error)
{
  return (s = "ControlHost  exception (" + error.attr + ") in method: " + error.proc + " ligier: " + error.disp);
}

std::ostream  &operator<< (std::ostream &s, ControlShare::Exception &error)
{
  return (s << "ControlShare exception (" << error.attr << ") in method: " << error.proc << " ligier: " << error.disp);
}

std::string  &operator<< (std::string  &s, ControlShare::Exception &error)
{
  return (s = "ControlShare exception (" + error.attr + ") in method: " + error.proc + " ligier: " + error.disp);
}

#ifndef VXWORKS
#include <sys/ipc.h>
#include <sys/sem.h>

#if (defined(__GNU_LIBRARY__) && (!defined(_SEM_SEMUN_UNDEFINED))) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__MACH__)
/* union semun is defined by including <sys/sem.h> */
#else
/* according to X/OPEN we have to define it ourselves */
union semun
{
    int                 val;    /* value  for SETVAL            */
    struct   semid_ds  *buf;    /* buffer for IPC_STAT, IPC_SET */
    unsigned short int *array;  /* array  for GETALL, SETALL    */
    struct   seminfo   *__buf;  /* buffer for IPC_INFO          */
};
#endif

    int
ControlShare::Osem (int key)
{
    union semun arg ;
    int         sid;

    sid = semget ((key_t)key, 1, IPC_CREAT | 0666);
    arg.val = 1;
    semctl (sid, 0, SETVAL, arg);
    return (sid);
}

    int
ControlShare::Psem (int sid)
{
    int           ret;
    struct sembuf sb ;

    sb.sem_num =  0;
    sb.sem_op  = -1;
    sb.sem_flg = SEM_UNDO;

    sigfillset  (&Newmask);
    sigprocmask (SIG_SETMASK, &Newmask, &Oldmask);
    if (sid == -1) return (1);
    ret = semop (sid, &sb, 1);
    return (ret);
}

    int
ControlShare::Vsem (int sid)
{
    int            val;
    struct sembuf   sb;

    sb.sem_num =  0;
    sb.sem_op  =  1;
    sb.sem_flg = SEM_UNDO;

    if (sid != -1)
	val = semop (sid, &sb, 1);
    else
	val = 1;
    sigprocmask (SIG_SETMASK, &Oldmask, NULL);
    return (val);
}
#endif
