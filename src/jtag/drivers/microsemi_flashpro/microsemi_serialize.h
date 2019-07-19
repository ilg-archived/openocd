/***************************************************************************
 *   Copyright (C) 2018 Microsemi Corporation                              *
 *   soc_tech@microsemi.com                                                *
 ***************************************************************************/


#ifndef MICROSEMI_SERIALIZE_H
#define MICROSEMI_SERIALIZE_H

#include "microsemi_api_calls.h"

#ifdef FP_SERVER_SIDE
#include "jtag_and_dependencies.h"
#else
#include <jtag/interface.h>
#endif

#include "libbinn/include/binn.h"

// the request serializers will return the api call ID of the command, which then can be used in the
// table microsemi_fp_request_timeout_weights to calculate aproperiate timeout settings for that command
// slower commands will get their timeout multiplied while faster commands can have timeout tight

microsemi_fp_request serialize_hello(              binn *handle);
microsemi_fp_request serialize_set_usb_port(       binn *handle, const char *port);
microsemi_fp_request serialize_logging(            binn *handle, bool verbosity_enable);
microsemi_fp_request serialize_init_request(       binn *handle);
microsemi_fp_request serialize_speed(              binn *handle, int speed);
microsemi_fp_request serialize_speed_div(          binn *handle, int speed);
microsemi_fp_request serialize_quit_request(       binn *handle);
microsemi_fp_request serialize_ujtag_set(          binn *handle, bool ujtag_enable);
microsemi_fp_request serialize_profiling(          binn *handle);
microsemi_fp_request serialize_server_file_logging(binn *handle, bool log_to_file);

microsemi_fp_request serialize_scan_command(       binn *handle, struct scan_command      *command);
microsemi_fp_request serialize_statemove_command(  binn *handle, struct statemove_command *command);
microsemi_fp_request serialize_runtest_command(    binn *handle, struct runtest_command   *command);
microsemi_fp_request serialize_reset_command(      binn *handle, struct reset_command     *command);
microsemi_fp_request serialize_sleep_command(      binn *handle, struct sleep_command     *command);
microsemi_fp_request serialize_pathmove(           binn *handle, struct pathmove_command  *command);

// response serializers do not wait for response (because they ARE the response), because of no waiting
// there is no timeout and therefore they do not need to return api call ID

void serialize_response_hello(    binn *handle, int codeVersion, int apiVersion);
void serialize_response_code(     binn *handle, int code);
void serialize_response_speed_div(binn *handle, int code, int khz);
void serialize_response_profiling(binn *handle, char *str);


#endif //MICROSEMI_SERIALIZE_H
