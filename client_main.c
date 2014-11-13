/* File Name 	: BMS STUN client 
 * Author 	: H Sudha,Cupola Technology
 * Version 	: 1.0
 * Description 	: This file contains the complete implementation on the STUN framework for the BMS STUN client.
 * 		  All the send, receive and interface with the STUN Server and the Camera are covered as part of this file.
 * Revision 	: 1.0
 * Date         : 24th Feb 2012
 */
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <netdb.h>
#include <sys/wait.h>
#include <pthread.h>
#include "pjlib.h"
#include "pjlib-util.h"
#include "pjnath/stun_sock.h"
#include "pjnath/errno.h"
#include "pjnath/stun_transaction.h"
#include "pjnath/stun_session.h"
#include "pjlib-util/srv_resolver.h"
#include "pj/activesock.h"
#include "pj/addr_resolv.h"
#include "pj/array.h"
#include "pj/assert.h"
#include "pj/ip_helper.h"
#include "pj/timer.h"
#include "pj/os.h"
#include "pj/string.h"
#include "pj/errno.h"
#include "pj/lock.h"
#include "pj/log.h"
#include "pj/pool.h"
#include "pj/rand.h"
#include "pj/sock.h"


#include "stunbridge.h"
#include "debuglog.h"

#undef PJ_LOG
#define PJ_LOG(...)

/* Remove the definition below, when the main function needs to be called in context of other camera function */
//#define STUN_MAIN 1
#ifndef STUN_MAIN
int stunClient_main();
#endif

#define THIS_FILE 					"BMS Client"
#define CONFIG_FILE /etc/stun_client.conf

extern char server_name[];
extern int server_stunport;
#define SERVER_PORT 					server_stunport
#define SERVER_IP 		server_name


#define MAC_ADDR_SIZE 					13
#define MAC_GEN_FAILURE         			1

#define CAMERA_AVAILABLE 				1

/* Connection type indicators for the server */
#define DATA_CONNECTION 				"dc"
#define CONTROL_CONNECTION 				"cc"

/* Connection type length */
#define CONNECTION_LENGTH				2

/* Thread name */
#define THREAD_NAME_CS_WT 				"Control session worker thread"
#define THREAD_NAME_DS_WT       			"Data session worker thread"
#define THREAD_NAME_DS          			"Data session thread"

/* Data session memory cleanup parameter count */
#define MAX_DS_MEM_PARAM_DEL_COUNT 			100

/* Thread name length */
#define THRD_NAM_LEN_CS_WT 				30
#define THRD_NAM_LEN_DS_WT      			27
#define THRD_NAM_LEN_DS         			20

/* System interface type (0 if Local interface, 1 if Wifi interface) */
#define INTERFACE_TYPE          			0

/* System interface type (0 if Local interface, 1 if Wifi interface) */
#ifdef SIMULATION
#define INTERFACE_TYPE          0

/* System interface name */
#define LOCAL_INTERFACE 				"eth0" 
#define WIFI_INTERFACE 					"wlan0"
#else
#define INTERFACE_TYPE          0
#define WIFI_INTERFACE 		"ra0"
/* System interface name */
#define LOCAL_INTERFACE 	"ra0" 

#endif



/* Keep alive time (in seconds) */
#define KEEP_ALIVE_TIME_SEC     15
#define KEEP_ALIVE_TIME_MSEC    			0

/* Keep alive timer ID */
#define KEEP_ALIVE_ID 					100

/* The maximum no of attributes */
#define MAX_ATTR_COUNT 					10   

/* Attribute Mask definitions */
#define MASK_ATTR_USERNAME 				0x01
#define MASK_ATTR_CMD_TYPE 				0x02
#define MASK_ATTR_STREAM_ID 				0x04
#define MASK_ATTR_REMOTE_ADDR 				0x08

/* Camera availability message */
#define CAMERA_AVAILABLE_MSG 				"status:camera available "

/* Remote portal/video client message */
#define REMOTE_MSG 					"Sending message to the remote server"
#define MAX_REMOTE_MSG_LEN 				36

/* Size of receiving buffer */
#define MAX_PACKET_SIZE 				256

/* Memory size for STUN configuration */
#define MAX_MEM_POOL_SIZE 				1024
#define MAX_RUN_MEM_POOL_SIZE 				1024
#define TIMER_SIZE 					1024
#define IO_MEM 						16

/* Time delay for I/O queue polling */
#define TIME_DELAY_IO_POLL      			10      

/* Commandtype, use string to be sync with the command type relay used between the portal and camera */
#define START_STREAMING 				"one"
#define STOP_STREAMING 					"two"
#define CAM_STATUS 					"three"
#define COMMAND 					"four"

/* Command type length */
#define MAX_CMD_TYE_LEN 				10

/* Command length */
#define MAX_COMMAND_LEN 				200

/* Command response length */
#define MAX_COMMAND_RES_LEN     			24

/* Random number length */
#define MAX_RANDOM_NUM_LEN 				9

/* Stream ID length */
#define MAX_STREAM_ID_LENGTH    			16

static void my_perror(const char *title, pj_status_t status)
{
	char errmsg[PJ_ERR_MSG_SIZE];  				
	pj_strerror(status, errmsg, sizeof(errmsg));
	printf( "%s: %s", title, errmsg);
}

#define CHECK(expr)     status=expr; \
			if (status!=PJ_SUCCESS) { \
					        	my_perror(#expr, status); \
							return status; \
						}

/* Control connection structure */
static struct _control_context
{
	int	       	       state;
	int		       mac_valid;
	pj_stun_config         stun_cfg;
	pj_caching_pool        cp;
	pj_pool_t              *pool;
	pj_sockaddr            dst_addr;
	pj_sockaddr            nat_addr;
	pj_sockaddr            local_addr;
	pj_stun_session        *sess;
	pj_stun_session_cb     sess_cb;
	pj_sock_t              sock;    
        pj_thread_t            *thread;
	pj_timer_entry         ka_timer_entry;
}control_context;

typedef struct _data_sess
{
	int                     state_ds;
	int 			quit_ds;
	pj_caching_pool         cp_ds;
	pj_pool_t               *pool_ds;
	pj_sockaddr             remote_addr_ds;
	pj_sockaddr             local_addr_ds;
	pj_stun_session_cb      sess_cb_ds;
	pj_sock_t               sock_ds;
	pj_thread_t             *thread_ds;
	char                    stream_id_ds[MAX_STREAM_ID_LENGTH];
	char                    random_num_ds[MAX_RANDOM_NUM_LEN];
}data_sess;

struct _ds_del_param_list
{
	pj_thread_t 		*thread_del_ptr;
	pj_pool_t   		*del_pool_ptr;
	data_sess               *data_context;
}ds_del_param_list[MAX_DS_MEM_PARAM_DEL_COUNT];


/* MACADDRESS */
char mac_addr[MAC_ADDR_SIZE] = {'\0'};

/* Streamer functions */
int is_streamer_available (void);
int start_streaming(pj_sockaddr *remote_add,pj_sockaddr *local_add,char *stream_id,char *rand_num);
void stop_streaming(char *stream_id);
char *camera_process_command(char *stream_id, char *cmd);

/* STUN Client functions */
int get_mac_address(char *mac_addr);
static int worker_thread_control(void *unused);
void delete_ds_thread(pj_thread_t *thrd_pointer,pj_pool_t *pool_del_ds,data_sess *data_sess_context);
void ka_timer_callback(pj_timer_heap_t  *timer_heap, pj_timer_entry  *entry);
pj_status_t  ctrl_send_stun_request(void);
pj_status_t send_msg_control(pj_stun_session *sess,void *token,const void *pkt,pj_size_t pkt_size,const pj_sockaddr_t *dst_addr,unsigned addr_len);
void ctrl_handle_stun_response(pj_stun_session *sess,pj_status_t status,void *token,pj_stun_tx_data *tdata,const pj_stun_msg *response,const pj_sockaddr_t *src_addr,unsigned sad_len);
pj_status_t ctrl_handle_binding_request(pj_stun_session *sess,const pj_uint8_t *pkt,unsigned pkt_len,const pj_stun_rx_data *rdata,void *token,const pj_sockaddr_t *src_addr,unsigned sad_len);
pj_status_t init_data_session(data_sess *data_session_context);
pj_status_t send_msg_data(pj_stun_session *sess,void *token,const void *pkt,pj_size_t pkt_size,const pj_sockaddr_t *dst_addr,unsigned addr_len);
void data_handle_stun_response(pj_stun_session *sess,pj_status_t status,void *token,pj_stun_tx_data *rdata,const pj_stun_msg *response,const pj_sockaddr_t *src_addr,unsigned sad_len);


// JOHN ADD FOR RELAY PART
int isRelayEnable = 0;


/* Function Name : main
 * Input         : None
 * Output        : 1 on failure
 * Description   : This is the main while loop for the client functionality.
 */
#ifdef STUN_MAIN
int main()
#else
int stunClient_main()
#endif
{
	pj_size_t *parsed_len;  	
	pj_size_t parsed_val = 0;
	pj_status_t status;
	pj_status_t rec;
	parsed_len = &parsed_val;
	pj_hostent res_serv_addr;
	pj_str_t serv_addr;
	int ka_timer_id = KEEP_ALIVE_ID;

	// JOHN ADD
	{
	    FILE* fp;
	    fp = fopen("/tmp/symmetricNAT","r");
	    if (fp == NULL) 
	    {
		isRelayEnable = 0;

	    }
	    else
	    {
		fclose(fp);
		isRelayEnable = 1;

	    }
	}

	/* MACADDRESS received */
	if (get_mac_address(mac_addr) == MAC_GEN_FAILURE)
	{

		exit(1);
	}
	
	/* Check if the streamer is available, if not log and exit */
/*
	if (!(is_streamer_available()))
	{
		printf( "Streamer not available"); 
		exit(1);
	}		
*/	
	/* Library initialization */
	CHECK( pj_init() );
	CHECK( pjlib_util_init() );
	CHECK( pjnath_init() );

	/* Caching pool initialization for Control session */
	pj_caching_pool_init(&control_context.cp, &pj_pool_factory_default_policy, 0);
	
	/* Memory allocation for Control session */	
	control_context.pool = pj_pool_create(&control_context.cp.factory, "BMS client", MAX_MEM_POOL_SIZE, MAX_RUN_MEM_POOL_SIZE, NULL);
	if (!control_context.pool) 
	{

		exit(1);
	}

	/* STUN config initialization */
	pj_stun_config_init(&control_context.stun_cfg, &control_context.cp.factory, 0, NULL, NULL);

	/* Global timer heap creation */
	status = pj_timer_heap_create(control_context.pool, TIMER_SIZE, &control_context.stun_cfg.timer_heap);
	if (status != PJ_SUCCESS)
	{

		exit(1);
	}

	/* Create global ioqueue */
	status = pj_ioqueue_create(control_context.pool, IO_MEM, &control_context.stun_cfg.ioqueue);                   
	if (status != PJ_SUCCESS)
	{

		exit(1);
	}

	/* Keep-alive timer initialization */
	if (pj_timer_entry_init(&control_context.ka_timer_entry, ka_timer_id, NULL, &ka_timer_callback) == NULL)
	{

		exit(1);
	}

	/* Server address conversions */
	serv_addr = pj_str(SERVER_IP);
	rec = pj_gethostbyname(&serv_addr, &res_serv_addr);
	if (rec != PJ_SUCCESS)
	{

		exit(1);
        }

	/* Server address particulars */
	control_context.dst_addr.ipv4.sin_family = PJ_AF_INET;                                     		
	control_context.dst_addr.ipv4.sin_port = htons(SERVER_PORT);                               	
	control_context.dst_addr.ipv4.sin_addr.s_addr = *(pj_uint32_t *)res_serv_addr.h_addr;       		
	
	/* Callback initializations for the Control session */
	pj_bzero(&control_context.sess_cb, sizeof(control_context.sess_cb));
	control_context.sess_cb.on_rx_request = &ctrl_handle_binding_request;
	control_context.sess_cb.on_send_msg  = &send_msg_control;
	control_context.sess_cb.on_request_complete = &ctrl_handle_stun_response;

	/* Start the worker thread */
	status = pj_thread_create(control_context.pool, "Control session worker thread", &worker_thread_control, NULL, PJ_THREAD_DEFAULT_STACK_SIZE, 0, &control_context.thread);	
	if (status != PJ_SUCCESS)
	{

		exit(1);
	}


	
	/* Control socket creation of client */
	status = pj_sock_socket(pj_AF_INET(), pj_SOCK_DGRAM(), 0, &control_context.sock);
	if (status != PJ_SUCCESS)
       	{

		exit(1);
	}



	/* Creation of STUN Control session */
	status = pj_stun_session_create(&control_context.stun_cfg, "BMS client", &control_context.sess_cb, PJ_FALSE, &control_context.sess);
	if (status != PJ_SUCCESS) 
	{

		exit(1);
        }
	


	/* Binding the STUN Client with STUN Server on Control connection */
	pj_sock_bind(control_context.sock, (pj_sockaddr_t *)&control_context.dst_addr, sizeof(control_context.dst_addr));

	/* Call function to send STUN Request */
	status = ctrl_send_stun_request();
	if (status != PJ_SUCCESS) 
	{

	}
	
	while(1)
	{

	    sleep(2);
	}
	return 1;
}



/* Function Name : ctrl_send_stun_request
 * Input         : None
 * Output        : status = PJ_SUCCESS if success else -1
 * Description   : This function sends STUN Binding Request to the STUN server on Control connection.
 */
pj_status_t  ctrl_send_stun_request(void)
{
	int req_type = PJ_STUN_BINDING_REQUEST;                                                   			                                                   	
	pj_str_t value_cntrl;
	pj_ssize_t len;                                                                         			
	int src_len,msg_invalid;
	pj_stun_tx_data *p_tdata;                                                               			
	pj_sockaddr src_addr_of_pkt;                                                            			
	pj_uint8_t packet[MAX_PACKET_SIZE];
	pj_size_t *parsed_len;                                                                  			
	pj_size_t parsed_val = 0;
	pj_status_t status;
	pj_str_t value;
	parsed_len = &parsed_val;
	
	/* Parsed length content validation */	
	if (parsed_len == NULL)
	{

		return -1;
	}
	
	/* Software attribute set to NULL*/
	status = pj_stun_session_set_software_name(control_context.sess, NULL);
	if (status != PJ_SUCCESS) 
	{

		return status;
	}

	/* Stun request creation */
	status = pj_stun_session_create_req(control_context.sess, req_type, PJ_STUN_MAGIC , NULL, &p_tdata);
	if (status != PJ_SUCCESS) 
	{

		return status;
	}

	/* Inclusion of connection_type attribute in the STUN request */
	status = pj_stun_msg_add_string_attr(p_tdata->pool, p_tdata->msg, CONNECTION_TYPE, pj_cstr(&value_cntrl,CONTROL_CONNECTION));
	if (status != PJ_SUCCESS) 
	{

		return status;
	}	

	/* Inclusion of username attribute in the STUN request */
	status = pj_stun_msg_add_string_attr(p_tdata->pool, p_tdata->msg, PJ_STUN_ATTR_USERNAME, pj_cstr(&value,mac_addr));
	if (status != PJ_SUCCESS) 
	{

		return status;
	}

	/* Sending STUN Binding request */
	status = pj_stun_session_send_msg(control_context.sess, NULL, PJ_FALSE, PJ_TRUE, (pj_sockaddr_t *)&control_context.dst_addr, sizeof(control_context.dst_addr), p_tdata);
	if (status != PJ_SUCCESS) 
	{

		return status;
	}

	

	while (1)
	{
		msg_invalid =0;
		printf("Waiting on the Control connection");
		/* Receiving on the control socket */
		src_len = sizeof(src_addr_of_pkt);
		len = sizeof(packet);
		status = pj_sock_recvfrom(control_context.sock, packet, &len, 0, &src_addr_of_pkt, &src_len);
		if (status != PJ_SUCCESS) 
		{
			printf( "Error in receiving the STUN Binding request/response from the STUN server on the Control connection");
			return status;
		}



		/* STUN message check */
		status = pj_stun_msg_check(packet, len, PJ_STUN_CHECK_PACKET);
		if (status != PJ_SUCCESS) 
		{
			printf( "Not a valid STUN packet");
			msg_invalid = 1;
		}

		if (!msg_invalid)
		{
			printf("The received Data packet is a valid STUN message");

			/* Notification to respective callback functions on receiving STUN packet */
			status = pj_stun_session_on_rx_pkt(control_context.sess, packet, sizeof(packet), PJ_STUN_IS_DATAGRAM, NULL, parsed_len, &control_context.dst_addr, sizeof(control_context.dst_addr));
			if (status != PJ_SUCCESS) 
			{
				printf( "Error in notification for callbacks in the Control session, pj_stun_session_on_rx_pkt()");
				return status;
			}
		}
	}
	return PJ_SUCCESS;
}



/* Function Name : send_msg_control
 * Input         : STUN session instance, token, pointer to packet, packet length, STUN server address, STUN server address length
 * Output        : status = PJ_SUCCESS if success else -1
 * Description   : This function is a callback of pj_stun_session_send_msg().It sends the STUN data packet to the STUN server on the Control connection.
 */
pj_status_t send_msg_control(pj_stun_session *sess,void *token,const void *pkt,pj_size_t pkt_size,const pj_sockaddr_t *dst_addr,unsigned addr_len)
{
	pj_ssize_t len = pkt_size;
	PJ_UNUSED_ARG(sess);
	PJ_UNUSED_ARG(token);
	return pj_sock_sendto(control_context.sock, pkt, &len, 0, dst_addr, addr_len);
}



/* Function Name : ctrl_handle_stun_response
 * Input         : STUN session instance, status of request, token, original STUN request, response message, source address of response, source address length
 * Output        : None
 * Description   : This function is a callback of pj_stun_session_on_rx_pkt().It is called on receiving the STUN response for the STUN Binding Request sent on Control connection.It checks for  *		   acknowledgement from STUN server when STUN server is booting up
 */
void ctrl_handle_stun_response(pj_stun_session *sess,pj_status_t status,void *token,pj_stun_tx_data *tdata,const pj_stun_msg *response,const pj_sockaddr_t *src_addr,unsigned sad_len)
{
	int i;
	pj_time_val  ka_delay = { KEEP_ALIVE_TIME_SEC, KEEP_ALIVE_TIME_MSEC };
	if (response == NULL && status == PJNATH_ESTUNTIMEDOUT && control_context.state == 0)
	{
		printf( "********************Retransmission of the STUN Binding Request on the Control connection failed");
		/* Scheduling the keep-alive timer */
		status = pj_timer_heap_schedule(control_context.stun_cfg.timer_heap, &control_context.ka_timer_entry, &ka_delay);
		if (status != PJ_SUCCESS) 
		{
			printf( "****************Error in scheduling the keep-alive timer");
			exit(1);
		}
		return;
	}
	
	if (!control_context.state)
	{
		if (!strcmp(pj_stun_get_class_name(response->hdr.type),"success response"))
		{
			printf( "STUN success response received");
			for(i=0;i<response->attr_count;i++)
			{
				if (response->attr[i]->type == ACK)
				{
					printf( "Acknowledgement received");

					/* Scheduling the keep-alive timer */
					status = pj_timer_heap_schedule(control_context.stun_cfg.timer_heap, &control_context.ka_timer_entry, &ka_delay);
					if (status != PJ_SUCCESS) 
					{
						printf( "****************Error in scheduling the keep-alive timer");
						exit(1);																						}
					
					/* Control session established successfully */
					control_context.state = 1; 				
				}
			}
		}
		else
		{
			printf( "Error response received in Acknowledgement for very first STUN Request");
		}
	}
}



/* Function Name : ctrl_handle_binding_request
 * Input         : STUN session instance, pointer to packet, packet length, incoming request message, token, source address of packet, source address length
 * Output        : status = PJ_SUCCESS if success else -1
 * Description   : This function is a callback of pj_stun_session_on_rx_pkt().It is called on receiving the STUN Binding Request on Control connection.It captures the attributes in the STUN 
 * 		   Binding Request that are required for Start streaming, Stop streaming and Camera status.Appropriate error messages are sent on receiving unknown, invalid and insufficient 
 * 		   attributes.
 */
pj_status_t ctrl_handle_binding_request(pj_stun_session *sess,const pj_uint8_t *pkt,unsigned pkt_len,const pj_stun_rx_data *rdata,void *token,const pj_sockaddr_t *src_addr,unsigned sad_len)
{
	int mask = 0,check = 0;
	char cmd_type[MAX_CMD_TYE_LEN];
	int attr_count;
	int str_count;
	pj_stun_tx_data *p_tdata; 
	int stream_status;
	pj_status_t status;
	pj_sockaddr remote_addr;
	char user_name[MAC_ADDR_SIZE];
	pj_str_t value_cntrl,val,str_val;
	pj_str_t mac_val,cmd_val,comm_res;
	const pj_stun_string_attr *attr;
	char command[MAX_COMMAND_LEN];
	char stream_id[MAX_STREAM_ID_LENGTH];
	char random_num[MAX_RANDOM_NUM_LEN];
	char *cam_process_ret;
	char cam_process_truncated[MAX_COMMAND_RES_LEN+1] = {'\0'};
	data_sess *data_sess_context;

	PJ_LOG(2,(THIS_FILE, "STUN Binding request received from the STUN server"));
	PJ_LOG(2,(THIS_FILE, "Attributes captured from the Binding request are:"));
	for(attr_count=0;attr_count<rdata->msg->attr_count;attr_count++)
	{
		switch (rdata->msg->attr[attr_count]->type)
		{
			case PJ_STUN_ATTR_STREAM_ID:
			{
				attr = (pj_stun_string_attr*)rdata->msg->attr[attr_count];   
				for(str_count=0; str_count<attr->hdr.length;str_count++)
                                {
                                        stream_id[str_count] = *(attr->value.ptr+str_count);
                                }
                                stream_id[attr->hdr.length] = '\0'; 			
				mask|=MASK_ATTR_STREAM_ID; 
				printf( "Stream ID = %s",stream_id);
			}break;	

			case PJ_STUN_ATTR_REMOTE_ADDR:
			{
		                const pj_stun_sockaddr_attr *attr;
				attr = (const pj_stun_sockaddr_attr*)rdata->msg->attr[attr_count];
				remote_addr.ipv4.sin_family = attr->sockaddr.ipv4.sin_family;
				remote_addr.ipv4.sin_addr.s_addr = attr->sockaddr.ipv4.sin_addr.s_addr;
				remote_addr.ipv4.sin_port = attr->sockaddr.ipv4.sin_port;
			        mask|=MASK_ATTR_REMOTE_ADDR;
				printf( "Remote address IP = %s",pj_inet_ntoa(remote_addr.ipv4.sin_addr));
				printf( "Remote address port = %d",pj_ntohs(remote_addr.ipv4.sin_port));
			}break;

			case  PJ_STUN_ATTR_USERNAME:
			{
				attr = (pj_stun_string_attr*)rdata->msg->attr[attr_count];
				if (attr->hdr.length > MAC_ADDR_SIZE)
                                {
					printf( "USERNAME length error");
                                }

				for(str_count=0; str_count<attr->hdr.length;str_count++)
                                {
                                        user_name[str_count] = *(attr->value.ptr+str_count);
                                }
                                user_name[attr->hdr.length] = '\0';
				if(!strcmp(mac_addr,user_name))
				{
					check = 1;
				}
				mask|=MASK_ATTR_USERNAME;
				printf( "Username (MACADDRESS) = %s",user_name);
			}break;

			case COMMAND_TYPE:
			{
				attr = (pj_stun_string_attr*)rdata->msg->attr[attr_count];
				if (attr->hdr.length > MAX_CMD_TYE_LEN)
				{
					printf( "COMMAND_TYPE length error");
					//printf("COMMAND_TYPE length error");
				}
				for(str_count=0; str_count<attr->hdr.length;str_count++)
				{
					cmd_type[str_count] = *(attr->value.ptr+str_count);
				}
				cmd_type[attr->hdr.length] = '\0';
				mask|=MASK_ATTR_CMD_TYPE; 
				//printf( "Command type = %s",cmd_type));
				printf("Command type = %s",cmd_type);
			}break;
			
			case PJ_STUN_ATTR_MAPPED_ADDR:
			{
				const pj_stun_sockaddr_attr *attr;
				attr = (const pj_stun_sockaddr_attr*)rdata->msg->attr[attr_count];
				control_context.nat_addr.ipv4.sin_family = attr->sockaddr.ipv4.sin_family;
				control_context.nat_addr.ipv4.sin_addr.s_addr = attr->sockaddr.ipv4.sin_addr.s_addr;
				control_context.nat_addr.ipv4.sin_port = attr->sockaddr.ipv4.sin_port;
				printf( "NAT IP address = %s",pj_inet_ntoa(control_context.nat_addr.ipv4.sin_addr));
				printf( "NAT IP port = %d",pj_ntohs(control_context.nat_addr.ipv4.sin_port));
			}break;

			case  PJ_STUN_ATTR_XOR_MAPPED_ADDR:
			{
			}break;
			
			case PJ_STUN_COMMAND:
			{
				attr = (pj_stun_string_attr*)rdata->msg->attr[attr_count];
				if (attr->hdr.length > MAX_COMMAND_LEN)
				{
					//printf( "COMMAND length error"));																		
					printf("COMMAND length error");
				}
	   			for(str_count=0; str_count<attr->hdr.length;str_count++)
				{
					command[str_count] = *(attr->value.ptr+str_count);																	
				}
				command[attr->hdr.length] = '\0';																			        
				//printf( "Camera process command string = %s",command));																	
				printf("Camera process command string = %s",command);
			}break;
			
			case PJ_STUN_RANDOM_NUMBER:
			{
				attr = (pj_stun_string_attr*)rdata->msg->attr[attr_count];
				if (attr->hdr.length > MAX_RANDOM_NUM_LEN)
				{
					printf( "RANDOM NUMBER length error");
				}
				for(str_count=0; str_count<attr->hdr.length;str_count++)
				{
					random_num[str_count] = *(attr->value.ptr+str_count);
				}
				random_num[attr->hdr.length] = '\0';
				printf( "Random number type = %s",random_num);
			}break;
				
			default:
			{
				printf( "Unexpected attribute received, sending error response to STUN server");

				/* STUN error response creation */
				status = pj_stun_session_create_res(control_context.sess, rdata, PJ_STUN_UNEXPECTED_ATTRIBUTE, NULL,&p_tdata);
				if (status != PJ_SUCCESS) 
				{
					printf( "Error in STUN error response creation,pj_stun_session_create_res()");
		                        return status;
				}
														
				/* Inclusion of connection type attribute in the STUN error response */
				status = pj_stun_msg_add_string_attr(p_tdata->pool, p_tdata->msg, CONNECTION_TYPE, pj_cstr(&value_cntrl,CONTROL_CONNECTION));
				if (status != PJ_SUCCESS) 
				{
					printf( "Error in inclusion of CONNECTION_TYPE attribute, pj_stun_msg_add_string_attr()");
		                        return status;
				}
				
				/* Sending STUN error response */
				status = pj_stun_session_send_msg(control_context.sess, NULL, PJ_FALSE, PJ_TRUE, src_addr, sad_len, p_tdata);
				if (status != PJ_SUCCESS) 
				{
					printf( "Error in sending STUN error response for obtaining unknown attribute, pj_stun_session_send_msg()");
					return status;
				}

				return PJ_SUCCESS;
			}break;
		}
	}

	if (check != 1)
	{
		printf("Username authentication failed, sending error response to the STUN server");
		
		/* STUN error response creation */
		status = pj_stun_session_create_res(control_context.sess, rdata, PJ_STUN_SC_UNAUTHORIZED, NULL, &p_tdata);
		if (status != PJ_SUCCESS) 
		{
			printf( "Error in STUN error response creation,pj_stun_session_create_res()");
		        return status;
		}

		/* Inclusion of connection type attribute in the STUN error response */
		status = pj_stun_msg_add_string_attr(p_tdata->pool, p_tdata->msg, CONNECTION_TYPE, pj_cstr(&value_cntrl,CONTROL_CONNECTION));
		if (status != PJ_SUCCESS) 
		{
			printf( "Error in inclusion of CONNECTION_TYPE attribute, pj_stun_msg_add_string_attr()");
		        return status;
	        }
		
		/* Inclusion of Stream ID attribute in the STUN error response */
		status = pj_stun_msg_add_string_attr(p_tdata->pool, p_tdata->msg, PJ_STUN_ATTR_STREAM_ID, pj_cstr(&str_val,stream_id));
		if (status != PJ_SUCCESS)
		{
			printf( "Error in inclusion of PJ_STUN_ATTR_STREAM_ID attribute, pj_stun_msg_add_string_attr()");
			return status;
                }

		/* Inclusion of command type attribute in the STUN error response */
		status = pj_stun_msg_add_string_attr(p_tdata->pool, p_tdata->msg, COMMAND_TYPE, pj_cstr(&cmd_val,cmd_type));
		if (status != PJ_SUCCESS) 
		{
			printf( "Error in inclusion of COMMAND_TYPE attribute, pj_stun_msg_add_string_attr()");
			return status;
		}
		
		/* Sending STUN error response */
		status = pj_stun_session_send_msg(control_context.sess, NULL, PJ_FALSE, PJ_TRUE, src_addr, sad_len, p_tdata);
		if (status != PJ_SUCCESS) 
		{
			printf( "Error in sending STUN error response for username authentication failure, pj_stun_session_send_msg()");
			return status;
		}																					        
		
		return PJ_SUCCESS;
	}


	if (!(mask & MASK_ATTR_CMD_TYPE))
	{
		PJ_LOG(2,(THIS_FILE, "Command_type attribute missing, sending error response to the STUN server"));
		
		/* STUN error response creation */
		status = pj_stun_session_create_res(control_context.sess, rdata, PJ_STUN_SC_BAD_REQUEST, NULL, &p_tdata);
		if (status != PJ_SUCCESS) 
		{
			printf( "Error in STUN error response creation,pj_stun_session_create_res()");
		        return status;
		}

		/* Inclusion of connection type attribute in the STUN error response */
		status = pj_stun_msg_add_string_attr(p_tdata->pool, p_tdata->msg, CONNECTION_TYPE, pj_cstr(&value_cntrl,CONTROL_CONNECTION));
		if (status != PJ_SUCCESS) 
		{
			printf( "Error in inclusion of CONNECTION_TYPE attribute, pj_stun_msg_add_string_attr()");
                        return status;
		}

		/* Inclusion of Stream ID attribute in the STUN error response */
		status = pj_stun_msg_add_string_attr(p_tdata->pool, p_tdata->msg, PJ_STUN_ATTR_STREAM_ID, pj_cstr(&str_val,stream_id));
		if (status != PJ_SUCCESS)
		{
			printf( "Error in inclusion of PJ_STUN_ATTR_STREAM_ID attribute, pj_stun_msg_add_string_attr()");
			return status;
                }

		/* Inclusion of username attribute in the STUN success request */
		status = pj_stun_msg_add_string_attr(p_tdata->pool, p_tdata->msg, PJ_STUN_ATTR_USERNAME, pj_cstr(&mac_val,user_name));
		if (status != PJ_SUCCESS) 
		{
			printf( "Error in inclusion of PJ_STUN_ATTR_USERNAME attribute, pj_stun_msg_add_string_attr()");
			return status;
		}

		/* Sending STUN error response */
		status = pj_stun_session_send_msg(control_context.sess, NULL, PJ_FALSE, PJ_TRUE, src_addr, sad_len, p_tdata);
		if (status != PJ_SUCCESS) 
		{
			printf( "Error in sending STUN error response for command type attribute missing, pj_stun_session_send_msg()");
			return status;
		}																					        		
		return PJ_SUCCESS;
	}
	
	if (strcmp(cmd_type,CAM_STATUS)==0)
	{
		printf( "Checking for video streamer availability");
		stream_status = is_streamer_available ();
		if (stream_status != CAMERA_AVAILABLE)
		{
		 	printf( "Camera not available, sending error response to the STUN server");

			/* STUN error response creation */
			status = pj_stun_session_create_res(control_context.sess, rdata, CAMERA_NOT_AVAILABLE, NULL, &p_tdata);
			if (status != PJ_SUCCESS) 
			{
				printf( "Error in STUN error response creation,pj_stun_session_create_res()");
		        	return status;
			}

			/* Inclusion of connection type attribute in the STUN error response */
			status = pj_stun_msg_add_string_attr(p_tdata->pool, p_tdata->msg, CONNECTION_TYPE, pj_cstr(&value_cntrl,CONTROL_CONNECTION));
			if (status != PJ_SUCCESS) 
			{
				printf( "Error in inclusion of CONNECTION_TYPE attribute, pj_stun_msg_add_string_attr()");
		  		return status;
	        	}	

			/* Inclusion of Stream ID attribute in the STUN error response */
			status = pj_stun_msg_add_string_attr(p_tdata->pool, p_tdata->msg, PJ_STUN_ATTR_STREAM_ID, pj_cstr(&str_val,stream_id));
			if (status != PJ_SUCCESS)
			{
				printf( "Error in inclusion of PJ_STUN_ATTR_STREAM_ID attribute, pj_stun_msg_add_string_attr()");
				return status;
                	}

			/* Inclusion of username attribute in the STUN error response */
			status = pj_stun_msg_add_string_attr(p_tdata->pool, p_tdata->msg, PJ_STUN_ATTR_USERNAME, pj_cstr(&mac_val,user_name));
			if (status != PJ_SUCCESS) 
			{
				printf( "Error in inclusion of PJ_STUN_ATTR_USERNAME attribute, pj_stun_msg_add_string_attr()");
				return status;
			}

			/* Inclusion of command type attribute in the STUN error response */
			status = pj_stun_msg_add_string_attr(p_tdata->pool, p_tdata->msg, COMMAND_TYPE, pj_cstr(&cmd_val,cmd_type));
			if (status != PJ_SUCCESS) 
			{
				printf( "Error in inclusion of COMMAND_TYPE attribute, pj_stun_msg_add_string_attr()");
				return status;
			}

			/* Sending STUN error response */
			status = pj_stun_session_send_msg(control_context.sess, NULL, PJ_FALSE, PJ_TRUE, src_addr, sad_len, p_tdata);
			if (status != PJ_SUCCESS) 
			{
				printf( "Error in sending STUN error response for camera unavailability, pj_stun_session_send_msg()");
				return status;
			}																					        		
			return PJ_SUCCESS;
		}
		
		PJ_LOG(2,(THIS_FILE, "Camera available, sending success response to STUN server"));
			
		/* STUN success response creation */
		status = pj_stun_session_create_res(control_context.sess, rdata, 0, NULL, &p_tdata);
		if (status != PJ_SUCCESS) 
		{
			printf( "Error in STUN success response creation,pj_stun_session_create_res()");
		        return status;
		}
		
		/* Inclusion of connection type attribute in the STUN success response */
		status = pj_stun_msg_add_string_attr(p_tdata->pool, p_tdata->msg, CONNECTION_TYPE, pj_cstr(&value_cntrl,CONTROL_CONNECTION));
		if (status != PJ_SUCCESS) 
		{
			printf( "Error in inclusion of CONNECTION_TYPE attribute, pj_stun_msg_add_string_attr()");
                        return status;
        	}

		/* Inclusion of Stream ID attribute in the STUN success response */
		status = pj_stun_msg_add_string_attr(p_tdata->pool, p_tdata->msg, PJ_STUN_ATTR_STREAM_ID, pj_cstr(&str_val,stream_id));
		if (status != PJ_SUCCESS)
		{
			printf( "Error in inclusion of PJ_STUN_ATTR_STREAM_ID attribute, pj_stun_msg_add_string_attr()");
			return status;
                }

		/* Inclusion of username attribute in the STUN success response */
		status = pj_stun_msg_add_string_attr(p_tdata->pool, p_tdata->msg, PJ_STUN_ATTR_USERNAME, pj_cstr(&mac_val,user_name));
		if (status != PJ_SUCCESS) 
		{
			printf( "Error in inclusion of PJ_STUN_ATTR_USERNAME attribute, pj_stun_msg_add_string_attr()");
			return status;
		}

		/* Inclusion of command type attribute in the STUN success response */
		status = pj_stun_msg_add_string_attr(p_tdata->pool, p_tdata->msg, COMMAND_TYPE, pj_cstr(&cmd_val,cmd_type));
		if (status != PJ_SUCCESS) 
		{
			printf( "Error in inclusion of COMMAND_TYPE attribute, pj_stun_msg_add_string_attr()");
			return status;
		}

		/* Inclusion of camera available attribute in the STUN success response*/
		status = pj_stun_msg_add_string_attr(p_tdata->pool, p_tdata->msg, PJ_STUN_CAMERA_AVAILABLE, pj_cstr(&val,CAMERA_AVAILABLE_MSG));
	       	if (status != PJ_SUCCESS) 
		{
			printf( "Error in inclusion of PJ_STUN_CAMERA_AVAILABLE attribute");
	                return status;
                }
		
		/* Sending STUN success response */
		status = pj_stun_session_send_msg(control_context.sess, NULL, PJ_FALSE, PJ_TRUE, src_addr, sad_len, p_tdata);
		if (status != PJ_SUCCESS) 
		{
			printf( "Error in sending STUN success response for camera availability, pj_stun_session_send_msg()");
			return status;
		}																					        	}
	else if (strcmp(cmd_type,START_STREAMING)==0)
	{
		PJ_LOG(2,(THIS_FILE, "Received request for Start streaming from STUN server"));
		if (is_streamer_available() != CAMERA_AVAILABLE)
		{
			printf( "Camera not ready to take any more request");
			
			/* STUN error response creation */
			status = pj_stun_session_create_res(control_context.sess, rdata, CAMERA_NOT_AVAILABLE, NULL, &p_tdata);
			if (status != PJ_SUCCESS) 
			{
				printf( "Error in STUN error response creation,pj_stun_session_create_res()");
		        	return status;
			}

			/* Inclusion of connection type attribute in the STUN error response */
			status = pj_stun_msg_add_string_attr(p_tdata->pool, p_tdata->msg, CONNECTION_TYPE, pj_cstr(&value_cntrl,CONTROL_CONNECTION));
			if (status != PJ_SUCCESS) 
			{
				printf( "Error in inclusion of CONNECTION_TYPE attribute, pj_stun_msg_add_string_attr()");
		  		return status;
	        	}	

			/* Inclusion of Stream ID attribute in the STUN error response */
			status = pj_stun_msg_add_string_attr(p_tdata->pool, p_tdata->msg, PJ_STUN_ATTR_STREAM_ID, pj_cstr(&str_val,stream_id));
			if (status != PJ_SUCCESS)
			{
				printf( "Error in inclusion of PJ_STUN_ATTR_STREAM_ID attribute, pj_stun_msg_add_string_attr()");
				return status;
                	}

			/* Inclusion of username attribute in the STUN error response */
			status = pj_stun_msg_add_string_attr(p_tdata->pool, p_tdata->msg, PJ_STUN_ATTR_USERNAME, pj_cstr(&mac_val,user_name));
			if (status != PJ_SUCCESS) 
			{
				printf( "Error in inclusion of PJ_STUN_ATTR_USERNAME attribute, pj_stun_msg_add_string_attr()");
				return status;
			}

			/* Inclusion of command type attribute in the STUN error response */
			status = pj_stun_msg_add_string_attr(p_tdata->pool, p_tdata->msg, COMMAND_TYPE, pj_cstr(&cmd_val,cmd_type));
			if (status != PJ_SUCCESS) 
			{
				printf( "Error in inclusion of COMMAND_TYPE attribute, pj_stun_msg_add_string_attr()");
				return status;
			}

			/* Sending STUN error response */
			status = pj_stun_session_send_msg(control_context.sess, NULL, PJ_FALSE, PJ_TRUE, src_addr, sad_len, p_tdata);
			if (status != PJ_SUCCESS) 
			{
				printf( "Error in sending STUN error response for camera unavailability, pj_stun_session_send_msg()");
				return status;
			}																					        		
			return PJ_SUCCESS;
		}

		/* Data session context structure's memory allocation */
		data_sess_context = (data_sess *) malloc(sizeof(data_sess));
		if (data_sess_context == NULL)
		{
			printf("****************Error in memory allocation for the Data session context");
			exit(1);
		}
		
		/* Caching pool initialization for Data session */
		pj_caching_pool_init(&data_sess_context->cp_ds, &pj_pool_factory_default_policy, 0);
		
		/* Memory allocation for Data session */	
		data_sess_context->pool_ds = pj_pool_create(&data_sess_context->cp_ds.factory, "BMS client", MAX_MEM_POOL_SIZE, MAX_RUN_MEM_POOL_SIZE, NULL);
		if (!data_sess_context->pool_ds) 
		{
			printf( "*************Unable to create memory pool for Data session");
			exit(1);
		}
			
		/* Updating the Start stream parameters value in the Data session structure */
		strcpy(data_sess_context->stream_id_ds,stream_id);                                          // Stream ID updated
		strcpy(data_sess_context->random_num_ds,random_num);                                        // Random number updated
		data_sess_context->remote_addr_ds.ipv4.sin_family = remote_addr.ipv4.sin_family;            // Remote address updated
		data_sess_context->remote_addr_ds.ipv4.sin_addr.s_addr = remote_addr.ipv4.sin_addr.s_addr;
		data_sess_context->remote_addr_ds.ipv4.sin_port = remote_addr.ipv4.sin_port;
		
		/* Creation of the Data session thread */
		status = pj_thread_create(data_sess_context->pool_ds, "Data session thread", (pj_thread_proc*) &init_data_session, data_sess_context, PJ_THREAD_DEFAULT_STACK_SIZE, 0, &data_sess_context->thread_ds);
		if (status != PJ_SUCCESS)
		{
			printf( "***********************Error in creation of the Data session thread");
			exit(1);
		}
	}
	else if (strcmp(cmd_type,STOP_STREAMING)==0)
	{
		PJ_LOG(2,(THIS_FILE, "Received request for Stop streaming from STUN server"));
		stop_streaming(stream_id);
		PJ_LOG(2,(THIS_FILE, "Stop streaming executed successfully"));

		/* STUN success response creation */
		status = pj_stun_session_create_res(control_context.sess, rdata, 0, NULL, &p_tdata);
		if (status != PJ_SUCCESS) 
		{
			printf( "Error in STUN success response creation,pj_stun_session_create_res()");
		        return status;
		}
			
		/* Inclusion of connection type attribute in the STUN success request */
		status = pj_stun_msg_add_string_attr(p_tdata->pool, p_tdata->msg, CONNECTION_TYPE, pj_cstr(&value_cntrl,CONTROL_CONNECTION));
		if (status != PJ_SUCCESS) 
		{
			printf( "Error in inclusion of CONNECTION_TYPE attribute, pj_stun_msg_add_string_attr()");
			return status;
		}
				
		/* Inclusion of Stream ID attribute in the STUN success request */
		status = pj_stun_msg_add_string_attr(p_tdata->pool, p_tdata->msg, PJ_STUN_ATTR_STREAM_ID, pj_cstr(&str_val,stream_id));
		if (status != PJ_SUCCESS) 
		{
			printf( "Error in inclusion of PJ_STUN_ATTR_STREAM_ID attribute, pj_stun_msg_add_string_attr()");
			return status;
		}
			
		/* Inclusion of username attribute in the STUN success request */
		status = pj_stun_msg_add_string_attr(p_tdata->pool, p_tdata->msg, PJ_STUN_ATTR_USERNAME, pj_cstr(&mac_val,user_name));
		if (status != PJ_SUCCESS) 
		{
			printf( "Error in inclusion of PJ_STUN_ATTR_USERNAME attribute, pj_stun_msg_add_string_attr()");
			return status;
		}

		/* Inclusion of command type attribute in the STUN success request */
		status = pj_stun_msg_add_string_attr(p_tdata->pool, p_tdata->msg, COMMAND_TYPE, pj_cstr(&cmd_val,STOP_STREAMING));
		if (status != PJ_SUCCESS) 
		{
			printf( "Error in inclusion of COMMAND_TYPE attribute, pj_stun_msg_add_string_attr()");
			return status;
		}

		/* Sending STUN success response after Stop streaming */
		status = pj_stun_session_send_msg(control_context.sess, NULL, PJ_FALSE, PJ_TRUE, src_addr, sad_len, p_tdata);
		if (status != PJ_SUCCESS) 
		{
			printf( "Error in sending STUN success response for Stop streaming, pj_stun_session_send_msg()");
			return status;
		}

		printf( "Stop streaming success response sent to the STUN server");
	}
	else if (strcmp(cmd_type,COMMAND)==0)
	{
	   	cam_process_ret = camera_process_command(stream_id,command);
		printf( "Camera process command executed by the streamer function");

		if (cam_process_ret != NULL)
		{
			if (strlen (cam_process_ret) <= MAX_COMMAND_RES_LEN) 
			{
				/* STUN success response creation */
				status = pj_stun_session_create_res(control_context.sess, rdata, 0, NULL, &p_tdata);
				if (status != PJ_SUCCESS) 
				{
					printf( "Error in STUN success response creation,pj_stun_session_create_res()");
					free(cam_process_ret);
					cam_process_ret = NULL;
					return PJ_SUCCESS;
				}

				/* Inclusion of connection type attribute in the response */
				status = pj_stun_msg_add_string_attr(p_tdata->pool, p_tdata->msg, CONNECTION_TYPE, pj_cstr(&value_cntrl,CONTROL_CONNECTION));
				if (status != PJ_SUCCESS)
				{
					printf( "Error in inclusion of CONNECTION_TYPE attribute, pj_stun_msg_add_string_attr()");
					free(cam_process_ret);
					cam_process_ret = NULL;
					return PJ_SUCCESS;
				}

				/* Inclusion of Stream ID attribute in the response */
				status = pj_stun_msg_add_string_attr(p_tdata->pool, p_tdata->msg, PJ_STUN_ATTR_STREAM_ID, pj_cstr(&str_val,stream_id));
				if (status != PJ_SUCCESS)
				{
					printf( "Error in inclusion of PJ_STUN_ATTR_STREAM_ID attribute, pj_stun_msg_add_string_attr()");
					free(cam_process_ret);
					cam_process_ret = NULL;
					return PJ_SUCCESS;
				}

				/* Inclusion of username attribute in the response */
				status = pj_stun_msg_add_string_attr(p_tdata->pool, p_tdata->msg, PJ_STUN_ATTR_USERNAME, pj_cstr(&mac_val,user_name));
				if (status != PJ_SUCCESS)
				{
					printf( "Error in inclusion of PJ_STUN_ATTR_USERNAME attribute, pj_stun_msg_add_string_attr()");
					free(cam_process_ret);
					cam_process_ret = NULL;
					return PJ_SUCCESS;
				}

				/* Inclusion of command type attribute in the response */
				status = pj_stun_msg_add_string_attr(p_tdata->pool, p_tdata->msg, COMMAND_TYPE, pj_cstr(&cmd_val,cmd_type));
				if (status != PJ_SUCCESS) 
				{
					printf( "Error in inclusion of COMMAND_TYPE attribute, pj_stun_msg_add_string_attr()");
					free(cam_process_ret);
					cam_process_ret = NULL;
					return PJ_SUCCESS;
				}
			
				/* Inclusion of camera process command function response */
				status = pj_stun_msg_add_string_attr(p_tdata->pool, p_tdata->msg, PJ_STUN_COMMAND_RESPONSE, pj_cstr(&comm_res,cam_process_ret));
				free(cam_process_ret);
				cam_process_ret = NULL;
				if (status != PJ_SUCCESS) 
				{
					printf( "Error in inclusion of PJ_STUN_COMMAND_RESPONSE attribute, pj_stun_msg_add_string_attr()");
					return PJ_SUCCESS;
				}
			
				/* Sending STUN success response */
				status = pj_stun_session_send_msg(control_context.sess, NULL, PJ_FALSE, PJ_TRUE, src_addr, sad_len, p_tdata);
				if (status != PJ_SUCCESS) 
				{
					printf( "Error in sending STUN success response for camera process function response, pj_stun_session_send_msg()");
					return PJ_SUCCESS;
				}

			}
			else
			{
				/* Camera process ret value stored in cam_process_truncated */
				memcpy(cam_process_truncated,cam_process_ret,MAX_COMMAND_RES_LEN);
				free(cam_process_ret);
                                cam_process_ret = NULL;

				/* STUN error response creation */
				status = pj_stun_session_create_res(control_context.sess, rdata, 0, NULL, &p_tdata);
				if (status != PJ_SUCCESS) 
				{
					printf( "Error in STUN error response creation,pj_stun_session_create_res()");
					return PJ_SUCCESS;
				}
				
				/* Inclusion of connection type attribute in the response */
				status = pj_stun_msg_add_string_attr(p_tdata->pool, p_tdata->msg, CONNECTION_TYPE, pj_cstr(&value_cntrl,CONTROL_CONNECTION));
				if (status != PJ_SUCCESS)
				{
					printf( "Error in inclusion of CONNECTION_TYPE attribute, pj_stun_msg_add_string_attr()");
					return PJ_SUCCESS;
				}
				
				/* Inclusion of Stream ID attribute in the response */
				status = pj_stun_msg_add_string_attr(p_tdata->pool, p_tdata->msg, PJ_STUN_ATTR_STREAM_ID, pj_cstr(&str_val,stream_id));
				if (status != PJ_SUCCESS)
				{
					printf( "Error in inclusion of PJ_STUN_ATTR_STREAM_ID attribute, pj_stun_msg_add_string_attr()");
					return PJ_SUCCESS;
				}

				/* Inclusion of username attribute in the response */
				status = pj_stun_msg_add_string_attr(p_tdata->pool, p_tdata->msg, PJ_STUN_ATTR_USERNAME, pj_cstr(&mac_val,user_name));
				if (status != PJ_SUCCESS)
				{
					printf( "Error in inclusion of PJ_STUN_ATTR_USERNAME attribute, pj_stun_msg_add_string_attr()");
					return PJ_SUCCESS;
				}

				/* Inclusion of command type attribute in the response */
				status = pj_stun_msg_add_string_attr(p_tdata->pool, p_tdata->msg, COMMAND_TYPE, pj_cstr(&cmd_val,cmd_type));
				if (status != PJ_SUCCESS) 
				{
					printf( "Error in inclusion of COMMAND_TYPE attribute, pj_stun_msg_add_string_attr()");
					return PJ_SUCCESS;
				}
				
				/* Inclusion of camera process command function response after truncation */
				status = pj_stun_msg_add_string_attr(p_tdata->pool, p_tdata->msg, PJ_STUN_COMMAND_RESPONSE, pj_cstr(&comm_res,cam_process_truncated));
				if (status != PJ_SUCCESS) 
				{
					printf( "Error in inclusion of PJ_STUN_COMMAND_RESPONSE attribute, pj_stun_msg_add_string_attr()");
					return PJ_SUCCESS;
				}
		
				/* Sending STUN error response */
				status = pj_stun_session_send_msg(control_context.sess, NULL, PJ_FALSE, PJ_TRUE, src_addr, sad_len, p_tdata);
				if (status != PJ_SUCCESS) 
				{
					printf( "Error in sending STUN error response for camera process function response, pj_stun_session_send_msg()");
					return PJ_SUCCESS;
				}
			}
		}
		else
		{
			/* STUN success response creation */
			status = pj_stun_session_create_res(control_context.sess, rdata, 0, NULL, &p_tdata);
			if (status != PJ_SUCCESS) 
			{
				printf( "Error in STUN success response creation,pj_stun_session_create_res()");
				return PJ_SUCCESS;
			}

			/* Inclusion of connection type attribute in the response */
			status = pj_stun_msg_add_string_attr(p_tdata->pool, p_tdata->msg, CONNECTION_TYPE, pj_cstr(&value_cntrl,CONTROL_CONNECTION));
			if (status != PJ_SUCCESS)
			{
				printf( "Error in inclusion of CONNECTION_TYPE attribute, pj_stun_msg_add_string_attr()");
				return PJ_SUCCESS;
			}

			/* Inclusion of Stream ID attribute in the response */
			status = pj_stun_msg_add_string_attr(p_tdata->pool, p_tdata->msg, PJ_STUN_ATTR_STREAM_ID, pj_cstr(&str_val,stream_id));
			if (status != PJ_SUCCESS)
			{
				printf( "Error in inclusion of PJ_STUN_ATTR_STREAM_ID attribute, pj_stun_msg_add_string_attr()");
				return PJ_SUCCESS;
			}

			/* Inclusion of username attribute in the response */
			status = pj_stun_msg_add_string_attr(p_tdata->pool, p_tdata->msg, PJ_STUN_ATTR_USERNAME, pj_cstr(&mac_val,user_name));
			if (status != PJ_SUCCESS)
			{
				printf( "Error in inclusion of PJ_STUN_ATTR_USERNAME attribute, pj_stun_msg_add_string_attr()");
				return PJ_SUCCESS;
			}

			/* Inclusion of command type attribute in the response */
			status = pj_stun_msg_add_string_attr(p_tdata->pool, p_tdata->msg, COMMAND_TYPE, pj_cstr(&cmd_val,cmd_type));
			if (status != PJ_SUCCESS) 
			{
				printf( "Error in inclusion of COMMAND_TYPE attribute, pj_stun_msg_add_string_attr()");
				return PJ_SUCCESS;
			}
	
			/* Sending STUN success response */
			status = pj_stun_session_send_msg(control_context.sess, NULL, PJ_FALSE, PJ_TRUE, src_addr, sad_len, p_tdata);
			if (status != PJ_SUCCESS) 
			{
				printf( "Error in sending STUN success response for camera process function response, pj_stun_session_send_msg()");
				return PJ_SUCCESS;
			}
		}
	}
	else
	{
		printf( "Command type value invalid, sending error response to the STUN server");

		/* STUN error response creation */
		status = pj_stun_session_create_res(control_context.sess, rdata, CMD_TYPE_INVALID, NULL, &p_tdata);
		if (status != PJ_SUCCESS) 
		{
			printf( "Error in STUN error response creation,pj_stun_session_create_res()");
		        return status;
		}

		/* Inclusion of connection type attribute in the STUN error response */
                status = pj_stun_msg_add_string_attr(p_tdata->pool, p_tdata->msg, CONNECTION_TYPE, pj_cstr(&value_cntrl,CONTROL_CONNECTION));
		if (status != PJ_SUCCESS) 
		{
			printf( "Error in inclusion of CONNECTION_TYPE attribute, pj_stun_msg_add_string_attr()");
			return status;
		}

		/* Inclusion of Stream ID attribute in the STUN error response */
		status = pj_stun_msg_add_string_attr(p_tdata->pool, p_tdata->msg, PJ_STUN_ATTR_STREAM_ID, pj_cstr(&str_val,stream_id));
		if (status != PJ_SUCCESS)
		{
			printf( "Error in inclusion of PJ_STUN_ATTR_STREAM_ID attribute, pj_stun_msg_add_string_attr()");
			return status;
                }

		/* Inclusion of username attribute in the STUN error response */
		status = pj_stun_msg_add_string_attr(p_tdata->pool, p_tdata->msg, PJ_STUN_ATTR_USERNAME, pj_cstr(&mac_val,user_name));
		if (status != PJ_SUCCESS)
		{
			printf( "Error in inclusion of PJ_STUN_ATTR_USERNAME attribute, pj_stun_msg_add_string_attr()");
			return status;
                }

		/* Sending STUN error response */
		status = pj_stun_session_send_msg(control_context.sess, NULL, PJ_FALSE, PJ_TRUE, src_addr, sad_len, p_tdata);
		if (status != PJ_SUCCESS) 
		{
			printf( "Error in sending STUN error response for command type invalid, pj_stun_session_send_msg()");
			return status;
		}
	}
	return PJ_SUCCESS;	
}



pj_status_t init_data_session(data_sess *data_session_context)
{	
	int req_type = PJ_STUN_BINDING_REQUEST;                                                                          
    	pj_stun_tx_data *p_tdata; 
        pj_str_t value,str_id,data_val,cmd_val;; 	
	pj_ssize_t len;                                                                                                 
	int src_len,thrd_reg,msg_valid=0;                                                                                                    
	pj_sockaddr src_addr_of_pkt;                                                                                    
	pj_uint8_t  packet[MAX_PACKET_SIZE];                                                                            
	pj_size_t *parsed_len;                                                                                          
	pj_size_t parsed_data = 0;                                                                                         
	pj_status_t status;
	struct sockaddr_in sin;              										
	socklen_t addrlen;
	pj_stun_session *sess;
	parsed_len = &parsed_data;
	pj_thread_desc desc_data_sess;
	char thrd_name_ds[THRD_NAM_LEN_DS] = THREAD_NAME_DS;
	printf("=======STUN DATA THREAD=%d====\n",getpid());
	/* Initialize the Data session quit value at the beginning of the Data session */
	data_session_context->quit_ds = 0;

	/* Check if the Data session thread is already registered to PJLIB */
	thrd_reg = pj_thread_is_registered();
	if (thrd_reg==0)
	{
		/* Registering the worker thread for data connection */
        	status = pj_thread_register(thrd_name_ds, desc_data_sess, &data_session_context->thread_ds);
        	if (status != PJ_SUCCESS) 
		{
	        	printf( "Error in registering the Data session thread to the PJNATH framework, pj_thread_register()");
			delete_ds_thread(data_session_context->thread_ds,data_session_context->pool_ds,data_session_context);
			return 0;
		}
	}

	/* Parsed length content validation */
	if (parsed_len == NULL) 
	{
		printf("Parsed length pointer in function init_data_session() invalid");
		delete_ds_thread(data_session_context->thread_ds,data_session_context->pool_ds,data_session_context);
		return 0;
	}

	/* Callback initializations */
	pj_bzero(&data_session_context->sess_cb_ds, sizeof(data_session_context->sess_cb_ds));
	data_session_context->sess_cb_ds.on_send_msg = &send_msg_data;
	data_session_context->sess_cb_ds.on_request_complete = &data_handle_stun_response;

	/* Retreiving the local address of data socket */
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(INADDR_ANY);
	sin.sin_port = htons(0);
	addrlen = sizeof(sin);

	/* STUN Client data socket creation */
	status = pj_sock_socket(pj_AF_INET(), pj_SOCK_DGRAM(), 0, &data_session_context->sock_ds);
	if (status != PJ_SUCCESS) 
	{
	  	printf( "Error in creation of Data socket in init_data_session, pj_sock_socket()");
		delete_ds_thread(data_session_context->thread_ds,data_session_context->pool_ds,data_session_context);
		return 0;
        }
	
	/* Binding on STUN Client data socket */
	bind(data_session_context->sock_ds,(struct sockaddr *)&sin,sizeof(sin));
	
	/* Retreiving local port of STUN Client data socket */
	addrlen = sizeof(sin);
	if ((getsockname(data_session_context->sock_ds, (struct sockaddr *)&sin, &addrlen)!=0)) 
	{
		printf( "Error in getsockname()");	
		delete_ds_thread(data_session_context->thread_ds,data_session_context->pool_ds,data_session_context);
		return 0;
	}

	/* Local address updated in the Data session context */
	data_session_context->local_addr_ds.ipv4.sin_family = PJ_AF_INET;
	data_session_context->local_addr_ds.ipv4.sin_addr.s_addr = sin.sin_addr.s_addr;
	data_session_context->local_addr_ds.ipv4.sin_port = sin.sin_port;

	/* State updation in data_context for Start streaming */
	data_session_context->state_ds = 1; 											

	/* Creation of STUN Client data session */
	status = pj_stun_session_create(&control_context.stun_cfg, "BMS client", &data_session_context->sess_cb_ds, PJ_FALSE, &sess);
	if (status != PJ_SUCCESS) 
	{
		printf( "Error in Data session creation of the STUN client, pj_stun_session_create()");
		delete_ds_thread(data_session_context->thread_ds,data_session_context->pool_ds,data_session_context);
		return 0;	
	}
	
	/* Software attribute set to NULL*/
	status = pj_stun_session_set_software_name(sess, NULL);
	if (status != PJ_SUCCESS) 
	{
		PJ_LOG(2, (THIS_FILE, "Error in pj_stun_session_set_software_name()"));
		pj_stun_session_destroy(sess);
		delete_ds_thread(data_session_context->thread_ds,data_session_context->pool_ds,data_session_context);
		return 0;	
	}

	/* STUN Binding Request creation */
	status = pj_stun_session_create_req(sess, req_type, PJ_STUN_MAGIC ,NULL, &p_tdata);
	if (status != PJ_SUCCESS) 
	{
		printf( "Error in STUN request creation, pj_stun_session_create_req()");
		pj_stun_session_destroy(sess);
		delete_ds_thread(data_session_context->thread_ds,data_session_context->pool_ds,data_session_context);
		return 0;	
	}

	/* Inclusion of connection type attribute in the STUN Binding Request */
	status = pj_stun_msg_add_string_attr(p_tdata->pool, p_tdata->msg, CONNECTION_TYPE, pj_cstr(&data_val,DATA_CONNECTION));
	if (status != PJ_SUCCESS) 
	{
		printf( "Error in inclusion of CONNECTION_TYPE attribute, pj_stun_msg_add_string_attr()");
		pj_stun_session_destroy(sess);
		delete_ds_thread(data_session_context->thread_ds,data_session_context->pool_ds,data_session_context);
		return 0;	
	}

	/* Inclusion of stream ID attribute in the STUN Binding Request */
	status = pj_stun_msg_add_string_attr(p_tdata->pool, p_tdata->msg, PJ_STUN_ATTR_STREAM_ID, pj_cstr(&str_id,data_session_context->stream_id_ds));
	if (status != PJ_SUCCESS) 
	{
		printf( "Error in inclusion of PJ_STUN_ATTR_STREAM_ID attribute, pj_stun_msg_add_string_attr()");
		pj_stun_session_destroy(sess);
		delete_ds_thread(data_session_context->thread_ds,data_session_context->pool_ds,data_session_context);
		return 0;	
        }

	/* Inclusion of username attribute in the STUN Binding Request */
	status = pj_stun_msg_add_string_attr(p_tdata->pool, p_tdata->msg, PJ_STUN_ATTR_USERNAME, pj_cstr(&value,mac_addr));
	if (status != PJ_SUCCESS) 
	{
		printf( "Error in inclusion of PJ_STUN_ATTR_USERNAME attribute, pj_stun_msg_add_string_attr()");
		pj_stun_session_destroy(sess);
		delete_ds_thread(data_session_context->thread_ds,data_session_context->pool_ds,data_session_context);
		return 0;	
	}
	
	/* Inclusion of command type attribute in the STUN Binding Request */
	status = pj_stun_msg_add_string_attr(p_tdata->pool, p_tdata->msg, COMMAND_TYPE, pj_cstr(&cmd_val,START_STREAMING));
	if (status != PJ_SUCCESS) 
	{
		printf( "Error in inclusion of COMMAND_TYPE attribute, pj_stun_msg_add_string_attr()");
		pj_stun_session_destroy(sess);
		delete_ds_thread(data_session_context->thread_ds,data_session_context->pool_ds,data_session_context);
		return 0;	
        }

	/* Associating client data session context with the data session */
	status = pj_stun_session_set_user_data(sess, data_session_context);
	if (status != PJ_SUCCESS) 
	{
		printf( "Error in associating data_session_context with the Data session");
		pj_stun_session_destroy(sess);
		delete_ds_thread(data_session_context->thread_ds,data_session_context->pool_ds,data_session_context);
		return 0;	
        }
	
	/* Sending STUN Binding Request */
	status = pj_stun_session_send_msg(sess, NULL, PJ_FALSE, PJ_TRUE, (pj_sockaddr_t *)&control_context.dst_addr, sizeof(control_context.dst_addr), p_tdata); 
	if (status != PJ_SUCCESS) 
	{
		printf( "Error in sending STUN Binding request on the Data connection, pj_stun_session_send_msg()");
		pj_stun_session_destroy(sess);
		delete_ds_thread(data_session_context->thread_ds,data_session_context->pool_ds,data_session_context);
		return 0;	
	}

        PJ_LOG(2, (THIS_FILE, "STUN Binding request message with CONNECTION_TYPE,USERNAME,STREAM_ID and COMMAND_TYPE attributes sent to the STUN server"));

	while(!msg_valid)
	{
	/* Receiving on the data socket */
	src_len = sizeof(src_addr_of_pkt);
	len = sizeof(packet);
	status = pj_sock_recvfrom(data_session_context->sock_ds, packet, &len, 0, &src_addr_of_pkt, &src_len);
	if (status != PJ_SUCCESS) 
	{
		printf( "Error in receiving the STUN response from the STUN server on Data connection");
		pj_stun_session_destroy(sess);
		delete_ds_thread(data_session_context->thread_ds,data_session_context->pool_ds,data_session_context);
		return 0;	
        }

	/* Check if no response on Data connection */
	if (data_session_context->quit_ds)
	{
		printf( "STUN Binding response not received on the Data socket,shutting down the data session");
		pj_stun_session_destroy(sess);
		delete_ds_thread(data_session_context->thread_ds,data_session_context->pool_ds,data_session_context);
		return 0;
	}
	
		PJ_LOG(2,(THIS_FILE, "STUN client received a Data paket of length %d on the Data connection from address %s and port %d",len,pj_inet_ntoa(src_addr_of_pkt.ipv4.sin_addr),pj_ntohs(src_addr_of_pkt.ipv4.sin_port)));

	
	/* STUN message check */
        status = pj_stun_msg_check(packet, len, PJ_STUN_CHECK_PACKET);
        if (status != PJ_SUCCESS) 
	{
			PJ_LOG(2, (THIS_FILE, "Not a valid STUN packet"));
		}
		else
		{
			msg_valid = 1;
		}
	}
	
	/* Notification to callback functions on receiving STUN packet */
	status = pj_stun_session_on_rx_pkt(sess, packet, sizeof(packet), PJ_STUN_IS_DATAGRAM, NULL, parsed_len, &control_context.dst_addr, sizeof(control_context.dst_addr));
	if (status != PJ_SUCCESS) 
	{
		printf( "Error in notification for callbacks in the Data session, pj_stun_session_on_rx_pkt()");
		pj_stun_session_destroy(sess);
		delete_ds_thread(data_session_context->thread_ds,data_session_context->pool_ds,data_session_context);
		return 0;	
	}
	
	/* Destroying the Data session */
	status = pj_stun_session_destroy(sess);
	if (status != PJ_SUCCESS)
	{
		printf( "Error in destroying the Data session");	
	}
	
	/* Add the Data session thread ,pool,caching pool pointer to the deletion list */
	delete_ds_thread(data_session_context->thread_ds,data_session_context->pool_ds,data_session_context);
	return 0;
}


pj_status_t send_msg_data(pj_stun_session *sess,void *token,const void *pkt,pj_size_t pkt_size,const pj_sockaddr_t *dst_addr,unsigned addr_len)
{
	data_sess *data_sess_context;
	data_sess_context = pj_stun_session_get_user_data(sess);
	pj_ssize_t len = pkt_size;
	PJ_UNUSED_ARG(token);
	return pj_sock_sendto(data_sess_context->sock_ds, pkt, &len, 0, dst_addr, addr_len);
}



void data_handle_stun_response(pj_stun_session *sess,pj_status_t status,void *token,pj_stun_tx_data *rdata,const pj_stun_msg *response,const pj_sockaddr_t *src_addr,unsigned sad_len)
{
	int no_res = 0;
	data_sess *data_session_context;	
	char *data = DATA_CONNECTION; 
	pj_str_t data_val,cmd_val;
	char *cmd = START_STREAMING;
	cmd_val = pj_str(cmd);
	data_val = pj_str(data);
	char buff[MAX_REMOTE_MSG_LEN] = REMOTE_MSG;
	ssize_t send_ret;
	PJ_UNUSED_ARG(token);

	/* Get the data context associated with the Data session */
        data_session_context = pj_stun_session_get_user_data(sess);
       
	/* On retransmission failure,Data session and the Data session thread is destroyed */	
	if (response == NULL || status == PJNATH_ESTUNTIMEDOUT)
	{
		PJ_LOG(2,(THIS_FILE, "Timeout on Data connection waiting for STUN Binding response"));
		
		/* Quit the Data session */
		data_session_context->quit_ds = 1;
		
		/* Shutting down the Data socket */
		pj_sock_shutdown(data_session_context->sock_ds,PJ_SHUT_RDWR);
		
		no_res = 1;
	}
	
	if (no_res != 1)
	{
		/* Check for error response */
		if (!strcmp(pj_stun_get_class_name(response->hdr.type),"error response"))
		{
			PJ_LOG(2,(THIS_FILE, "Binding response error on Data connection"));
		}
		else if (!strcmp(pj_stun_get_class_name(response->hdr.type),"success response"))		
		{
			/* Sending Dummy UDP packet to the Remote address */
			send_ret = sendto(data_session_context->sock_ds,buff, MAX_REMOTE_MSG_LEN, 0, (struct sockaddr *)&data_session_context->remote_addr_ds, sizeof(data_session_context->remote_addr_ds));
			if (send_ret == -1) 
			{
				printf( "Error in sending %d bytes to the Remote address",send_ret);
			}
			else
			{
				pj_sock_close(data_session_context->sock_ds);
				data_session_context->sock_ds = 0;
				printf( "Dummy UDP packet of %d bytes successfully sent to the Remote address = %s, Remote port = %d",send_ret,pj_inet_ntoa(data_session_context->remote_addr_ds.ipv4.sin_addr),pj_ntohs(data_session_context->remote_addr_ds.ipv4.sin_port));	

				/* Pass the required arguements to the streamer to initiate Streaming */	
				start_streaming(&data_session_context->remote_addr_ds, &data_session_context->local_addr_ds, data_session_context->stream_id_ds, data_session_context->random_num_ds); 
				printf( "Start streaming executed successfully");			
			}
		}
	
		/* Close the data socket */
		if (data_session_context->sock_ds != 0)
		{	
			pj_sock_close(data_session_context->sock_ds);
		}
	}	
}



int get_mac_address(char *mac_addr)
{
	
	int mac_invalid = 0;
	unsigned char mac[6];
	int inet_type = INTERFACE_TYPE,i=0;
	struct ifreq ifr;
	int sock,l=0;
	char *ifname=NULL;
	extern char debug_macaddr[];
	
	if (strlen(debug_macaddr) != 0)
	{
	    strcpy(mac_addr,debug_macaddr);
	    return 0;
	}

	if (!inet_type) 
	{
		ifname = LOCAL_INTERFACE ; 				 
	}
	else
	{
		ifname = WIFI_INTERFACE; 			 
	}
	sock=socket(AF_INET,SOCK_DGRAM,0);
	if (sock==-1) 
	{
		printf( "Error in socket creation in get_mac_address()");
		mac_invalid = MAC_GEN_FAILURE;
	}
	if (mac_invalid != MAC_GEN_FAILURE)
	{
		strcpy(ifr.ifr_name,ifname );
		ifr.ifr_addr.sa_family = AF_INET;
		if (ioctl(sock,SIOCGIFHWADDR,&ifr) < 0) 
		{
			printf( "Error in ioctl() in get_mac_address()");
			mac_invalid = MAC_GEN_FAILURE;
		}
		if (mac_invalid != MAC_GEN_FAILURE)
		{
			memcpy(mac, ifr.ifr_hwaddr.sa_data, 6);
			close(sock);
			sprintf(mac_addr, "%02x%02x%02x%02x%02x%02x", mac[0] & 0xff, mac[1]& 0xff, mac[2]& 0xff, mac[3]& 0xff, mac[4]& 0xff, mac[5]& 0xff);
			for(i=0;i<12;i++)
			{
				l=mac_addr[i];
				mac_addr[i]=toupper(l);
			}
		}
	}
	return mac_invalid;
}	



void ka_timer_callback(pj_timer_heap_t  *timer_heap, pj_timer_entry  *entry)
{
	pj_status_t status;
	const pj_time_val delay = { KEEP_ALIVE_TIME_SEC, KEEP_ALIVE_TIME_MSEC };
	pj_stun_tx_data *ka_msg;
	pj_str_t value_cntrl;
	pj_str_t value;
	
	/* Keep-alive request creation */
	status = pj_stun_session_create_req(control_context.sess, PJ_STUN_BINDING_INDICATION, PJ_STUN_MAGIC , NULL, &ka_msg);
	if (status != PJ_SUCCESS) 
	{
		printf( "****************Error in Keep-alive message creation on the Control connection, pj_stun_session_create_req()");
		exit(1);	
	}

	/* Inclusion of connection type attribute in Keep-alive request */
	status = pj_stun_msg_add_string_attr(ka_msg->pool, ka_msg->msg, CONNECTION_TYPE, pj_cstr(&value_cntrl,CONTROL_CONNECTION));
	if (status != PJ_SUCCESS) 
	{
		printf( "****************Error in inclusion of CONNECTION_TYPE attribute in the keep-alive message, pj_stun_msg_add_string_attr()");
		exit(1);	
	}

	/* Inclusion of username attribute in keep-alive request */
	status = pj_stun_msg_add_string_attr(ka_msg->pool, ka_msg->msg, PJ_STUN_ATTR_USERNAME, pj_cstr(&value,mac_addr));
	if (status != PJ_SUCCESS) 
	{
		printf( "******************Error in inclusion of PJ_STUN_ATTR_USERNAME attribute in the keep-alive message, pj_stun_msg_add_string_attr()");
		exit(1);	
	}

	/* Rescheduling the keep-alive timer */
	status = pj_timer_heap_schedule(timer_heap, entry, &delay);
	if (status != PJ_SUCCESS) 
	{
		printf( "********************Error in rescheduling the keep-alive timer in keep-alive timer callback");
		exit(1);	
	}

	/* Sending the keep-alive message */
	status = pj_stun_session_send_msg(control_context.sess, NULL, PJ_FALSE, PJ_FALSE,(pj_sockaddr_t *)&control_context.dst_addr, sizeof(control_context.dst_addr), ka_msg); 
	if (status != PJ_SUCCESS) 
	{
		printf( "*********************Error in sending the keep-alive message, pj_stun_session_send_msg()");
		exit(1);	
	}

	PJ_LOG(2, (THIS_FILE, "Keep-alive message sent to the STUN server"));
}




static int worker_thread_control(void *unused)
{
	int thrd_reg,i;
	char thrd_name_cc[THRD_NAM_LEN_CS_WT] = THREAD_NAME_CS_WT;
	pj_thread_desc desc;
	pj_status_t status;
	PJ_UNUSED_ARG(unused);

	/* Check if thread is already registered to pjlib */
	thrd_reg = pj_thread_is_registered();
	if (thrd_reg==0)
	{
		/* Register the thread to the PJNATH framework */
		status = pj_thread_register(thrd_name_cc, desc, &control_context.thread);
		if (status != PJ_SUCCESS) 
		{

		}
	}

	while(1)
	{ 
		const pj_time_val delay = {0, TIME_DELAY_IO_POLL};
		
		/* Polling the I/O queue */
		pj_ioqueue_poll(control_context.stun_cfg.ioqueue, &delay);
		
		/* Polling the timer heap */
		pj_timer_heap_poll(control_context.stun_cfg.timer_heap, NULL);

		/* Destroying the Data session threads */
		for (i=0;i<MAX_DS_MEM_PARAM_DEL_COUNT;i++)
		{
			if (ds_del_param_list[i].thread_del_ptr != NULL)
			{
				/* Join with the Data session thread */
				status = pj_thread_join(ds_del_param_list[i].thread_del_ptr);
				if (status != PJ_SUCCESS)
				{
					printf( "Unable to join the thread");
				}	
		
				/* Destroy the Data session thread */
				status = pj_thread_destroy(ds_del_param_list[i].thread_del_ptr);
				if (status != PJ_SUCCESS)
				{
					printf( "Error in destroying the Data session thread");
				}
				ds_del_param_list[i].thread_del_ptr = NULL;

				/* Release the Data session pool */
				pj_pool_release(ds_del_param_list[i].del_pool_ptr);
				ds_del_param_list[i].thread_del_ptr = NULL;

				/* Destroy the Data session caching pool */
				pj_caching_pool_destroy(&ds_del_param_list[i].data_context->cp_ds);

				/* Free the Data session context */
				free(ds_del_param_list[i].data_context);
				ds_del_param_list[i].data_context = NULL;
			}
		}
	}
	pthread_exit(NULL);
	return 0;
}




void delete_ds_thread(pj_thread_t *thrd_pointer,pj_pool_t *pool_del_ds,data_sess *data_sess_context)
{
	int i;

	/* Add the thread pointer to the deletion list */
	for (i=0;i<MAX_DS_MEM_PARAM_DEL_COUNT;i++)
	{
	    if (ds_del_param_list[i].thread_del_ptr == NULL)
	    {
            	ds_del_param_list[i].thread_del_ptr = thrd_pointer;
		ds_del_param_list[i].del_pool_ptr = pool_del_ds;
		ds_del_param_list[i].data_context = data_sess_context; 
		break;
	    }
	}

	/* Check if maximum thread count is reached */
	if (i == MAX_DS_MEM_PARAM_DEL_COUNT)
	{

	}
	return;
}




int is_streamer_available(void)
{


	if( isRelayEnable)
	{
	  return !(relay_is_running());
	}
	else  // CASE of NORMAL STUN BUT STILL GO TO RELAY
	{
	  if(camera_is_available() && (!(relay_is_running())) ) 
	    {

	    return 1;
	    }
	  else
	    {

	    return 0;
	    }
	}


	return 0;
}




int start_streaming(pj_sockaddr *remote_add, pj_sockaddr *local_add, char *stream_id, char *rand_num)
{
	int ret;
	if (!isRelayEnable)
	{
	    ret = udt_start_session(pj_inet_ntoa(remote_add->ipv4.sin_addr), pj_ntohs(remote_add->ipv4.sin_port),pj_ntohs(local_add->ipv4.sin_port), stream_id,rand_num);

	}
	else
	{

	}
	return PJ_SUCCESS;
}



void stop_streaming(char *stream_id)
{
	int ret;

	if (!isRelayEnable)
	{
	    ret = udt_stop_session(stream_id);

	}
	else
	{
	    ret = relay_stop_session(stream_id);
	}
}




char *camera_process_command(char *stream_id, char *cmd)
{
	/* TESTING */
	char *st;
	st = malloc(512); // hope 1024 is enough  - the other function takes care of freeing

	if (strcmp(cmd,"streamer_status")==0)
	{
		if (is_streamer_available()==1)
		{
			sprintf(st,"available");
		}
		else
		{
			sprintf(st,"busy");
		}
		printf("Streamer_status: %s\n",st);
		return st;
	}
	else if (strcmp(cmd,"one")==0)
	{
		return NULL;
	}
	else if (strcmp(cmd,"two")==0)
	{
		sprintf(st,"k");
		return 	st;
	}
	else if (strcmp(cmd,"three")==0)
	{
		sprintf(st,"IP camera validation");
		return st;
	}
	else if (strcmp(cmd,"four")==0)
	{
		sprintf(st,"IP camera command processed successfully");
		return st;
	}
	else if (strcmp(cmd,"action=close_session") == 0)
	{
	    int ret;

	    ret = udt_stop_session(stream_id);
	    if(ret == 0)
		sprintf(st,"OK");
	    else
		sprintf(st,"NA");

	    return st;
	}

	else if (strcmp(cmd,"action=close_relay_session") == 0)
	{
	    int ret;

	    ret = relay_stop_session(stream_id);
	    if(ret == 0)
		sprintf(st,"OK");
	    else
		sprintf(st,"NA");

	    return st;
	}
	else if (strstr(cmd,"action=start_relay_session&setup=") != NULL)
	{
	    char *ptr;
	    int ret;
	    if (!is_streamer_available())
	    {

		sprintf(st,"NA_BUSY");
		return st;
	    }
	    ptr = cmd + strlen("action=start_relay_session&setup=");

	    ret = relay_start_session(ptr);

	    if (ret != 0)
	    {
		switch (ret)
		{
		    case -1:
			sprintf(st,"NA_BUSY");
			break;
		    case -2:
			sprintf(st,"NA_WRONGFORMAT");
			break;
		    case -3:
			sprintf(st,"NA_WRONGCHANNELID");
			break;
		    case -4:
			sprintf(st,"NA_CANTGENSESSIONKEY");
			break;
		    case -5:
			sprintf(st,"NA_CANTCREATSOCKET");
			break;
		    case -6:
			sprintf(st,"NA_CANTCONNECTSERVER");
			break;
			
		}
	    }
	    else
	    {
		sprintf(st,"OK");
	    }
	    return st;
	}
	
	else
	{
		if (camera_process_send_command(cmd, st))
		{

		    sprintf(st,"NA");
		}
		return 	st;
	}
}
	

