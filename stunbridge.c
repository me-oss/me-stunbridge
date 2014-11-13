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
#include <syslog.h>

#include "udtcwrapper.h"
#include "stunbridge.h"
#include "debuglog.h"

#ifndef SIMULATION
#define CAMERA_IP "127.0.0.1"
#define DEST_IP "192.168.1.77"
#define DEST_PORT 50005
#else
#include "minIni.h"
#endif

//#include "debuglog.h"

#define TRUE 1

#define TIMEOUT 10*60
#define MAX_IDLE_TIME 20

//#define DEBUG_UDT_TO_FILE 1

char debug_macaddr[20];

char server_name[128];
char command_servername[128];
int server_stunport;

struct MemoryStruct
{
	char*	buffer;
	size_t size;
};


#define MAX_COMMAND_THREAD_NUM 1
#define MAX_DATA_THREAD_NUM 1

typedef struct _UDTSession
{
	//CURL* curl_handle_data;
	char dest_ip[32];
	char stream_id[32];
	char sessionkey[65];
	char randomstr[9];
	int dest_port;
	int local_port;
	int udt_socket;
	time_t start_time;
	time_t idle_time;
	int stop;
	int command_thread_num;
	int data_thread_num;
	int notavail;
}UDTSession;


typedef struct _context
{
	char camera_ip[32];
	int camera_port;
	char credetial[32];
	pthread_mutex_t mutex;
	UDTSession session[MAX_UDT_DATA_SESSION];
	int num_session;
}context;



typedef struct _command_info
{
	char command_data[128];
	char command_return[256];
}command_info;

typedef struct _UDTConnection
{
	context* pcontext;
	int session_id;
	struct sockaddr_in client_addr;
	int udt_fd;
	int stop_download_data;
	int totalbytes;
}UDTConnection;

static context ctx;
#if DEBUG_UDT_TO_FILE
static FILE* debug_fp;
#endif




static size_t write_data(void *ptr, size_t size, size_t nmemb, void *userp) {
    size_t written;
	FILE* stream = (FILE*)userp;
    printf("Need to write %d bytes to file\n",size*nmemb);    
    written = fwrite(ptr, size, nmemb, stream);
    return written;
}
#define SEND_FAIL_MAX  5
static size_t send_udt_data(void *ptr, size_t size, size_t nmemb, void *userp) {
	size_t written = size * nmemb;
    unsigned int ssize = 0;
    int ss;
    time_t curtime;
    static int display_flag;
    static int megabytes = 0;
    static int count_fail=0;
    UDTConnection* pUDTConn = (UDTConnection*)userp;

#if DEBUG_UDT_TO_FILE
	write_data(ptr,size,nmemb,debug_fp); // for debug only log to file will need to remove it later
#endif

	curtime = time(NULL);
	if ((curtime - pUDTConn->pcontext->session[pUDTConn->session_id].start_time) > TIMEOUT )
	{

		pUDTConn->stop_download_data = 1;
		return 0;
	}
/*	
	if ((((curtime - pUDTConn->pcontext->session[pUDTConn->session_id].start_time) % 20) == 0) && display_flag)
	{
	    printf("Speed %f KBs\n",((float)pUDTConn->totalbytes)/(1024*(curtime - pUDTConn->pcontext->session[pUDTConn->session_id].start_time)));
	    display_flag = 0;
	}
	else
	{
	    display_flag = 1;
	}
	
*/	
	if ((pUDTConn->totalbytes/(1024*1024)) > megabytes)
	{

	    megabytes++;
	} 
	while (ssize < written && pUDTConn->stop_download_data != 1)
	{
	    ss = wrapper_send(pUDTConn->udt_fd,(char*)ptr+ssize, written - ssize,0);
	    if (ss < 0)
	    {
	    	int errorcode = wrapper_getErrorCode();	    	
			printf("Error %d: %s\n",errorcode, wrapper_getErrorMessage());
			//! Check connection state

	    	sock_state s_state = wrapper_getSocketStatus(pUDTConn->udt_fd);
	    	printf("Socket status = %d\n",s_state);
	    	if( s_state == CUDTSTATE_CONNECTED)
	    	{
				printf("Connection still exists\n");
				count_fail++;
				if(count_fail > SEND_FAIL_MAX)
					pUDTConn->stop_download_data = 1;
	    	}else
	    	{
	    		pUDTConn->stop_download_data = 1;
	    		printf("Connection Closed by client\n");
	    	}

/*
	    	if ( (errorcode == 6001)) // network error
	    	{
	    	    printf("Network error just ignore\n");
	    	    break;
		}
	    	else
	    	{
	    		printf("Connection Closed by client\n");
		    	pUDTConn->stop_download_data = 1;
		            // client say exit
		            // set the flag also to the progress func to signal please exit
		        break;

	    	}
*/
	    	break;
	    }else
	    	count_fail = 0;

	    ssize += ss;
	}
	pUDTConn->totalbytes += ssize;
	
	return written;
}


static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
  size_t realsize = size * nmemb;
  struct MemoryStruct *mem = (struct MemoryStruct *)userp;
  printf("RealSize = %d\n",realsize);
  //printf("Content='%s'\n",(char*) contents);
  if (realsize < 256)
  {
	memcpy(mem->buffer, contents, realsize);
	mem->size = realsize;
	mem->buffer[realsize] = 0;
  }
  else
  {
	  return -1;
  }
  return realsize;
}

static int progress_func(void* ptr, double TotalToDownload, double NowDownloaded, double TotalToUpload, double NowUploaded)
{
    UDTConnection* pUDTConn = (UDTConnection*)ptr;
//    printf("1pUDTConn->stop_download_data : %d\n",pUDTConn->stop_download_data);
    // It's here you will write the code for the progress message or bar
	//printf("Already download %d bytes\n",NowDownloaded);
	if (pUDTConn->stop_download_data)
	{
		printf("User cancel\n");
		return 1;
	}
	return 0;
}

void udt_init()
{
	int i=0;
	wrapper_startUDT();
	for (i=0;i<MAX_UDT_DATA_SESSION;i++)
	{
		memset(&ctx.session[i],0,sizeof(UDTSession));
	}

}

void udt_deinit()
{
	wrapper_stopUDT();
	// Use KILLALL to KILL so no need free ???

}


int camera_process_send_command(char* cmddata, char* sret) // sret = string return. The mem needs to be avail at least 256 bytes
{
    CURLcode res;
	CURL* curl_handle_command;

    char cmd[256];
    int exit_status = 0;
    struct MemoryStruct chunk;

    curl_handle_command = curl_easy_init();
    if (curl_handle_command == NULL)
    {

    	return 1;
    }

    sprintf(cmd,"http://%s:%d/?%s",ctx.camera_ip,ctx.camera_port,cmddata);


	 /* specify URL to get */
	  curl_easy_setopt(curl_handle_command, CURLOPT_URL, cmd);

	  /* send all data to this function  */
	  curl_easy_setopt(curl_handle_command, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);

	  /* we pass our 'chunk' struct to the callback function */
	  chunk.buffer = sret;
	  curl_easy_setopt(curl_handle_command, CURLOPT_WRITEDATA, (void *)&chunk);
	  if (ctx.credetial != NULL)
	  {
		  curl_easy_setopt(curl_handle_command, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
		  curl_easy_setopt(curl_handle_command, CURLOPT_USERPWD, ctx.credetial);
	  }

	  curl_easy_setopt(curl_handle_command,CURLOPT_CONNECTTIMEOUT,60);
	  curl_easy_setopt(curl_handle_command,CURLOPT_FAILONERROR,TRUE);

	  /* some servers don't like requests that are made without a user-agent
		 field, so we provide one */
	  curl_easy_setopt(curl_handle_command, CURLOPT_USERAGENT, "libcurl-agent/1.0");
	  /* get it! */
	  res = curl_easy_perform(curl_handle_command);
	  if (res != CURLE_OK)
	  {

	    exit_status = 1;
	    goto CLEANUP;
	  }
	  else
	  {
	  }

CLEANUP:
	  curl_easy_cleanup(curl_handle_command);
	  return exit_status;
}


int camera_process_receive_data(UDTConnection* pUDTConn,answer_t type) // Only support APLET VA Stream at the moment
{
    char cmd[256];
    int exit_status = 0;
//    FILE* fp ; // for debug only will replace with something else
    CURLcode res;
	CURL* curl_handle_data;
	// start the timer
	pUDTConn->pcontext->session[pUDTConn->session_id].start_time = time(NULL);
	pUDTConn->totalbytes = 0;
    if (type == VAAPPLET)
	sprintf(cmd,"http://%s:%d/?action=appletvastream",ctx.camera_ip,ctx.camera_port);
    else  // appletAStream Audio Only
	sprintf(cmd,"http://%s:%d/?action=appletastream",ctx.camera_ip,ctx.camera_port);

    curl_handle_data = curl_easy_init();
    if (curl_handle_data  == NULL)
    {
    	printf("Error Creating Curl\n");
    	return 1;
    }
	 /* specify URL to get */
	  curl_easy_setopt(curl_handle_data, CURLOPT_URL, cmd);

	  /* send all data to this function  */
	  //curl_easy_setopt(ctx.curl_handle_data, CURLOPT_WRITEFUNCTION, write_data);
	  curl_easy_setopt(curl_handle_data , CURLOPT_WRITEFUNCTION, send_udt_data);
	  /* we pass our 'chunk' struct to the callback function */
#if DEBUG_UDT_TO_FILE
	  	  debug_fp = fopen("/tmp/data.dump","wb");

	  if (debug_fp == NULL)
	  {
			printf("ERROR CANT CREATE FILE \n");
			exit_status = 1;
          goto CLEANUP;
	  }

#endif
	  curl_easy_setopt(curl_handle_data , CURLOPT_WRITEDATA, (void *)pUDTConn);
	  if (ctx.credetial != NULL)
	  {
		  curl_easy_setopt(curl_handle_data , CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
		  curl_easy_setopt(curl_handle_data , CURLOPT_USERPWD, ctx.credetial);
	  }

	  curl_easy_setopt(curl_handle_data ,CURLOPT_CONNECTTIMEOUT,60);
	  curl_easy_setopt(curl_handle_data ,CURLOPT_FAILONERROR,TRUE);
	  curl_easy_setopt(curl_handle_data ,CURLOPT_NOPROGRESS,0);
	  curl_easy_setopt(curl_handle_data ,CURLOPT_PROGRESSFUNCTION,progress_func);
	  curl_easy_setopt(curl_handle_data ,CURLOPT_PROGRESSDATA,(void*)pUDTConn);

	  /* some servers don't like requests that are made without a user-agent
		 field, so we provide one */
	  curl_easy_setopt(curl_handle_data , CURLOPT_USERAGENT, "libcurl-agent/1.0");
	  /* get it! */

	  res = curl_easy_perform(curl_handle_data );
	  if (res == CURLE_OK)
	  {

		  goto CLEANUP;
	  }
	  else if (res == CURLE_ABORTED_BY_CALLBACK)
	  {

		  exit_status = 1;
		  goto CLEANUP;
	  }
	  else
	  {

		    exit_status = 1;
		    goto CLEANUP;

	  }
CLEANUP:
#if DEBUG_UDT_TO_FILE
	fclose(debug_fp);
#endif

	curl_easy_cleanup(curl_handle_data);
	//ctx.session[id].stop_download_data = 2;
	return exit_status;
}





void* udt_client_thread(void* arg)
{
	UDTConnection* pUDTConn;
	char read_buffer[1024];
	char return_buffer[1024];

	int size = 0;
	char* ptr;
	answer_t type;
	int isCommandThread=0;
	pUDTConn = (UDTConnection*) arg;

	//printf("=======UDT CLIENT PID=%d====\n",getpid());
	size = wrapper_receive(pUDTConn->udt_fd,read_buffer,256,0);
	if (size <= 0)
	{

	    return NULL;
	}
	read_buffer[size]='\0';


	// Now parse the buffer or right now just temporarilly dump the data first
	//ptr = strstr(read_buffer,"action=appletvastream"); // appletvastream only at the moment
	
	isCommandThread = 1;
	type = PASSTHROUGH_COMMAND;
	if (strstr(read_buffer, "action=appletvastream") != NULL) {
	    isCommandThread = 0;
	    type = VAAPPLET;
	} else if (strstr(read_buffer, "action=appletastream") != NULL) {
		isCommandThread = 0;
		type = AAPPLET;
	} else if (strstr(read_buffer, "close_session") != NULL) {
		isCommandThread = 1;
		type = CLOSESESSION_COMMAND;
	}
	
	
	if (isCommandThread) // transfer command
	{
		if (type==PASSTHROUGH_COMMAND)
		{
		    if (pUDTConn->pcontext->session[pUDTConn->session_id].command_thread_num < MAX_COMMAND_THREAD_NUM)
		    {
			pUDTConn->pcontext->session[pUDTConn->session_id].command_thread_num++;
		    }
		    else
		    {

			wrapper_closeSocket(pUDTConn->udt_fd);
			free(pUDTConn);
			return NULL;
		    }
		    camera_process_send_command(read_buffer,return_buffer);

	    	    wrapper_send(pUDTConn->udt_fd,return_buffer,strlen(return_buffer),0);
		}
		else //if (type==CLOSESESSION_COMMAND) // wanna close session
		{

			udt_stop_session(pUDTConn->pcontext->session[pUDTConn->session_id].stream_id);
		    
		    wrapper_closeSocket(pUDTConn->udt_fd);
		    free(pUDTConn);

		    return NULL;
		}
	}
	else  // transfer data
	{

		char* sessionkey;
		char expectsessionkey[65];
		struct in_addr addr;
		isCommandThread = 0;
		if (pUDTConn->pcontext->session[pUDTConn->session_id].data_thread_num < MAX_DATA_THREAD_NUM)
		{
			pUDTConn->pcontext->session[pUDTConn->session_id].data_thread_num++;
		}
		else
		{

			wrapper_closeSocket(pUDTConn->udt_fd);
			free(pUDTConn);

			return NULL;
		}
		pUDTConn->pcontext->session[pUDTConn->session_id].idle_time = INT_MAX;

		sessionkey = strstr(read_buffer,"remote_session=");
		if ( sessionkey == NULL)
		{

			sprintf(expectsessionkey,"603");
			wrapper_send(pUDTConn->udt_fd,expectsessionkey,strlen(expectsessionkey),0);
			goto EXIT1;
		}
		sessionkey = sessionkey + sizeof("remote_session=") - 1;

		/*
		if (strlen(sessionkey) != 64)
		{
			printf("Error cant find remote_session123\n");
			sprintf(expectsessionkey,"401");
			wrapper_send(pUDTConn->udt_fd,expectsessionkey,strlen(expectsessionkey),0);
			goto EXIT;
		}
		*/

		strcpy(expectsessionkey,sessionkey);
		sprintf(read_buffer,"action=command&command=get_session_key&setup=%s.0",pUDTConn->pcontext->session[pUDTConn->session_id].randomstr);

		camera_process_send_command(read_buffer,return_buffer);

		if (strstr(return_buffer,"ERROR") != 0)
		{
			// something wrong
			printf("Error cant get SessionKey from MainApp\n");
			sprintf(expectsessionkey,"603");
			wrapper_send(pUDTConn->udt_fd,expectsessionkey,strlen(expectsessionkey),0);
			goto EXIT1;
		}
		return_buffer[65]='\0';

		if (strcmp(return_buffer,expectsessionkey) != 0)
		{
			sprintf(expectsessionkey,"601");
			wrapper_send(pUDTConn->udt_fd,expectsessionkey,strlen(expectsessionkey),0);
			goto EXIT1;
		}

		// Implement checking sessionkey here
		// TODO
		// Need to put the authentication here
		// Also how to count number of connection
		// Timeout also need to implement here now temp dump everything out
		//printf("pUDTConn->stop_download_data : %d\n",pUDTConn->stop_download_data);
		pUDTConn->stop_download_data = 0;
		camera_process_receive_data (pUDTConn,type);
EXIT1:
		// quit immediately
		pUDTConn->pcontext->session[pUDTConn->session_id].stop = 1;
		// notice callling thread will bring problem
		pUDTConn->pcontext->session[pUDTConn->session_id].idle_time = time(NULL);


	}
EXIT:

	//printf("Thread Connection Closed\n");
	// free HERE
	wrapper_closeSocket(pUDTConn->udt_fd);
	if (isCommandThread)
	{
		pUDTConn->pcontext->session[pUDTConn->session_id].command_thread_num--;
	}
	else
	{
		pUDTConn->pcontext->session[pUDTConn->session_id].data_thread_num--;
	}
	free(pUDTConn);
	//printf("Stop thread %lu\n",pthread_self());
	//pthread_exit(NULL);
	return NULL;
}

// NEED TO IMPLEMENT TIMEOUT
int udt_start_session(char* dest_ip, int dest_port,int local_port, char* stream_id,char* randomstr)
{
	struct addrinfo hints, *local;
	char portstr [16];
	int ret;
	int i,chosen;
	int new_udt_socket;
	struct sockaddr_in clientsock;
	int clientsocksize = sizeof (clientsock);
	pthread_t client_t;


	memset(&hints, 0, sizeof(struct addrinfo));
	sprintf(portstr,"%d",local_port);
	hints.ai_flags = AI_PASSIVE;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	//printf("1\n");
	if (0 != getaddrinfo(NULL, portstr, &hints, &local))
	{
	      printf( "incorrect network address.\n");
	      freeaddrinfo(local);
	      return 1;
	}
	//printf("2\n");
	// find a free slot
	chosen = -1;
	for (i=0;i<MAX_UDT_DATA_SESSION;i++)
	{
		if (ctx.session[i].notavail == 0)
		{

			chosen = i;
			break;
		}
	}
	//printf("3\n");
	if (chosen == -1)
	{

		freeaddrinfo(local);
		return -1;
	}
	memset(&ctx.session[chosen],0,sizeof(UDTSession));
	ctx.session[chosen].notavail = 1;
	ctx.num_session++;
	
	//ctx.session[chosen].curl_handle_data = curl_easy_init();
	ctx.session[chosen].stop = 0;
	//printf("4\n");
	
	ctx.session[chosen].udt_socket = wrapper_createSocket(local->ai_family,local->ai_socktype);
	/*
	{
	    int mss_size = 600;
	    printf("SETSOCKOPT HERE\n");
	    wrapper_setSockOpt(ctx.session[chosen].udt_socket,0,CUDT_MSS,&mss_size,sizeof(int));
	
	} 
	*/ 
	//printf("5\n");
	if (ctx.session[chosen].udt_socket < 0)
	{
	    printf("opensocket: %s\n",wrapper_getErrorMessage());
	    printf("ERRRROR**********************\n");
	    ret = 1;
	    goto EXIT;
	}
	
	{
	    int mss_size,error;
	    char rcvsync = 0;
	   mss_size = 1200;
	   error = wrapper_setSockOpt(ctx.session[chosen].udt_socket,0,CUDT_MSS,(const char*)&mss_size,sizeof(int));

	   //
	   error = wrapper_setSockOpt(ctx.session[chosen].udt_socket,0,CUDT_RCVSYN,&rcvsync,sizeof(char));


	   //mss_size = 100;
	   //wrapper_setSockOpt(new_udt_socket,0,CUDT_FC,&mss_size,sizeof(int));
	   mss_size = 100*1024;
	   error = wrapper_setSockOpt(ctx.session[chosen].udt_socket,0,CUDT_SNDBUF,(char*)&mss_size,sizeof(int));

	   mss_size = 100*1024;
	   error = wrapper_setSockOpt(ctx.session[chosen].udt_socket,0,CUDP_SNDBUF,(char*)&mss_size,sizeof(int));

	}
	strncpy(ctx.session[chosen].stream_id,stream_id,31);
	ctx.session[chosen].stream_id[31]='\0';
	strcpy(ctx.session[chosen].dest_ip,dest_ip);
	ctx.session[chosen].dest_port = dest_port;
	ctx.session[chosen].command_thread_num = 0;
	ctx.session[chosen].data_thread_num = 0;

	strncpy(ctx.session[chosen].randomstr,randomstr,9);

	// NO NEED TO SET SO_REUSEADDR
	// because default of UDT is ON
	/*
	on = 1;
	ret = wrapper_setsockopt(ctx.session[chosen].udt_socket, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on);
	if (ret < 0)
	{
		printf("Cant set sockopt SO_REUSEADDR\n");
	}
	 */

	ret = 0;
	printf("Binding\n");
	ret =  wrapper_bind(ctx.session[chosen].udt_socket,local->ai_addr, local->ai_addrlen);
	freeaddrinfo(local);
	if (0 != ret)
   {
	  printf ("Cant bind this port %d\n",local_port);
	  ret =  2;
	  goto EXIT;
   }

	ret =  wrapper_listen(ctx.session[chosen].udt_socket,1);
	if (0 != ret)
  {
	  printf ("Cant listen this port %d\n",local_port);
	  ret =  2;
	  goto EXIT;
  }

	ctx.session[chosen].idle_time = time(NULL);


   // Server Loop Task Here
  // For easy
  while(ctx.session[chosen].stop == 0)
   {
	   UDTConnection* pUDTConn;
	   int mss_size = 1050;
	   int error;
	   char sndsyn=0;
	   new_udt_socket = wrapper_accept(ctx.session[chosen].udt_socket,(struct sockaddr*)&clientsock,&clientsocksize);
	   if (new_udt_socket == -1)
	   {
		//   printf("accept: %s\n",wrapper_getErrorMessage());
		// timeout 2s each to check the stop
		   if ((time(NULL) - ctx.session[chosen].idle_time) >= MAX_IDLE_TIME)
		   {
			   printf("Idle TimeOut. Stop Session\n");
			   ctx.session[chosen].stop = 1;
		   }
		   sleep(2);
		   //printf("Accept timeout retries\n");
		   continue;
	   }
	   
	   mss_size = 1050;
	  // error = wrapper_setSockOpt(new_udt_socket,0,CUDT_MSS,&mss_size,sizeof(int));
	   //printf("CUDT_MSS=%d\n",error);
	   //mss_size = 100;
	   //wrapper_setSockOpt(new_udt_socket,0,CUDT_FC,&mss_size,sizeof(int));
	   
	   
	   //error = wrapper_setSockOpt(new_udt_socket,0,CUDT_SNDSYN,&sndsyn,sizeof(char));
	   //printf("CUDT_SNDSYN=%d\n",error);
	   //mss_size = 100*1024;
	   //error = wrapper_setSockOpt(new_udt_socket,0,CUDP_SNDBUF,(char*)&mss_size,sizeof(int));
	   //printf("CUDT_CUDP_SNDBUF=%d\n",error);
       //printf("set UDP buf: %s\n",wrapper_getErrorMessage());
 	   //mss_size = 100*1024;
 	   mss_size = 3*1000; // 3s
	   error = wrapper_setSockOpt(new_udt_socket,0,CUDT_SNDTIMEO,(char*)&mss_size,sizeof(int));

	   mss_size = 3*1000; // 3s
	   error = wrapper_setSockOpt(new_udt_socket,0,CUDT_RCVTIMEO,(char*)&mss_size,sizeof(int));

       //printf("set UDT buf: %s\n",wrapper_getErrorMessage());
       
	   // block here
	   pUDTConn = (UDTConnection*) malloc(sizeof(UDTConnection));
	   // free later in client thread
	   pUDTConn->pcontext = &ctx;
	   pUDTConn->client_addr = clientsock;
	   pUDTConn->session_id = chosen;
	   pUDTConn->udt_fd = new_udt_socket;
	   pUDTConn->stop_download_data = 0;

/*	   
	   if (pthread_create(&client_t, NULL, &udt_client_thread, pUDTConn) != 0) {
			printf("could not launch another client thread\n");
			wrapper_closeSocket(pUDTConn->udt_fd);
			free(pUDTConn);
			continue;
		}
		pthread_detach(client_t);
*/
	    udt_client_thread((void*)pUDTConn);

   }
    ret = 1; // to make it go to cleanup process

   //camera_process_receive_data(chosen);
   //ctx.session[chosen].stop_download_data = 0;
EXIT:

   if (ret)
   {
	   // close UDT Socket
	   if (ctx.session[chosen].udt_socket >= 0)
	   {
		   wrapper_closeSocket(ctx.session[chosen].udt_socket);
		   ctx.session[chosen].udt_socket = -1;
	   }
	   ctx.session[chosen].notavail = 0;
	   //ctx.session[chosen].udt_socket = -1;
	   ctx.num_session--;

	   return ret;
   }

   ctx.num_session--;
	return ret;
}

int udt_stop_session(char* stream_id)
{
	int i;
	// NEED A WAY TO STOP THE TRANSACTION AS WELL
	// STILL NEED TO HANDLE TIMEOUT - SERVER DEAD ETC ...
	// TODO : JOHN
	// used stream_id to check

	for (i=0;i<MAX_UDT_DATA_SESSION;i++)
	{
		if (ctx.session[i].notavail == 1)
		{
		    if (strcmp(ctx.session[i].stream_id,stream_id) == 0 )
		    {
			break;
		    }
		}
	}
	if (i >= MAX_UDT_DATA_SESSION)
	{

		return -1;
	}
	// send download stop
	ctx.session[i].stop = 1;
	while(ctx.session[i].notavail != 0)
	{

	    sleep(1);
	}
	/*
	if (ctx.session[i].udt_socket != 0) // not correct but we use memset so no choice
	{
		wrapper_closeSocket(ctx.session[i].udt_socket);
		ctx.session[i].udt_socket = 0;
	}
	memset(&ctx.session[i],0,sizeof(UDTSession));
	*/
	return 0;
}

int camera_is_available()
{
	if (ctx.num_session < MAX_UDT_DATA_SESSION)
	{
		return 1;
	}
	return 0;
}

/*
int camera_real_start_streaming((unsigned int* remote_add_val, int remote_port, char *stream_id, char *rand_num)
{

}
*/


int camera_process_init(char* ip)
{
	memset(&ctx,0,sizeof(ctx));
	strcpy(ctx.camera_ip,ip);
	ctx.camera_port = 80;
	ctx.credetial[0]='\0';
	//ctx.stop_download_data = 0;
	//ctx.dest_ip[0]='\0';
	//ctx.dest_port = -1;
	//ctx.udt_socket = -1;
	curl_global_init(CURL_GLOBAL_ALL);
    udt_init();
    //ctx.curl_handle_command = curl_easy_init();
	return 0;
}

int camera_process_deinit()
{
	//curl_easy_cleanup(ctx.curl_handle_command);
	udt_deinit();
	return 0;
}



//char server_name[128];
int main(int argc, char** argv)
{
	char buf[128];
	int choice;
	int ret;
	//pthread_t p_thread;
	//command_info cmd_info;
	char camera_ip [20];
	char dest_ip [20];
	char randomstring[9];
	char stream_id [30];
	int dest_port;
	int local_port;
  openlog("Stunbridge ", LOG_PID|LOG_CONS, LOG_USER);
  //openlog("MJPG-streamer ", LOG_PID|LOG_CONS|LOG_PERROR, LOG_USER);
  syslog(LOG_INFO, "starting application");
  syslog(LOG_INFO,"Build date %s - Build time %s\n",__DATE__,__TIME__);


#ifdef DEBUG_PCD
    set_coredump();
    PCD_API_REGISTER_EXCEPTION_HANDLERS();
#endif


  {
    pid_t pid;
    char str[50];
    pid = getpid();
    printf("PID = %d\n",pid);
    sprintf(str,"echo %d >/tmp/stunbridge.pid",pid);
    system(str);
  }


#ifdef SIMULATION
	ini_gets("General","CameraIP","127.0.0.1",camera_ip,20,"stunbridge.ini");
	ini_gets("General","DestIP","192.168.1.77",dest_ip,20,"stunbridge.ini");
	dest_port = ini_getl("General","DestPort",50005,"stunbridge.ini");
	local_port = ini_getl("General","LocalPort",9000,"stunbridge.ini");
	ini_gets("General","StreamId","HALOSTREAMDEFAULT",stream_id,30, "stunbridge.ini");
	ini_gets("General","RandomStr","12345678",randomstring,30, "stunbridge.ini");
	ini_gets("General","MacAddress","",debug_macaddr,20, "stunbridge.ini");
#else
	strcpy(camera_ip,CAMERA_IP);
	strcpy(dest_ip,DEST_IP);
	dest_port = DEST_PORT;
	debug_macaddr[0]='\0'; // set to 0 so that it will auto get correct mac
	sprintf(randomstring,"12345678");
	sprintf(stream_id,"HALOSTREAMDEFAULT");
	local_port = 9000;
	ini_gets("General","StunAppServerName","stun.monitoreverywhere.com",server_name,128,"/mlsrb/serverconfig.ini");
	server_stunport = ini_getl("General","StunAppPort",3478,"/mlsrb/serverconfig.ini");
	ini_gets("General","UPNPAppServerName","bms.monitoreverywhere.com",command_servername,128,"/mlsrb/serverconfig.ini");


#endif

	if (argc == 2) // just to test NAT
	{
	    int result=0;
	    printf("Checking Symmetric NAT only\n");
	    result = symmetricNATTest();
	    exit(0);
	}
	//signal(SIGABRT,SIG_IGN);
	//signal(SIGTERM,SIG_IGN);

	camera_process_init(camera_ip);
	relay_init();
#ifdef SIMULATION

	fgets(buf,127,stdin);
		printf("\n");
		choice = atoi(buf);
		switch (choice)
		{
			case 1:
				stunClient_main();
				break;
			case 2:

				//ret = udt_stop_session(stream_id);
				break;
			case 99:
				return 0;
			default:
				return 0;
		}

#else
	//ret = udt_start_session( dest_ip, dest_port,local_port, stream_id,randomstring);
	if (argc == 1)
	{
		//printf("Execute Normal Stun\n");
		stunClient_main();
	}
	else // FOR TESTING ONLY
	{
	    char superbuffer[128];
	    char relay_ip_address[]="192.168.3.115";
	    char channelID[]= "1234567890AB";
	    unsigned int relay_port = 1000;
	    unsigned int randomnumber = 20978;
	    unsigned int clientipnum = 9876543;
	    struct in_addr inp;
	    
	    inet_aton(relay_ip_address,&inp);
	    sprintf(superbuffer,"%08X.%08X.%08X.%08X.%s",inp.s_addr,relay_port,randomnumber,clientipnum,channelID);

	    relay_start_session(superbuffer);
	}
#endif

	return 0;
}
