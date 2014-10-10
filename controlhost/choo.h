/*
** Implements Class ControlHost
**
** $Id: choo.h,v 1.43 2008/01/21 11:50:08 ruud Exp $
*/

#ifndef CHOO_H
#define CHOO_H

#include "string"
#include "iostream"
#include "list"
#include <signal.h>

#define CHOO_VERSION            "1.8"

#define TAGSIZE                 8
#define DISPATCH_PORT           5553

#define DISPTAG_Subscribe       "_Subscri"
#define DISPTAG_Gime            "_Gime"
#define DISPTAG_Always          "_Always"
// fine DISPTAG_SkipMode        "_SkipMod"
#define DISPTAG_MyId            "_MyId"
#define DISPTAG_UniqueId        "_UniqueI"
// fine DISPTAG_ClosLine        "_ClosLin"
#define DISPTAG_CloseAll        "_CloseAl"
// fine DISPTAG_CleanTgs        "_CleanTg"
#define DISPTAG_StopDisp        "_StopDis"
#define DISPTAG_Born            "Born"
#define DISPTAG_Died            "Died"
#define DISPTAG_ShowStat        "_ShowSta"
#define DISPTAG_WhereIs         "_WhereIs"
// fine DISPTAG_WrongHdr        "_WrongHd"
// fine DISPTAG_Ignore          "_Ignore"
// fine DISPTAG_Duplicate       "_Duplica"
#define DISPTAG_Version         "_Version"
#define DISPTAG_Unlock          "_Unlock"
#define DISPTAG_DebugOn         "_Debug+"
#define DISPTAG_DebugOff        "_Debug-"

// Return values don't change sequnece
#define CH_RET_DEFAULT          (-1)
#define CH_RET_SHUTDOWN         (-2)
#define CH_RET_NO_CONNECTION    (-3)

// Mode arg constructor
#define CHOO_READ               0x01
#define CHOO_WRITE              0x02
#define CHOO_APPEND             0x04
#define CHOO_PIPE               0x08
#define CHOO_NEXT               0x10

// Arg set_connection
#define CONNECTION_WAIT         0
#define CONNECTION_NOWAIT       1


#define CHOO_NULL ((ControlHost *)0)

typedef enum
{
  s_idle,                       // no events on any line
  s_data,                       // header received, but not data
  s_free                        // data in shared mem or hidden buffer
} status_t;

typedef struct 
{
  char          p_tag[TAGSIZE];
  int           p_size;
} PREFIX;

typedef struct choo
{
  int           dataline;       // Socket should by init as: -1
  int           size_active;    // data size when status != s_idle
  int           real_unlock;    // true when status == s_free, data is in shared memory
  void         *data_ptr;       // pointer to data when status == s_free
  int           wouldblock;     // Out tcpio.c
  int           getpos;         // Position  input
  int           putpos;         // Position output
  PREFIX        pref;           // prefix
  status_t      status;         // init as: s_idle
} CHOO;


class ControlHost
{
public:

  static const int MAX_NAME =       256;  // Max size of subscription & format strings
  static const int MAX_CONN =       512;  // Max number of connections
#ifdef  VXWORKS
  static const int MAX_DATA =  (10*1024); // Max number of data bytes
#else
  static const int MAX_DATA =  (64*1024); // Max number of data bytes
#endif

  enum  ErrorType
  {
     BadBeg,
     BadInit,           BadSubscribe,      BadSendMeNext,
     BadSendMeAlways,   BadMyId,           BadUniqueId,
     BadWhereIs,        BadWaitHead,       BadCheckHead,

     BadGetFullString,  BadGetFullData,
     BadPutFullString,  BadPutFullData,
     BadPutPartString,  BadPutPartData,

     BadGetDataAddr,    BadUnlockData,
     BadGetSwapData,    BadPutSwapData,
     BadAddSwapInfo,    BadSwapSyntax,
     BadSelectRead,     BadSelectWrite,
     BadEnd
  };

  class TagStream
  {
  public:
    std::string tag;
    TagStream (const std::string argTag):tag (argTag) {}
  };

  class Exception
  {
  public:
    ErrorType   type;   // Error enum type.
    int         code;   // The negative error code (rc)
    std::string disp;   // Hostname dispatcher
    std::string proc;   // Member function name
    std::string attr;   // Additional info or ""

    Exception (ControlHost  *argThis,         ErrorType argType, int argCode,
	   const std::string argProc, const std::string argAttr = "")
    :
    type (argType),
    code (argCode),
    proc (argProc),
    attr (argAttr)
    {
      if (argThis)
	disp = argThis->Host;
    }
  };

  static void        Throw        (bool except = true);
  static bool        SwapSyntax   (const std::string fmt);
  static int         Swap         (const std::string fmt, void *dst, const void *src, int nbytes);
  static int         IpNumber     (const std::string name = "");
  static std::string IpDecimalDot (int ip);
  static int         WhereIs      (const std::string  disphost, const std::string nickname,
					 std::string &replybuf, int nbytes = MAX_DATA);

  static ControlHost *SelectRead   (int timeout, ...);
  static ControlHost *SelectWrite  (int timeout, ...);

  static ControlHost *SelectRead   (int timeout, ControlHost *selch []);
  static ControlHost *SelectWrite  (int timeout, ControlHost *selch []);

  ControlHost &operator<< (TagStream tagstream);
  ControlHost &operator<< (std::string ostr);
  ControlHost &operator<< (int         oint);

  ControlHost (std::string host, int mode, std::string nickname, std::string tag = "");
  ControlHost (std::string host = "");          // Regular client dispatcher
  ControlHost (std::string host, int port);     // User defined client
  ControlHost (int port);                  // User defined server
 ~ControlHost (void);

  int   Connected       (void);
  int   Subscribe       (const std::string subscr);

  int   AddSwapInfo     (const std::string tag, const std::string fmt);
  int   PutSwapData     (const std::string tag, const void  *data, int nbytes);
  int   GetSwapData     (const std::string tag,       void  *data, int nbytes);

  int   SendMeNext      (void);
  int   SendMeAlways    (void);
  int   MyId            (const std::string nickname);
  int   UniqueId        (const std::string id);
  int   CloseAll        (const std::string nickname);

  int   WaitHead        (std::string &tag, int &nbytes);
  int   CheckHead       (std::string &tag, int &nbytes);

  int   GetFullData     (void  *data, int  nbytes);
  int   GetFullString   (std::string &str);
  int   PutFullDaq      (const std::string tag, const void  *buf, int nbytes);
  int   PutFullDaq      (const std::string tag, const std::string str);
  int   PutFullDcs      (const std::string tag, const void  *buf, int nbytes);
  int   PutFullDcs      (const std::string tag, const std::string str);
  int   PutFullData     (const std::string tag, const void  *buf, int nbytes);
  int   PutFullString   (const std::string tag, const std::string str);
  int   PutPartData     (const std::string tag, const void  *buf, int nbytes);
  int   PutPartData     (const std::string tag, const void  *buf, int nbytes, int &pos);
  int   PutPartString   (const std::string tag, const std::string str, int &pos);
  int   PutPartString   (const std::string tag, const std::string str);

  void *GetDataAddr     (void);
  int   UnlockData      (void *ptr);
  void  ZbufWaitDone    (bool on);

friend class Exception;

private:
  static int  handleError(ControlHost  *argThis,    ErrorType argType,
				    int argCode, const std::string argProc,
						       std::string argAttr = "");

  int   headproc        (char *tag, int *size, int wait);
  int   get_data        (void *buf, int lim);
  int   get_string      (char *buf, int lim);
  void  unlock_data     (void);

  int   getblock        (int socket, void *buf, int bsize, int *pos);
  int   skipblock       (int socket, int *count);
  int   newblock        (int socket, const void *buf, int bsize);
  int   putblock        (int socket, const void *buf, int bsize, int *pos);
  int   getbwait        (int socket, void *buf, int bsize);
  int   putbwait        (int socket, const void *buf, int bsize);
  int   skipbwait       (int socket, int bsize);

  int   put_tagged_block(int socket, const char *tag, const void *buf, int size, int *pos);
  int   put_tagged_bwait(int socket, const char *tag, const void *buf, int size);
  int   put_tagged_bfull(int socket, const char *tag, const void *buf, int size);

  void *getShared       (int sock) ;

  static int whereis    (const char *host, const char *id, char *reply, int maxreplen);

#ifdef  VXWORKS
#ifdef  VX_USE_ZBUF
public:
  void  ZbufCallBack    (VOIDFUNCPTR fun, int arg);

private:
  VOIDFUNCPTR   CallBackFun;    // Zero copy callback function pointer
  int           CallBackArg;    // Zero copy callback function argument
  bool          ZbufCopyMode;   // Via PutFullData or PutFullString
  SEM_ID        WaitDoneSem;    // Zero copy semaphore

  bool          CallBackMode;   // in ZbufCallBack mode
  bool          WaitDoneMode;   // in ZbufWaitDone mode
  int           TcpSendSpace;   // tcp_sendspace
#endif
#endif

  std::string   Redirect;       // Current redirection string
  std::string   Host;           // Hostname dispatcher
  CHOO          Choo;
  unsigned int *UnlockBase;
  static bool   Except;         // Exception boolean

  class TS
  {
  public:
    std::string Tag;
    std::string Swp;
  };

  std::list <TS> TagSwapList;
};

class ControlShare
{
public:
  class Exception
  {
  public:
    std::string      disp;   // Hostname dispatcher
    std::string      proc;   // Member function name
    std::string      attr;   // Additional info or ""

    Exception (const std::string  argProc, const std::string  argDisp, const std::string argAttr = "")
    :
    disp (argDisp),
    proc (argProc),
    attr (argAttr)
    {
    }
  };

  static void   Throw   (bool except = true);

  ControlShare (std::string host, std::string myid = "");
 ~ControlShare (void);

  int   Connected       (void);
  int   PutFullData     (const std::string tag, const void  *buf, int nbytes);
  int   PutFullString   (const std::string tag, const std::string str);
#ifndef VXWORKS
  int   Osem            (int key);
  int   Psem            (int key);
  int   Vsem            (int key);
#endif

friend class Exception;

private:
  static void handleError (const std::string argProc, const std::string argDisp, const std::string argAttr = "");
	 int  put_tagged_share (int socket, const char *tag, const void *buf, int bsize);
	 int  putshare         (int socket,                  const void *buf, int bsize);

  int           ShareSocket;    // socket
  std::string   Host;           // Hostname dispatcher
  static bool   Except;         // Exception boolean
#ifdef  VXWORKS
  SEM_ID SemId;
#else
  int      SemId;
  sigset_t Oldmask;
  sigset_t Newmask;
#endif
};

std::ostream &operator<< (std::ostream &s, ControlHost ::Exception &error);
std::string  &operator<< (std::string  &s, ControlHost ::Exception &error);
std::ostream &operator<< (std::ostream &s, ControlShare::Exception &error);
std::string  &operator<< (std::string  &s, ControlShare::Exception &error);

class ControlServ
{
public:
   ControlServ (int port);
  ~ControlServ (void);

   ControlHost *AcceptClient (void);

   int  ServerSocket;
   int  ServerPort;
};

void fillprefix         (PREFIX *p, const char *tag, int size);
void fromprefix         (const PREFIX *p, char *tag, int *size);
int  put_tagged_block   (int socket, const char *tag, const void *buf, int size, int *pos);
int  put_tagged_bwait   (int socket, const char *tag, const void *buf, int size);
int  set_connection     (int socket, int value);
#ifdef  VXWORKS
void callBack           (caddr_t buf, int arg);
#endif

#endif
