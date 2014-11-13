/******************************************************************************
************************ All function prototypes ******************************
******************************************************************************/
#ifndef _DDNS_h_
#define _DDNS_h_

/* Device can choose to omit these macros, used for debug purposes */
#define _DEBUG
#define _GETMACID

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <netdb.h>
// These two headers are included to get the host's MAC ID
// Device(Camera) need not include these files
#ifdef _GETMACID
#include <sys/ioctl.h>
#include <net/if.h>
#endif


/* Add all macros here */
//#define MAC_ADDRESS "00-25-64-E4-48-60"
//#define SERVER_NAME "monitoreverywhere.com"
#define SERVER_NAME "www.monitoreverywhere.com"



#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define FALSE 0
#define TRUE 1
#define HTTP_PORT		80
#define RESPONSE_BUFFER		2048
#define VALID_INFO 1

/* Macros to handle file system calls */
/* While porting to camera, the Generic file read macros 
   should be replaced with the corresponding file system calls */
#define GenericFileOpen fopen
#define GenericFileRead fread
#define GenericFileWrite fwrite
#define GenericFileClose fclose
#define GenericFileGets	fgets
#define GenericFilePointer FILE 

/*Structure to maintain the server information */
typedef struct _ServerList{
   char ServerName[256]; 
   char IsInfoValid;	
}ServerList;
// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//  Function: CreateASocket()
//  Date: 06/12/2011
// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//  Parameters:
//	  return parameter	
//    int nSock - The socket that we are to
//      make sure is closed.
// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//  Purpose:
//    This function is responsible for creating
//	  a socket
// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
int CreateASocket( );

// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//  Function: TerminateConnection()
//  Date: 06/12/2011
// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//  Parameters:
//    int nSock - The socket that we are to
//      make sure is closed.
// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//  Purpose:
//    This function is responsible for gracefully 
//  closing a given socket if a connection is 
//  established.
// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
void TerminateConnection( int nSock );

// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//  Function: senda()
//  Date: 06/12/2011
// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//  Parameters:
//    int nSock - The valid socket which we
//      are to use to send the string.
//    const char *cText - A null terminated text 
//      string to be sent to socket.
// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//  Purpose:
//    This function just wraps the standard send 
//  using strlen to calculate the length, and 
//  assumes no options.
// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
static int senda( int nSock, const char *cText );

void SendQueryToServerUpdateSymmetricNATStatus(int nSock, int NATStatus);

#endif
