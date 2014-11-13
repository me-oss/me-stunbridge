#ifndef _UDTCWRAPPER_H_
#define _UDTCWRAPPER_H_

#define cAF_INET 0
#define cAF_INET6 1
#define cSOCK_STREAM 0
#define cSOCK_DGRAM 1

#ifdef __cplusplus
extern "C"
{
#endif

typedef enum
{
CUDT_MSS=0,
CUDT_SNDSYN,
CUDT_RCVSYN,
CUDT_CC,
CUDT_FC,
CUDT_SNDBUF,
CUDT_RCVBUF,
CUDP_SNDBUF,
CUDP_RCVBUF,
CUDT_LINGER,
CUDT_RENDEZVOUS,
CUDT_SNDTIMEO,
CUDT_RCVTIMEO,

}sockopt_type;


typedef enum
{
    CUDTSTATE_INIT = 1, 
    CUDTSTATE_OPENED, 
    CUDTSTATE_LISTENING, 
    CUDTSTATE_CONNECTING, 
    CUDTSTATE_CONNECTED, 
    CUDTSTATE_BROKEN, 
    CUDTSTATE_CLOSING, 
    CUDTSTATE_CLOSED, 
    CUDTSTATE_NONEXIST
}sock_state;

    int wrapper_startUDT();
    int wrapper_stopUDT();
    int wrapper_createSocket(int af, int type);
    int wrapper_setSockOpt(int socket,int level, int sockopt, const char* optval, int optlen);
    int wrapper_connect(int socket, const struct sockaddr* name, int namelen);
    int wrapper_closeSocket(int socket);
    int wrapper_send(int socket, const char* buf, int len, int flags);
    int wrapper_receive(int socket, char* buf, int len, int flags);
    int wrapper_accept(int socket, struct sockaddr* name, int* namelen);
    int wrapper_bind(int socket, const struct sockaddr* name, int namelen);
    int wrapper_listen(int socket,int numsock);
    const char* wrapper_getErrorMessage();
    int wrapper_getErrorCode();
    sock_state wrapper_getSocketStatus(int socket);

#ifdef __cplusplus
}
#endif

#endif

