#ifndef _SHARE_H
#define _SHARE_H

#define UNLOCK_CYCL
#define UNLOCK_SIZE 0x1000
#define UNLOCK_MASK (UNLOCK_SIZE-1)

#define FAILPTR     ((void *)(-1))
#define NCOREMAP    0x1000
#define MAP_SIZE    (NCOREMAP*sizeof(MAP))

static const int CONVERT_TO_NULL = -16; /* Not really an error */

typedef unsigned char UC;
typedef unsigned  int UI;
typedef unsigned long UL;

typedef struct adm
{
    UC *adm_pool;
    UI  adm_size;
} ADM;

typedef struct map
{
    UI  map_size;
    UL  map_addr;
} MAP;

typedef struct prefix
{
  char    tag[8];
  int     size;
} Prefix;

typedef struct header
{
  int     type;         /* Type of buffer Shared/Malloc */
  int     link;         /* Usage count when zero buffer is released */
  Prefix  pref;
} Header;

typedef struct shmdsc
{
  int     shmid;        /* The identifier of the shared memory */
  int     semid;        /* The identifier of the semaphore */
  int     index;        /* The data        buffer index in the shared memory */
  int     cycle;        /* The cycl unlock buffer index in the shared memory */
} Shmdsc;

#define RPTR        -1
#define WPTR        -2
#define INCBASE (sizeof (CYCADM)/sizeof (UI))

typedef struct cycadm
{
  UI    writepointer;
  UI     readpointer;
} CYCADM;

void            Csem        (int key);
void           *creatShm    (int shm_key, int size);
int             delkeyShm   (int shm_key);
int             ffInit      (unsigned char *memory, unsigned intsize, unsigned int semkey);
void            ffStat      (unsigned char *memory);
unsigned char  *ffAlloc     (unsigned char *memory, unsigned int usersize);
void            ffFree      (unsigned char *addr);

#endif
