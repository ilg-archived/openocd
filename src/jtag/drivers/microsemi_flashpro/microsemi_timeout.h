/***************************************************************************
 *   Copyright (C) 2018 Microsemi Corporation                              *
 *   soc_tech@microsemi.com                                                *
 ***************************************************************************/


#ifndef FPSERVER_MICROSEMI_TIMEOUTS_H
#define FPSERVER_MICROSEMI_TIMEOUTS_H

extern int microsemi_hardware_timeout;
extern int microsemi_client_timeout;
extern int microsemi_serverautostart_timeout;
extern int microsemi_serveridle_timeout;                    // default value, less agressive
extern int microsemi_serveridle_timeout_when_autostarted;   // very tight value when started from fpClient/openOCD

int  microsemi_hardware_set_timeout(int timeout);
int  microsemi_server_set_autostart_timeout(int timeout);
int  microsemi_serveridle_set_timeout(int timeout);
int  microsemi_client_set_timeout(int timeout);

#endif //FPSERVER_MICROSEMI_TIMEOUTS_H
