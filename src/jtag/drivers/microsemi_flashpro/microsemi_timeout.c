/***************************************************************************
 *   Copyright (C) 2018 Microsemi Corporation                              *
 *   soc_tech@microsemi.com                                                *
 ***************************************************************************/


#include "microsemi_timeout.h"

int microsemi_hardware_timeout                    = 1700;  // in ms, currently not used
int microsemi_client_timeout                      = 0;     // in ms, this timeout will get multiplied by microsemi_api_calls.c -> microsemi_fp_request_timeout_weights for each call allowing more time on slower calls
int microsemi_serverautostart_timeout             = 14000; // in ms, how much time will be added to a call if that call happens to being autostarting the server
int microsemi_serveridle_timeout                  = 120;   // in seconds, if server will not recieve any new request in that time, it will shutdown itself (on a startup user will get 2 hearbeats, so in the very begining user gets twice as much time to get client connected to it)
int microsemi_serveridle_timeout_when_autostarted = 0;     // in seconds will overide the microsemi_serveridle_timeout when the fpServer is started automatically from client


int microsemi_hardware_set_timeout(int timeout) {
  microsemi_hardware_timeout = timeout;
  return 0;
}


int microsemi_server_set_autostart_timeout(int timeout) {
  microsemi_serverautostart_timeout = timeout;
  return 0;
}


int  microsemi_serveridle_set_timeout(int timeout) {
  microsemi_serveridle_timeout = timeout;
}


int microsemi_client_set_timeout(int timeout) {
  microsemi_client_timeout = timeout;
  return 0;
}

