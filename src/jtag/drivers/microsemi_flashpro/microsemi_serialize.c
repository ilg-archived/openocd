/***************************************************************************
 *   Copyright (C) 2018 Microsemi Corporation                              *
 *   soc_tech@microsemi.com                                                *
 ***************************************************************************/

//#define MICROSEMI_SERIALIZER_VERBOSE 1

#include "microsemi_serialize.h"

#ifdef FP_SERVER_SIDE
// use the file logging feature only if this code is executed on the fpServer side, while
// if it's used from client and verbosity is on use slightly different logs
#include "microsemi_logger.h"
#endif


microsemi_fp_request serialize_hello(binn *handle) {
  binn_list_add_uint8(handle, fprq_hello);
  return fprq_hello;
}


microsemi_fp_request serialize_init_request(binn *handle) {
  binn_list_add_uint8(handle, fprq_raw_initialize);
  return fprq_raw_initialize;
}


microsemi_fp_request serialize_quit_request(binn *handle) {
  binn_list_add_uint8(handle, fprq_raw_quit);
  return fprq_raw_quit;
}


microsemi_fp_request serialize_speed(binn *handle, int speed) {
  binn_list_add_uint8(handle, fprq_raw_speed);
  binn_list_add_int32(handle, speed);
  return fprq_raw_speed;
}


microsemi_fp_request serialize_speed_div(binn *handle, int speed) {
  binn_list_add_uint8(handle, fprq_raw_speed_div);
  binn_list_add_int32(handle, speed);
  return fprq_raw_speed_div;
}


microsemi_fp_request serialize_reset_command(binn *handle, struct reset_command *command) {
  binn_list_add_uint8(handle, fprq_raw_execute_reset);
  binn_list_add_int8(handle,  command->trst);
  return fprq_raw_execute_reset;
}


microsemi_fp_request serialize_runtest_command(binn *handle, struct runtest_command *command) {
  binn_list_add_uint8(handle, fprq_raw_execute_runtest);
  binn_list_add_int32(handle, command->num_cycles);
  binn_list_add_int8(handle,  command->end_state);
  return fprq_raw_execute_runtest;
}


microsemi_fp_request serialize_sleep_command(binn *handle, struct sleep_command *command) {
  binn_list_add_uint8(handle,  fprq_raw_execute_sleep);
  binn_list_add_uint32(handle, command->us);
  return fprq_raw_execute_sleep;
}


microsemi_fp_request serialize_pathmove(binn *handle, struct pathmove_command *command) {
  binn_list_add_uint8(handle, fprq_raw_execute_pathmove);
  binn_list_add_int32(handle, command->num_states);
  tap_state_t *iterator = command->path;

#ifdef MICROSEMI_SERIALIZER_VERBOSE
#ifndef FP_SERVER_SIDE
  printf("Serialize pathmove num_states=%d \n", command->num_states);
#endif
#endif

  int i;
  for (i=0; i<command->num_states; i++, iterator++) {
    binn_list_add_int32(handle, *iterator);
#ifdef MICROSEMI_SERIALIZER_VERBOSE
#ifndef FP_SERVER_SIDE
    printf("\\ move=%d state=%d \n", i, *iterator);
#endif
#endif
  }
  return fprq_raw_execute_pathmove;
}


microsemi_fp_request serialize_statemove_command(binn *handle, struct statemove_command *command) {
  binn_list_add_uint8(handle, fprq_raw_execute_statemove);
  binn_list_add_int32(handle, command->end_state);
  return fprq_raw_execute_statemove;
}


microsemi_fp_request serialize_scan_command(binn *handle, struct scan_command *command) {
  binn_list_add_uint8(handle, fprq_raw_execute_scan);
  binn_list_add_bool(handle,  command->ir_scan);
  binn_list_add_int32(handle, command->end_state);
  binn_list_add_int32(handle, command->num_fields);

#ifdef MICROSEMI_SERIALIZER_VERBOSE
// logging behaves differently when the code is run as client or when it's run from server
#ifdef FP_SERVER_SIDE
  microsemi_log_verbose("ir_scan=%d end_state=%d num_fields=%d",
         command->ir_scan, command->end_state, command->num_fields);
#else
  printf("Serialize scan ir_scan=%d end_state=%d num_fields=%d \n",
         command->ir_scan, command->end_state, command->num_fields);
#endif
#endif

  struct scan_field *field_it = command->fields;

  int i;
  for (i=0; i<command->num_fields; i++, field_it++) {
    bool skip_in_value  = (field_it->in_value  == NULL); // "in" null means sending data into the target with "out" and not carrying what is coming back
    bool skip_out_value = (field_it->out_value == NULL); // pad "out" with zeros as anything is fine, because you are only interested what is coming back in the "in"

#ifdef MICROSEMI_SERIALIZER_VERBOSE
#ifdef FP_SERVER_SIDE
    microsemi_log_verbose("\\ field[%2d] num_bits=%d skip_out_value=%d skip_in_value=%d", i, field_it->num_bits, skip_out_value, skip_in_value);
#else
    printf("\\ field[%2d] num_bits=%d skip_out_value=%d skip_in_value=%d ", i, field_it->num_bits, skip_out_value, skip_in_value);
#endif
#endif

    binn_list_add_int32(handle, field_it->num_bits);
    binn_list_add_bool(handle, skip_out_value);
    binn_list_add_bool(handle, skip_in_value);

    uint8_t *out_val_it = field_it->out_value;
    uint8_t *in_val_it  = field_it->in_value;

    int j;
    for (j=0; j*8<field_it->num_bits; j++, out_val_it++, in_val_it++) {

      if (!skip_out_value) {
        binn_list_add_uint8(handle, *out_val_it);
#ifdef MICROSEMI_SERIALIZER_VERBOSE
#ifdef FP_SERVER_SIDE
        microsemi_log_verbose("  \\ out[%2d]=0x%02x", j, *out_val_it);
#else
        printf("(%2d out=0x%02x)", j, *out_val_it);
#endif
#endif
      }

      if (!skip_in_value) {
        binn_list_add_uint8(handle, *in_val_it);
#ifdef MICROSEMI_SERIALIZER_VERBOSE
#ifdef FP_SERVER_SIDE
        microsemi_log_verbose("  \\  in[%2d]=0x%02x", j, *in_val_it);
#else
        printf("(%2d in=0x%02x)", j, *in_val_it);
#endif
#endif
      }

    }
#ifdef MICROSEMI_SERIALIZER_VERBOSE
#ifndef FP_SERVER_SIDE
    printf("\n");
#endif
#endif
  }
  return fprq_raw_execute_scan;
}


microsemi_fp_request serialize_profiling(binn *handle) {
  binn_list_add_uint8(handle, fprq_mng_profiling);
  return fprq_mng_profiling;
}


microsemi_fp_request serialize_ujtag_set(binn *handle, bool ujtag_enable) {
  binn_list_add_uint8(handle, fprq_raw_set_ujtag);
  binn_list_add_uint8(handle, ujtag_enable);
  return fprq_raw_set_ujtag;
}


// controls logging inside FP implementation
microsemi_fp_request  serialize_logging(binn *handle, bool verbosity_enable) {
  binn_list_add_uint8(handle, fprq_raw_logging);
  binn_list_add_uint8(handle, verbosity_enable);
  return fprq_raw_logging;
}


// controls logging of the API calls/timeouts
microsemi_fp_request  serialize_server_file_logging(binn *handle, bool log_to_file) {
  binn_list_add_uint8(handle, fprw_mng_set_server_file_logger);
  binn_list_add_uint8(handle, log_to_file);
  return fprw_mng_set_server_file_logger;
}


#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdiscarded-qualifiers"
microsemi_fp_request serialize_set_usb_port(binn *handle, const char *port) {
  binn_list_add_uint8(handle, fprq_raw_set_usb_port);
  binn_list_add_str(handle, port);                    // the binn is using non-const chars, but it's not modifying them
  return fprq_raw_set_usb_port;
}
#pragma GCC diagnostic pop


int serialize_set_timeouts(binn *handle, int hardware_timeout, int client_timeout) {
  binn_list_add_uint8(handle, fprq_mng_timeouts);
  binn_list_add_int32(handle, hardware_timeout);
  binn_list_add_int32(handle, client_timeout);
  return fprq_mng_timeouts;
}


void serialize_response_hello(binn *handle, int codeVersion, int apiVersion) {
  binn_list_add_int32(handle, codeVersion);
  binn_list_add_int32(handle, apiVersion);
}


void serialize_response_code(binn *handle, int code) {
  binn_list_add_int32(handle, code);
}


void serialize_response_speed_div(binn *handle, int code, int khz) {
  binn_list_add_int32(handle, code);
  binn_list_add_int32(handle, khz);
}


void serialize_response_profiling(binn *handle, char *str) {
  binn_list_add_str(handle, str);
#ifdef MICROSEMI_SERIALIZER_VERBOSE
#ifdef FP_SERVER_SIDE
  microsemi_log_verbose("Populated profiling info, whole binn payload is %d", binn_size(handle));
#else
  printf("Populated profiling info, whole binn payload is %d \n", binn_size(handle));
#endif
#endif
}