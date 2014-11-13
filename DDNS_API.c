/*****************************************************************************
 This file contains API calls for a basic network protocol communication
 ****************************************************************************/
/**********************Add header files here*********************************/
#include "DDNS.h"
#include "debuglog.h"
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
int CreateASocket( ) {
	int nSock=0;
			struct timeval tv;
			int size=0;

	// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
	//  Create the TCP socket that will be 
	//  used to connect to the interface.
	// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
	nSock = socket( AF_INET, SOCK_STREAM, 0/*IPPROTO_TCP*/);
			tv.tv_sec = 5;
			tv.tv_usec = 0;
			if (setsockopt(nSock, SOL_SOCKET, SO_RCVTIMEO, &tv,
					sizeof(struct timeval)) == 0) {
				tv.tv_sec = 0;
				if (getsockopt(nSock, SOL_SOCKET, SO_RCVTIMEO, &tv, &size)
						== 0) {
					//printf("Timeout = %d-%d\n", tv.tv_sec, tv.tv_usec);
				}
			} else {
				perror("Set Sockopt error\n");
			}

			if (setsockopt(nSock, SOL_SOCKET, SO_SNDTIMEO, &tv,
					sizeof(struct timeval)) == 0) {
				tv.tv_sec = 0;
				if (getsockopt(nSock, SOL_SOCKET, SO_SNDTIMEO, &tv, &size)
						== 0) {
					//printf("Timeout = %d-%d\n", tv.tv_sec, tv.tv_usec);
				}
			} else {
				perror("Set Sockopt error\n");
			}

	return nSock;
}

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
void TerminateConnection( int nSock ) {
	
	//char cFlush[1024];
	// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
	//  If we have a socket that looks valid, we want to gracefully close it.
	// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
	if( INVALID_SOCKET != nSock ) {
		// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
		//  Make sure that we have signaled shutdown from our end of the socket.
		// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
		//shutdown( nSock, 1 );
		// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
		//  Here we read all remaining data that the server has to send.
		// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
		//while( 0 < recv( nSock, cFlush, 1024, 0 ) );
		// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
		//  Now we are free to close the socket.
		// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
		if( 0 != close( nSock ) ){
		    #ifdef _DEBUG 
			printf( "Error - closesocket() failed: " );
		    #endif	
		}
	}
}

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
static int senda( int nSock, const char *cText ) {
	return send( nSock, cText, (int)strlen(cText), 0 );
}


// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//  Function: GetHostMacID()
//  Date: 06/12/2011
// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//  Parameters:
//    char *cText - A null terminated text 
//    string which will have the MAC id, when the
//    function is executed
// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//  Purpose:
//    This function provides the MAC id of the host 
//    machine. Appropriate function has to be implemented
//    at the camera side to get the unique serial ID of 
//    the camera
// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
void GetHostMACID( char *cText ) {
    int s;
    struct ifconf ifc;
    struct ifreq buffer;
    
    //! network interface
    char buff[1024];
    ifc.ifc_len = sizeof(buff);
    ifc.ifc_buf = buff;    
    
    s = socket(PF_INET, SOCK_DGRAM, 0);
    memset(&buffer, 0x00, sizeof(buffer));
    
#ifdef SIMULATION
    strcpy(buffer.ifr_name, "eth0");
#else
    strcpy(buffer.ifr_name, "ra0");
#endif
    if(ioctl(s, SIOCGIFHWADDR, &buffer) < 0){
		perror("ioctl(SIOCGIFHWADDR)\n");
	}
    
    if(ioctl(s,SIOCGIFCONF, &ifc) < 0){
		perror("ioctl(SIOCGIFCONF)\n");
	}
	
    close(s);
	
	int nInterfaceWifi = ifc.ifc_len/sizeof(struct ifconf);
	if(nInterfaceWifi < 2){
		printf("In GetHostMACID funtion: dectected Wifi module insmod faild\n");		
		system("killall beep_arm");
	    		system("/mlsrb/beep_arm 20000 400 400 600 6000");
		sleep(8);
		system("killall beep_arm");
		return;
	}

		sprintf(cText,"%02X%02X%02X%02X%02X%02X",\
			buffer.ifr_hwaddr.sa_data[0],\
			buffer.ifr_hwaddr.sa_data[1],\
			buffer.ifr_hwaddr.sa_data[2],\
			buffer.ifr_hwaddr.sa_data[3],\
			buffer.ifr_hwaddr.sa_data[4],\
			buffer.ifr_hwaddr.sa_data[5]\
			);
#ifdef _DEBUG     
    printf("MacID is '%s'\n",cText);
#endif
}

// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//  Function: GetRemoteServerInformation()
//  Date: 06/12/2011
// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//  Parameters:
//	struct *ServerList - A structure which will 
//	be updated with the server name and also if 
//	information in the structure is valid
// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//  Purpose:
//    This function reads a config file and tries to 
//    get information of the available servers to which the
//    camera can connect to. The API also updates a list with 
//    the server names and also assigns an id to the server.
//    Note: This is a simulator varsion. While porting the 
//    code on camera appropriate file system calls have 
//    to be used. 	 
// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
void GetRemoteServerInformation(ServerList *pServerList)
{
    GenericFilePointer *pCfgFile;
    char line_buffer[256]; 
    ServerList *pTempServerList;
    int istrlen;
    int count =0;	
    pTempServerList = pServerList;


    strcpy(pTempServerList->ServerName,"stun.monitoreverywhere.com");
    pTempServerList->IsInfoValid=TRUE;


}



void SendQueryToServerUpdateSymmetricNATStatus(int nSock, int NATStatus)
{
    char tmpstr[256];
	int i;
	char mac[64];
    // =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
	//  Start out our HTTP session by requesting a page.
	// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
	GetHostMACID(mac);
	sprintf(tmpstr,"GET /BMS/cameraservice?action=command&command=symmetricnatstatus&status=%d&mac=%s",NATStatus,mac);
	senda( nSock, tmpstr);
	// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
	//  Finally we finish off the HTTP request.
	// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
	senda( nSock, " HTTP/1.0\r\n\r\n" );
}

int server_connect(int* pSock)
{
    extern char command_servername[];
//    char command_servername1[]="www.simplimonitor.com";
    printf("Use server %s\n",command_servername);
    return _server_connect(pSock,command_servername);
}

int local_connect(int* pSock)
{
    return _server_connect(pSock,"127.0.0.1");
}


int _server_connect(int* pSock,char* host)
{
	int nSock = INVALID_SOCKET;
	struct hostent *pHostEnt = NULL;
	struct sockaddr_in SockAddr;
	struct addrinfo hints, *peer=NULL;
	int numServers =0;
	char portstr[12];


	int errorCode = 0;

    /* Create the socket */
    nSock = CreateASocket( );
    if( INVALID_SOCKET == nSock ) {
        #ifdef _DEBUG 	
        printf( "Error - Failed to create socket: .\r\n");
        #endif	
        errorCode = -1;
    }			
    
    memset(&hints, 0 , sizeof (struct addrinfo));

    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    
    //pHostEnt = gethostbyname(SERVER_NAME);
    errorCode = getaddrinfo(host,"80",&hints,&peer);
    if(0 !=  errorCode)
    {
        printf( "Error - Failed to look up server %s:'%s' \r\n", host,gai_strerror(errorCode) );
         errorCode = -2;
        goto exit; 
    }

    /*  Attempt to make the TCP connection. */
    if( SOCKET_ERROR == connect( nSock, peer->ai_addr, peer->ai_addrlen ) ) {
        errorCode = -3;
    }
    if (errorCode == 0)
    {
        *pSock = nSock;
	if(peer != NULL)
        	freeaddrinfo(peer);
        return errorCode;
    }
    
exit:    
    if (nSock != INVALID_SOCKET)
    {
        shutdown( nSock, 1 );
        close(nSock);
    }
    if(peer != NULL)
    	freeaddrinfo(peer);
    return errorCode;
}

int server_reply(int nSock, int* HTTP_Status)
{
	char cResponse[RESPONSE_BUFFER];
 	char ReadDone = FALSE;
    int nReadResult = SOCKET_ERROR;
    *HTTP_Status = 0;
    /*  Loop until signaled */
    while( !ReadDone ) {

        /* Attempt to read from the socket */
        nReadResult = recv( nSock, cResponse, RESPONSE_BUFFER, 0 );

        /*  Handle the results of our read */
        switch( nReadResult ) {
        case SOCKET_ERROR: 
            /*  An unexpected error occurred */
           #ifdef _DEBUG 	
            printf( "Error: Error reading from socket: .\r\n");
           #endif	
        case 0: 
            /*  The server closed the connection */
            ReadDone = TRUE;
            break;
        default: 
            /*  All appearantly went well with our socket read */
            /*  NULL terminate the string we read & print it */
            cResponse[nReadResult] = 0x00;	
            //printf("%s", cResponse );
            puts(cResponse);
            printf("%s\n",cResponse);
            if (*HTTP_Status == 0 && strlen(cResponse)>15) // at least must contains enough error code
            {
                // parse parse
                char* tmpptr;
                
                tmpptr = strstr(cResponse,"HTTP/1.1");
                if (tmpptr != NULL)
                {
                    tmpptr += 9;
                    tmpptr[3]='\0';
                    printf("Error Code = %s\n",tmpptr);
                    *HTTP_Status = atoi(tmpptr);
                    break;
                }
                printf("Not HTTP 1.1\n");
                // test HTTP/1.0
                tmpptr = strstr(cResponse,"HTTP/1.0");
                if (tmpptr != NULL)
                {
                    tmpptr += 9;
                    tmpptr[3]='\0';
                    printf("Error Code = %s\n",tmpptr);
                    *HTTP_Status = atoi(tmpptr);
                    break;
                }
                printf("Not HTTP 1.0\n");
            }
         }
    }

    return 0;
}



int getLocalIPAddress(char* IPStr) {
		int ret = -1;
        FILE * fp = popen("ifconfig ra0", "r");
        if (fp) {
                char *p=NULL, *e,*tmpptr; 
				size_t n;
                while ((getline(&p, &n, fp) > 0) && p) {
                   //printf("p = %08X\n",p);
                   if (tmpptr = strstr(p, "inet addr:")) {
                        tmpptr+=10;
                        if (e = strchr(tmpptr, ' ')) {
                             *e='\0';
                             strcpy(IPStr,tmpptr);
					//		 free (p);
							 ret = 0;
							 break;
                        }
                   }
                   //printf("12345");
		    //free(p);
                }
                free(p);
        }
        pclose(fp);
        return ret;
}


