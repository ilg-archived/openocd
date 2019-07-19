/***************************************************************************
 *   Copyright (C) 2018 Microsemi Corporation                              *
 *   soc_tech@microsemi.com                                                *
 ***************************************************************************/

#ifndef MICROSEMI_PARSE_H
#define MICROSEMI_PARSE_H

#ifdef FP_SERVER_SIDE
#include "jtag_and_dependencies.h"
#else
#include <jtag/interface.h>
#endif
#include "libbinn/include/binn.h"

void                     parse_response_hello(binn *handle, int *codeVersion, int *apiVersion);
struct scan_command      parse_scan_command(          binn *handle);
void                     mutate_scan_command(         binn *handle, struct scan_command *command);
struct statemove_command parse_statemove_command(     binn *handle);
struct runtest_command   parse_runtest_command(       binn *handle);
struct reset_command     parse_reset_command(         binn *handle);
struct pathmove_command  parse_pathmove_command(      binn *handle);
struct sleep_command     parse_sleep_command(         binn *handle);
bool                     parse_ujtag_state(           binn *handle);
bool                     parse_logging(               binn *handle); // controls logging inside FP implementation
bool                     parse_server_file_logging(   binn *handle); // controls logging of the API calls/timeouts
char*                    parse_set_port(              binn *handle);
void                     parse_timeouts(              binn *handle, int *hardware, int *client);

int                      parse_response_basic(        binn *handle);
int                      parse_response_speed_div(    binn *handle, int *khz);
void                     parse_response_profiling(    binn *handle, char **str);

int parse_speed(binn *handle);

void destroy_pathmove_command(struct pathmove_command *command);
void destroy_scan_command(    struct scan_command     *command);



#endif //MICROSEMI_PARSE_H
