/***************************************************************************
 *   Copyright (C) 2018 Microsemi Corporation                              *
 *   soc_tech@microsemi.com                                                *
 ***************************************************************************/


#ifndef MICROSEMI_FPSERVER_SOCKETCLIENT_H
#define MICROSEMI_FPSERVER_SOCKETCLIENT_H

#include <stdbool.h>
#include "microsemi_socket.h"
#include "microsemi_api_calls.h"

#ifdef __MINGW32__
  SOCKET connectSocket;
#else
  int connectSocket;
#endif

#define MICROSEMI_IP_STRING_LEN 16
#define MICROSEMI_CURRENT_PATH 2048
#define MICROSEMI_SERVER_PATH_STRING_LEN 512

extern        bool         microsemi_socket_connected;
extern        int          microsemi_socket_port;
extern        char         microsemi_socket_ip[MICROSEMI_IP_STRING_LEN];
extern        bool         microsemi_server_autostart;
extern        bool         microsemi_server_autokill;
extern struct sockaddr_in  microsemi_server;
extern        char         microsemi_server_reply[MICROSEMI_MAX_SOCKET_BUFFER_SIZE];
extern        char         microsemi_server_path[MICROSEMI_SERVER_PATH_STRING_LEN];
extern        char         microsemi_server_path[MICROSEMI_SERVER_PATH_STRING_LEN];
extern        char         microsemi_server_binary[MICROSEMI_SERVER_PATH_STRING_LEN];
extern        char         microsemi_current_path[MICROSEMI_CURRENT_PATH];
extern        char         microsemi_client_path[MICROSEMI_SERVER_PATH_STRING_LEN];
extern        char         microsemi_server_absolute_basepath[MICROSEMI_SERVER_PATH_STRING_LEN];

#ifdef __MINGW32__
extern PROCESS_INFORMATION microsemi_server_process_info;
#else
extern        pid_t        microsemi_server_pid;
#endif

int  microsemi_socket_set_server_path(const char *path);
int  microsemi_socket_set_ipv4(const char *ip);
int  microsemi_socket_set_port(int port);
void microsemi_server_set_autostart(bool autostart);
void microsemi_server_set_autokill(bool autokill);

int  microsemi_socket_connect(void);
int  microsemi_socket_send(binn *request, binn **response, microsemi_fp_request timeout_type);
int  microsemi_socket_close(void);
void microsemi_client_settings(void);


#endif //MICROSEMI_FPSERVER_SOCKETCLIENT_H
