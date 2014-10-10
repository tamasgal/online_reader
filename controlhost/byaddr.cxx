/*
** Control Host OO interface implementation.
**
** $Id: byaddr.c,v 1.6 2006/08/29 10:39:37 ruud Exp $
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

void    usleep (unsigned microsec);
#else
#include <unistd.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <netinet/in.h>
#endif

#include "choo.h"
#include "share.h"

#ifndef VXWORKS
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#endif

#define UNLOCK_HIGH_WATER       100

void *ShmAddr = {FAILPTR};

static int cycl_count (UI wptr, UI rptr, int size);

  void *
ControlHost::GetDataAddr (void)
{
  // XXX return on error (void *)NULL instead of -1
  // make conversion in handleError

  Choo.real_unlock = 1;

  if (Choo.status == s_idle)
    return ((void *)(long)handleError (this, BadGetDataAddr, CONVERT_TO_NULL, "GetDataAddr not idle"));

  if (Choo.status == s_free)
  {
    Choo.status = s_idle;
    return ((void *)Choo.data_ptr);
  }

  if (Choo.size_active < 0)
    return ((void *)(long)handleError (this, BadGetDataAddr, CONVERT_TO_NULL, "GetDataAddr negative"));

  Choo.data_ptr = malloc (Choo.size_active);
  if (Choo.data_ptr == NULL)
    return ((void *)(long)handleError (this, BadGetDataAddr, CONVERT_TO_NULL, "GetDataAddr malloc"));

  Choo.real_unlock = 0;

  int rc = get_data (Choo.data_ptr, Choo.size_active);
  Choo.status = s_idle;
  if (rc <= 0)
  {
    free   (Choo.data_ptr);
    return ((void *)(long)handleError (this, BadGetDataAddr, CONVERT_TO_NULL, "GetDataAddr get_data"));
  }
  return ((void *)Choo.data_ptr);
}

  int
ControlHost::UnlockData (void *ptr)
{
  if (Choo.real_unlock == 0)
  {
    free (ptr);
    return (0);
  }

#ifdef  UNLOCK_CYCL
  if (ShmAddr == FAILPTR)
    return CH_RET_NO_CONNECTION;

  int index = htonl((long)ptr - (long)ShmAddr);

  while (cycl_count (UnlockBase [WPTR], UnlockBase [RPTR], UNLOCK_SIZE) > (UNLOCK_SIZE - UNLOCK_HIGH_WATER))
  {
//  fprintf (stderr, "Unlock administration overflow, increase UNLOCK_SIZE\n");
    usleep (10000);
  }

  {
    *(UnlockBase + (UnlockBase [WPTR])) = index;
      UnlockBase [WPTR] += 1;
      UnlockBase [WPTR] &= UNLOCK_MASK;
  }

#else

  Shmdsc descr;

  if (Choo.dataline < 0)
    return CH_RET_NO_CONNECTION;

  if (ShmAddr == FAILPTR)
    return CH_RET_NO_CONNECTION;

  descr.index = htonl((int)ptr - (int)ShmAddr);

  int rc = put_tagged_bwait (Choo.dataline, DISPTAG_Unlock, &descr, sizeof (descr));
  if (rc < 0)
    socket_close (Choo.dataline);
#endif

  return (0);
}

  void *
ControlHost::getShared (int sock)
{
#ifdef  VXWORKS
   printf ("getShared not implemented in VxWorks environment\n");
   return NULL;
#else

  Shmdsc descr;
  int    rc = getbwait (sock, &descr, sizeof (descr));

  if (rc <= 0)
    return NULL;

  if (ShmAddr == (void *)-1)
  {
    ShmAddr = shmat (ntohl(descr.shmid), 0, 0);
    if (ShmAddr == (void *)-1)
    {
       printf ("Error shmat\n");
       return NULL;
    }
  }

#ifdef  UNLOCK_CYCL
  UnlockBase  = (UI   *) (((unsigned long)ShmAddr) + ntohl(descr.cycle));
#endif

  return (      (void *) (((unsigned long)ShmAddr) + ntohl(descr.index) + sizeof (Header)));

#endif
}

  static int
cycl_count (UI wptr, UI rptr, int size)
{
  int diff = wptr - rptr;
  if (diff >= 0)
    return (diff);
  else
    return (diff+size);
}
