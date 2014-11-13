/*
* Copyright (C) 2014 Unyphi. All rights reserved.
* 
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* as published by the Free Software Foundation; either version 2
* of the License, or (at your option) any later version.
* 
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
* 
* You should have received a copy of the GNU General Public License
* along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "curl/curl.h"
#include <unistd.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <netdb.h>
#include <errno.h>
#include <signal.h>

#include "udtcwrapper.h"
#include "stunbridge.h"
#include "debuglog.h"

#include "minIni.h"
      


#define NAT1_PORT	"9999"
#define NAT2_PORT	"9999"

#define SOURCE_PORT	"12345"

static int SendNATStatus(int result)
{
    int nSock;
    int errorCode;
    int HTTP_Status;
    

    errorCode = server_connect(&nSock);
    if (errorCode != 0)
    {
        printf("Cant connect to the NATStatus Website\n");
        return -1;
    }
    
    SendQueryToServerUpdateSymmetricNATStatus(nSock, result);
    shutdown( nSock, 1 );
    errorCode = server_reply (nSock,&HTTP_Status);
    TerminateConnection( nSock );
    //printf("123\n");
    if (HTTP_Status == 200)
    {
        return 0;
    }
    else     
    {
        return -1; // port closed
    }
    
    return 0;
}


int symmetricNATTest()
{
    char natserver1[256];
    char natserver2[256];
    int result = -1;
    struct addrinfo hints, *local=NULL, *peer=NULL;
    int socket;
    int ss,ret;
    char natcommand[]="NATTEST";
    char return_value1[128];
    char return_value2[128];
    char port1[16];
    char port2[16];

    ini_gets("General","NATServer1","nat1.monitoreverywhere.com",natserver1,128,"/mlsrb/serverconfig.ini");
    ini_gets("General","NATServer2","nat2.monitoreverywhere.com",natserver2,128,"/mlsrb/serverconfig.ini");
    
    
    wrapper_startUDT();
    

   memset(&hints, 0, sizeof(struct addrinfo));

   hints.ai_flags = AI_PASSIVE;
   hints.ai_family = AF_INET;
   hints.ai_socktype = SOCK_STREAM;
   //hints.ai_socktype = SOCK_DGRAM;

   if (0 != getaddrinfo(NULL, SOURCE_PORT, &hints, &local))
   {
      printf( "incorrect network address.\n");
     // return result;
     goto EXIT;
   }

   socket = wrapper_createSocket(hints.ai_family,hints.ai_socktype);

  // freeaddrinfo(local);

   if (0 != getaddrinfo(natserver1, NAT1_PORT, &hints, &peer))
   {
      printf ("incorrect server/peer address.1 %s : %s\n",natserver1,NAT1_PORT);
      //return result;
      goto EXIT;
   }
   
   if (0 !=  wrapper_bind(socket,local->ai_addr, local->ai_addrlen))
   {
	printf("Cant bind Address1\n");
	goto EXIT;
   }

   // connect to the server, implict bind
   ret = wrapper_connect(socket,peer->ai_addr, peer->ai_addrlen);
   if (ret < 0)
   {
      printf("connect1: %s\n", wrapper_getErrorMessage());
      goto EXIT;
   }
   freeaddrinfo(peer);

   ss = wrapper_send(socket,natcommand,strlen(natcommand),0);
   if (ss != strlen(natcommand))
   {
    printf("Cant send data1[%d:%d]\n",ss,strlen(natcommand));
    goto EXIT;
   }
   memset(return_value1,0,128);
   ss = wrapper_receive(socket,return_value1,64,0);
   if (ss >64 || ss < 5)
   {
    printf("Cant receive correct data1\n");
    goto EXIT;
   }
   return_value1[ss]=0;
   printf("Receive Return Value 1 [%s]\n",return_value1);
    
    //return_value1[ss-2]=0;
    {
	char delims[]="::";
	char *token = NULL;
	token = strtok(return_value1,delims);
	// get the ip
	if (token == NULL)
	{
	    printf("Error Parsing1\n");
	    goto EXIT;
	}
	token = strtok(NULL,delims);

	if (token == NULL)
	{
	    printf("Error Parsing1\n");
	    goto EXIT;
	}
	
	printf("Port1 is %s\n",token);
	strcpy(port1,token);
    }
    
    wrapper_closeSocket(socket);
    
    
   socket = wrapper_createSocket(hints.ai_family,hints.ai_socktype);

  // freeaddrinfo(local);

   if (0 != getaddrinfo(natserver2, NAT2_PORT, &hints, &peer))
   {
      printf ("incorrect server/peer address.2 %s : %s\n",natserver2,NAT2_PORT);
      //return result;
      goto EXIT;
   }
   
   if (0 !=  wrapper_bind(socket,local->ai_addr, local->ai_addrlen))
   {
	printf("Cant bind Address2\n");
	goto EXIT;
   }

   // connect to the server, implict bind
   ret = wrapper_connect(socket,peer->ai_addr, peer->ai_addrlen);
   if (ret < 0)
   {
      printf("connect2: %s\n", wrapper_getErrorMessage());
      goto EXIT;
   }
   freeaddrinfo(peer);

   ss = wrapper_send(socket,natcommand,strlen(natcommand),0);
   if (ss != strlen(natcommand))
   {
    printf("Cant send data2[%d:%d]\n",ss,strlen(natcommand));
    goto EXIT;
   }
   memset(return_value2,0,128);
   ss = wrapper_receive(socket,return_value2,64,0);
   if (ss >64 || ss < 5)
   {
    printf("Cant receive correct data1\n");
    goto EXIT;
   }
   return_value2[ss]=0;
   printf("Receive Return Value 2[%s]\n",return_value2);

    {
	char delims[]="::";
	char *token = NULL;
	token = strtok(return_value2,delims);
	// get the ip
	if (token == NULL)
	{
	    printf("Error Parsing1\n");
	    goto EXIT;
	}
	token = strtok(NULL,delims);
	if (token == NULL)
	{
	    printf("Error Parsing1\n");
	    goto EXIT;
	}
	
	printf("Port2 is %s\n",token);
	strcpy(port2,token);
    }
    
    wrapper_closeSocket(socket);
    
    
    if (strcmp(port1,port2)==0)
    {
	printf("2 ports are the same\n");
	result = 1;
	freeaddrinfo(local);
	wrapper_stopUDT();
	/*John added*/
	//SendNATStatus(0);
	//system("echo 0 > /tmp/symmetricNAT");
//	system("rm /tmp/symmetricNAT");
	return result;
    }    

    
    
EXIT:
   //freeaddrinfo(peer);
   result = 0;
   freeaddrinfo(local);

    wrapper_stopUDT();
 //   SendNATStatus(1);
    system("echo 1 > /tmp/symmetricNAT");
    return result;
}
