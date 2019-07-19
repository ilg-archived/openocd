/***************************************************************************
 *   Copyright (C) 2018 Microsemi Corporation                              *
 *   soc_tech@microsemi.com                                                *
 ***************************************************************************/


#ifndef MICROSEMI_FPSERVER_SOCKET_H
#define MICROSEMI_FPSERVER_SOCKET_H

#define MICROSEMI_MAX_SOCKET_BUFFER_SIZE 50000

#ifdef __MINGW32__
  #include <winsock2.h>
  #include <windows.h>
  #include <ws2tcpip.h>
#else
  #include <arpa/inet.h>
  #include <sys/socket.h>
#endif

#include "libbinn/include/binn.h"

#endif //MICROSEMI_FPSERVER_SOCKET_H
