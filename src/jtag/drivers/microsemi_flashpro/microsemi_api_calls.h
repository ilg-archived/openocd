/***************************************************************************
 *   Copyright (C) 2018 Microsemi Corporation                              *
 *   soc_tech@microsemi.com                                                *
 ***************************************************************************/


#ifndef MICROSEMI_API_CALLS_H
#define MICROSEMI_API_CALLS_H

#include "microsemi_version.h"

// Requests
typedef enum {
    fprq_hello                       = 0,

    fprq_raw_set_usb_port            = 1,
    fprq_raw_logging                 = 2,
    fprq_raw_initialize              = 3,
    fprq_raw_quit                    = 4,
    fprq_raw_set_ujtag               = 5,
    fprq_raw_speed                   = 6,
    fprq_raw_speed_div               = 7,
    fprq_raw_execute_scan            = 8,
    fprq_raw_execute_statemove       = 9,  // TLR_reset
    fprq_raw_execute_runtest         = 10,
    fprq_raw_execute_reset           = 11,
    fprq_raw_execute_pathmove        = 12,
    fprq_raw_execute_sleep           = 13,
    fprq_raw_END                     = 14,

    fprq_mng_shutdown                = 15,
    fprq_mng_profiling               = 16,
    fprq_mng_timeouts                = 17,
    fprw_mng_stall                   = 18,
    fprw_mng_set_server_file_logger  = 19,

    fprq_END
} microsemi_fp_request;


extern const char *microsemi_fp_request_names[];

extern const int   microsemi_fp_request_timeout_weights[];

volatile int fpserver_keep_running;

#endif //MICROSEMI_API_CALLS_H
