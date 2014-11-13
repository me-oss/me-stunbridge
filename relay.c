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

#include "debuglog.h"

#include "minIni.h"


typedef struct _RelayConnection
{
	//context* pcontext;
//	int session_id;
	struct sockaddr_in client_addr;
	int relay_fd;
	int stop_download_data;
	int totalbytes;
	time_t starttime;
	pthread_t running_thread;
	char channelID[16];
	char sessionkey[66];
}RelayConnection;

static int isBusy = 0;

static size_t write_data(void *ptr, size_t size, size_t nmemb, void *userp) {
    size_t written;
	FILE* stream = (FILE*)userp;

    written = fwrite(ptr, size, nmemb, stream);
    return written;
}

RelayConnection conn;

//#define DEBUG_RELAY_TO_FILE 1
#if DEBUG_RELAY_TO_FILE
static FILE* debug_fp;
#endif



#define SEND_FAIL_MAX  5
static size_t send_relay_data(void *ptr, size_t size, size_t nmemb, void *userp) {
	size_t written = size * nmemb;
    unsigned int ssize = 0;
    int ss;
    time_t curtime;
    static int display_flag;
    static int megabytes = 0;
    static int count_fail=0;
    int transfer_bytes=0;
    RelayConnection* pRelayConn = (RelayConnection*)userp;


#if DEBUG_RELAY_TO_FILE
	write_data(ptr,size,nmemb,debug_fp); // for debug only log to file will need to remove it later
#endif

	curtime = time(NULL);
/*
	if ((curtime - pRelayConn->start_time) > TIMEOUT )
	{
		printf("TIMEOUT\n");
		pRelayConn->stop_download_data = 1;
		return 0;
	}
*/
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
	if ((pRelayConn->totalbytes/(1024*1024)) > megabytes)
	{

	    megabytes++;
	}
 
#if 1	
	while (ssize < written && pRelayConn->stop_download_data != 1)
	{
	    transfer_bytes = ((written - ssize) > 4096) ? (4096):(written-ssize);
	    ss = write(pRelayConn->relay_fd,(char*)(ptr+ssize), transfer_bytes);
	    //ss = written;

	    if (ss < 0)
	    {

			//! Check connection state
	    	// DO I NEED TO BREAK HERE OR DOING SOME RETRIES
	    	//??????????
	    		pRelayConn->stop_download_data = 1;

	    	

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
	    }
	    ssize += ss;
	}
#endif
	
	pRelayConn->totalbytes += ssize;
	
	return written;
}

/*
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
	// MAX_SIZE_OF_HTML_GET_VERSION is more than enough so ignore
	  printf("SOMETHING WRONG****** REPLY CANT BE BIGGER THAN 256 bytes\n");
	  return -1;
  }
  return realsize;
}
*/


int progress_func(void* ptr, double TotalToDownload, double NowDownloaded, double TotalToUpload, double NowUploaded)
{
    RelayConnection* pRelayConn = (RelayConnection*)ptr;
//    printf("1pUDTConn->stop_download_data : %d\n",pUDTConn->stop_download_data);
    // It's here you will write the code for the progress message or bar
	//printf("Already download %d bytes\n",NowDownloaded);
	if (pRelayConn->stop_download_data)
	{
		printf("User cancel\n");
		return 1;
	}
	return 0;
}




int relay_compute_sessionkey(unsigned int randomnumber, unsigned int dest_iplong, char* sessionkey)
{
    int result;
    char tmpstr[128];

    sprintf(tmpstr,"action=command&command=set_delay_output&setup=200");

    camera_process_send_command(tmpstr,sessionkey);
    

    sprintf(tmpstr,"action=command&command=get_session_key&setup=%08X.%08X",randomnumber,dest_iplong);

    camera_process_send_command(tmpstr,sessionkey);

    if (strlen(sessionkey) != 64)
	return -1;
    return 0;
}


int relay_extend_delay_rate(unsigned long delayinms)
{
    int result;
    char tmpstr[128];
    char retstr[128];
    sprintf(tmpstr,"action=command&command=set_delay_output&setup=%d",delayinms);

    camera_process_send_command(tmpstr,retstr);

    return 0;
}

#pragma pack(1)
typedef struct _RelayServerHandshake
{
    unsigned char type;
    unsigned short length;
    char ChannelID[12];
    char SessionKey[64];
}RelayServerHandshake;


typedef struct _RelayServerHandshakeReply
{
    unsigned char type;
    unsigned short length;
    unsigned long status;
}RelayServerHandshakeReply;

#pragma pack()

static void* relay_running_thread(void* arg)
{
    RelayConnection* pRelayConn = (RelayConnection*) arg;
    char cmd[256];
    int ret;
    int retries;
    RelayServerHandshake handshake;
    RelayServerHandshakeReply reply;
    
    CURLcode res;
    CURL* curl_handle_data;
    
    pRelayConn->starttime = time(NULL);
    

    sleep(3);

    handshake.type = 0;
    handshake.length = 79;
    strncpy(handshake.ChannelID,pRelayConn->channelID,12);
    strncpy(handshake.SessionKey,pRelayConn->sessionkey,64);
    
    retries = 5;
    while(retries--)
    {
	ret = write(pRelayConn->relay_fd,&handshake,sizeof(handshake));
	if (ret != sizeof(handshake))
	{
	    usleep(500000);
	    continue;
	}
    }
    if (retries == 0)
    {

	// HAVE TO IMPLEMENT QUITTING HERE
	ret = -1;
	goto ERROR;
    }
    
    ret = read (pRelayConn->relay_fd,&reply,sizeof(reply));
    if (ret != sizeof(reply))
    {

	// HAVE TO IMPLEMENT QUITTING HERE
	ret = -2;
	goto ERROR;
    }
    if (reply.status != 0)
    {

	// HAVE TO IMPLEMENT QUITTING HERE
	ret = -3;
	goto ERROR;
    }
    relay_extend_delay_rate(250);  // 4 FPS maximum




    // TESTING TESTING
    /*
    while (1)
    {
	char buf[4096];
	int ss;
	ss = write(pRelayConn->relay_fd,buf,4096);
	printf("ss=%d\n",ss);
	usleep(100000);
    }
*/
    
    curl_handle_data = curl_easy_init();
    if (curl_handle_data  == NULL)
    {

    	// HAVE TO IMPLEMENT QUITTING HERE
    	ret = -4;
    	goto ERROR;
    }

	 /* specify URL to get */
	  sprintf(cmd,"http://127.0.0.1/?action=appletvastream");
	  curl_easy_setopt(curl_handle_data, CURLOPT_URL, cmd);

	  /* send all data to this function  */
	  //curl_easy_setopt(ctx.curl_handle_data, CURLOPT_WRITEFUNCTION, write_data);
	  curl_easy_setopt(curl_handle_data , CURLOPT_WRITEFUNCTION, send_relay_data);
	  /* we pass our 'chunk' struct to the callback function */
#if DEBUG_RELAY_TO_FILE
	  	  debug_fp = fopen("/tmp/data.dump","wb");

	  if (debug_fp == NULL)
	  {
			printf("ERROR CANT CREATE FILE \n");
			    	// HAVE TO IMPLEMENT QUITTING HERE
        	    goto ERROR;
	  }

#endif
	  curl_easy_setopt(curl_handle_data , CURLOPT_WRITEDATA, (void *)pRelayConn);

	  curl_easy_setopt(curl_handle_data ,CURLOPT_CONNECTTIMEOUT,60);
	  curl_easy_setopt(curl_handle_data ,CURLOPT_FAILONERROR,1);
	  curl_easy_setopt(curl_handle_data ,CURLOPT_NOPROGRESS,0);
	  curl_easy_setopt(curl_handle_data ,CURLOPT_PROGRESSFUNCTION,progress_func);
	  curl_easy_setopt(curl_handle_data ,CURLOPT_PROGRESSDATA,(void*)pRelayConn);
	  curl_easy_setopt(curl_handle_data ,CURLOPT_BUFFERSIZE,4096);
	  /* some servers don't like requests that are made without a user-agent
		 field, so we provide one */
	  curl_easy_setopt(curl_handle_data , CURLOPT_USERAGENT, "libcurl-agent/1.0");
	  /* get it! */
	printf("Submit curl\n");
	  res = curl_easy_perform(curl_handle_data );
	  printf("CURL FINISH\n");
	  if (res == CURLE_OK)
	  {
	  }
	  else if (res == CURLE_ABORTED_BY_CALLBACK)
	  {
		  // TODO HERE
		  ret = 0;
		  goto ERROR;
	  }
	  else
	  {

//		    exit_status = 1;
//		    goto CLEANUP;
		    // TODOHERE

	  }
ERROR:
#if DEBUG_RELAY_TO_FILE
	fclose(debug_fp);
#endif

	curl_easy_cleanup(curl_handle_data);


    isBusy = 0;
    printf("Close Relay FD\n");
    close(pRelayConn->relay_fd);
    pRelayConn->relay_fd = -1;
    pRelayConn->running_thread = -1;
    printf("Quitting Thread\n");
    return NULL;
}

int relay_start_session(char* str)
{
    char* tmpstr;
    unsigned long relayIPlong,relayPortlong,randomnumberlong,clientIPlong;
    char *ptrstr,*tmpptr;
    char sessionidstr[32];
    char randomnumberstr[16];
    char iphexstr[16];
    char sessionkey[128];
//    struct sockaddr_in relay_ipsock;
//    struct sockaddr_
    int ret;
    int retries;
/*
    char relay_ip_address[]="192.168.3.130";
    struct in_addr inp;
*/	    

    //inet_aton(relay_ip_address,&inp);
    

    
    if (isBusy)
    {
	printf("System still busy with other Relay Request\n");
	return -1;
    }
    conn.running_thread = -1;
    isBusy = 1;
    conn.stop_download_data = 0;

    ret = sscanf(str,"%08X.%8X.%8X.%8X.%s",&relayIPlong,&relayPortlong,&randomnumberlong,&clientIPlong,sessionidstr);

    if (ret != 5)
    {
	printf("Server not send correct format\n");
        ret = -2;
        goto ERROR;
    }
    if (strlen(sessionidstr) != 12)
    {
	ret = -3;

     goto ERROR;
    }
    ret = relay_compute_sessionkey(randomnumberlong, clientIPlong,sessionkey);
    strcpy(conn.sessionkey,sessionkey);
    strcpy(conn.channelID,sessionidstr);
    if (ret != 0)
    {
	printf("Cant compute correct Session Key\n");
	ret = -4; 	
	goto ERROR;
    }
/*
    {
	printf("Mod to test\n");
	inet_aton(relay_ip_address,&inp);
	relayIPlong = inp.s_addr;
	relayPortlong = 1000;
    }
*/
    
    // NOW SETUP THE CONNECTION BACK TO TCP SERVER
    // need to check whether already open or not (Later)
    conn.relay_fd = CreateASocket();
    if (conn.relay_fd == -1) 
    {
	printf("Cant create Socket\n");
	ret = -5;
	isBusy = 0;
	return ret;
    }

    conn.client_addr.sin_family = AF_INET;
    conn.client_addr.sin_addr.s_addr = relayIPlong;
    conn.client_addr.sin_port = htons((unsigned short)relayPortlong);

    //conn.client_addr.sin_addr.s_addr = ;
//    inet_aton("192.168.3.115",&(conn.client_addr.sin_addr));
//    conn.client_addr.sin_port = htons(1000);
    
    retries = 5;
    while(retries --)
    {
	ret = connect(conn.relay_fd, (struct sockaddr *) &(conn.client_addr), sizeof (conn.client_addr));
	if (ret < 0)
	{

	    usleep(300000);
	    continue;
	}
	break;
    }
    if (retries == 0)
    {

	close(conn.relay_fd);
	conn.relay_fd = -1;
	ret = -6;
	isBusy = 0;
	return ret;
    }
    // start the thread which will do the uploading data to server
    
    pthread_create(&conn.running_thread,NULL,&relay_running_thread,&conn);
//    while (1)
//    {
//	sleep(3);
//    }
    return 0;
    
ERROR:

    //ret = -1;
    isBusy = 0;
    return ret;

}

int relay_stop_session(char* stream_id)
{
    int retries = 3;

    if (conn.running_thread != -1)
    {
	conn.stop_download_data = 1;
	while(conn.running_thread != -1 && (retries > 0))
	{
	    printf("Wait till relay stop\n");
	    sleep(1);
	}
    }
    conn.running_thread = -1;
    isBusy = 0;
    return 0;
}

int relay_is_running()
{
    return isBusy;
}
int relay_init()
{
    conn.running_thread = -1;
}