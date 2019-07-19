/***************************************************************************
 *   Copyright (C) 2018 Microsemi Corporation                              *
 *   soc_tech@microsemi.com                                                *
 ***************************************************************************/


#include "microsemi_api_calls.h"


const char *microsemi_fp_request_names[] = {
  "hello",
  "set_usb_port",
  "fprq_raw_logging",
  "initialize",
  "quit",
  "set_ujtag",
  "speed",
  "speed_div",
  "execute_scan",
  "execute_statemove",
  "execute_runtest",
  "execute_reset",
  "execute_pathmove",
  "execute_sleep",
  "N/A",
  "shutdown",
  "profiling",
  "set_timeouts",
  "stall",
  "set_server_file_logger",
  "N/A"
};

// Weights how slow each call is expected to be, this will be used to calculate how much time it will be given before a
// timeout expires, a microsemi_timeout.c -> microsemi_client_timeout variable is used to multiply and timeout thread
// is setup inside the microsemi_socket_client
const int microsemi_fp_request_timeout_weights[] = {
  [fprq_hello]                      = 1,
  [fprq_raw_set_usb_port]           = 1,
  [fprq_raw_logging]                = 1,
  [fprq_raw_initialize]             = 80,
  [fprq_raw_quit]                   = 50,
  [fprq_raw_set_ujtag]              = 3,
  [fprq_raw_speed]                  = 3,
  [fprq_raw_speed_div]              = 3,
  [fprq_raw_execute_scan]           = 12,
  [fprq_raw_execute_statemove]      = 6,
  [fprq_raw_execute_runtest]        = 6,    // runtest is used by riscv to delay flow
  [fprq_raw_execute_reset]          = 6,
  [fprq_raw_execute_pathmove]       = 6,
  [fprq_raw_execute_sleep]          = 90,
  [fprq_raw_END]                    = 1,    // do not use this command, only used as separator
  [fprq_mng_shutdown]               = 70,
  [fprq_mng_profiling]              = 2,
  [fprq_mng_timeouts]               = 1,
  [fprw_mng_stall]                  = 20,
  [fprw_mng_set_server_file_logger] = 1
};

