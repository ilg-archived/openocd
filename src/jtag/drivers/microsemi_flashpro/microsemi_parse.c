/***************************************************************************
 *   Copyright (C) 2018 Microsemi Corporation                              *
 *   soc_tech@microsemi.com                                                *
 ***************************************************************************/

//#define MICROSEMI_PARSE_VERBOSE 1  // to log from this source file this define needs to be uncommentd and it needs to be compiled for the fpServer side

#include <stdio.h>
#include <stdlib.h>

#include "microsemi_parse.h"

#ifdef FP_SERVER_SIDE
// use the file logging feature only if this code is executed on the fpServer side
#include "microsemi_logger.h"
#endif

struct reset_command parse_reset_command(binn *handle) {
  struct reset_command command = {
    .trst = binn_list_int8(handle, 2)
  };
  binn_free(handle);
  return command;
}


struct runtest_command parse_runtest_command(binn *handle) {
  struct runtest_command command = {
    .num_cycles = binn_list_int32(handle, 2),
    .end_state  = binn_list_int8(handle,  3)
  };
  binn_free(handle);
  return command;
}


struct sleep_command parse_sleep_command(binn *handle) {
  struct sleep_command command = {
    .us = binn_list_uint32(handle, 2)
  };
  binn_free(handle);
  return command;
}


struct pathmove_command parse_pathmove_command(binn *handle) {
  struct pathmove_command command = {
    .num_states = binn_list_int32(handle, 2),
    .path = (tap_state_t *) malloc(sizeof(tap_state_t) * command.num_states)
  };

  tap_state_t *iterator = command.path;

#ifdef MICROSEMI_PARSE_VERBOSE
#ifdef FP_SERVER_SIDE
  microsemi_log_verbose("num_states=%d", command.num_states);
#endif
#endif

  int i;
  for (i=0; i<command.num_states; i++, iterator++) {
    // not counting from zero and already used first entry for command type and second entry for num_states
    *iterator = binn_list_int32(handle, i + 3);

#ifdef MICROSEMI_PARSE_VERBOSE
#ifdef FP_SERVER_SIDE
    microsemi_log_verbose("\\ state[%2d]=%d", i, *iterator);
#endif
#endif
  }
  binn_free(handle);
  return command;
}


void destroy_pathmove_command(struct pathmove_command *command) {
  free(command->path);
}


struct statemove_command parse_statemove_command(binn *handle) {
  struct statemove_command command = {
    .end_state = binn_list_int32(handle, 2)
  };
  binn_free(handle);
  return command;
}


struct scan_command parse_scan_command(binn *handle) {
  unsigned int count = 2;

  struct scan_command command;
  command.ir_scan    = binn_list_bool(handle,  count++);
  command.end_state  = binn_list_int32(handle, count++);
  command.num_fields = binn_list_int32(handle, count++);
  command.fields     = (struct scan_field*) malloc(sizeof(struct scan_field) * command.num_fields);

#ifdef MICROSEMI_PARSE_VERBOSE
#ifdef FP_SERVER_SIDE
  microsemi_log_verbose("ir_scan=%d end_state=%d num_fields=%d", command.ir_scan, command.end_state, command.num_fields);
#endif
#endif

  struct scan_field *field_it = command.fields;

  int i;
  for (i=0; i<command.num_fields; i++, field_it++) {
    struct scan_field field;
    field.num_bits      = binn_list_int32(handle, count++);
    bool skip_out_value = binn_list_bool(handle, count++);
    bool skip_in_value  = binn_list_bool(handle, count++);
    int  num_bytes      = (field.num_bits + 7) / 8;

#ifdef MICROSEMI_PARSE_VERBOSE
#ifdef FP_SERVER_SIDE
    microsemi_log_verbose("\\ field[%2d] num_bits=%d num_bytes=%d skip_out_value=%d skip_in_value=%d", i, field.num_bits, num_bytes, skip_out_value, skip_in_value);
#endif
#endif

    if (!skip_out_value) {
      field.out_value    = (unsigned char *) malloc(sizeof(unsigned char) * num_bytes);
    }
    else {
      field.out_value    = NULL;
    }

    if (!skip_in_value) {
      field.in_value    = (unsigned char *) malloc(sizeof(unsigned char) * num_bytes);
    }
    else {
      field.in_value    = NULL;
    }

    field.check_value   = (unsigned char *) malloc(sizeof(unsigned char) * num_bytes);
    field.check_mask    = (unsigned char *) malloc(sizeof(unsigned char) * num_bytes);

    uint8_t *out_val_it = field.out_value;
    uint8_t *in_val_it  = field.in_value;

    int j;
    for (j=0; j*8<field.num_bits; j++, out_val_it++, in_val_it++) {
      if (!skip_out_value) {
        *out_val_it     = binn_list_uint8(handle, count++);
#ifdef MICROSEMI_PARSE_VERBOSE
#ifdef FP_SERVER_SIDE
        microsemi_log_verbose("  \\ out[%2d]=0x%02x", j, *out_val_it);
#endif
#endif
      }

      if (!skip_in_value) {
        *in_val_it      = binn_list_uint8(handle, count++);
#ifdef MICROSEMI_PARSE_VERBOSE
#ifdef FP_SERVER_SIDE
        microsemi_log_verbose("  \\  in[%2d]=0x%02x", j, *in_val_it);
#endif
#endif
      }
    }

    *field_it = field;
  }
  binn_free(handle);
  return command;
}


void mutate_scan_command(binn *handle, struct scan_command *command) {
  unsigned int count = 2;

  command->ir_scan    = binn_list_bool(handle,  count++);
  command->end_state  = binn_list_int32(handle, count++);
  command->num_fields = binn_list_int32(handle, count++);

  struct scan_field *field_it = command->fields;

#ifdef MICROSEMI_PARSE_VERBOSE
#ifdef FP_SERVER_SIDE
  microsemi_log_verbose("ir_scan=%d, end_state=%d, num_fields=%d",
         command->ir_scan, command->end_state, command->num_fields);
#endif
#endif

  int i;
  for (i=0; i<command->num_fields; i++, field_it++) {
    field_it->num_bits    = binn_list_int32(handle, count++);
    bool skip_out_value   = binn_list_bool(handle, count++);
    bool skip_in_value    = binn_list_bool(handle, count++);

    if (!skip_in_value && field_it->in_value == NULL) {
      // when there are IN values in the stream but current structure has the IN not allocated/initialized
      int num_bytes = (field_it->num_bits + 7) / 8;
#ifdef MICROSEMI_PARSE_VERBOSE
#ifdef FP_SERVER_SIDE
      microsemi_log_verbose("Allocating mem for the new in_val which were not allocated in this structure yet, size=%d",
             sizeof(unsigned char) * num_bytes);
#endif
#endif
      field_it->in_value = (unsigned char *) malloc(sizeof(unsigned char) * num_bytes);   // TODO: I don't know how properly deallocate this :-( or if it's dealocated by something above me?
    }
    uint8_t *in_value_it = field_it->in_value;  // setting the pointer correctly, if it was freshly allocated or not

#ifdef MICROSEMI_PARSE_VERBOSE
#ifdef FP_SERVER_SIDE
    microsemi_log_verbose("\\ field[%2d] num_bits=%d skip_out_value=%d skip_in_value=%d", i, field_it->num_bits,skip_out_value, skip_in_value);
#endif
#endif

    int j;
    for (j=0; j*8<field_it->num_bits; j++, in_value_it++) {
      // do not modify const out_val, but read the value from the stream
      unsigned char out; // not using it here anyway

      if (!skip_out_value) {
        out = binn_list_uint8(handle, count++); // read it from the stream and ignore
#ifdef MICROSEMI_PARSE_VERBOSE
#ifdef FP_SERVER_SIDE
        microsemi_log_verbose("  \\ out[%2d]=0x%02x but will be ignored", j, out);
#endif
#endif
      }

      if (!skip_in_value) {
        *in_value_it    = binn_list_uint8(handle, count++);
#ifdef MICROSEMI_PARSE_VERBOSE
#ifdef FP_SERVER_SIDE
        microsemi_log_verbose("  \\  in[%2d]=0x%02x", j, *in_value_it);
#endif
#endif
      }

    }
  }
  binn_free(handle);
}


void destroy_scan_command(struct scan_command *command) {
  struct scan_field *field_it = command->fields;

  int i;
  for (i=1; i<=command->num_fields; i++, field_it++) {
    if (field_it->out_value   != NULL) free((void *)field_it->out_value);  //TODO: Investigate if it's correct: that on server side possible, might not be possible on client
    if (field_it->in_value    != NULL) free(field_it->in_value);
    if (field_it->check_value != NULL) free(field_it->check_value);
    if (field_it->check_mask  != NULL) free(field_it->check_mask);
  }

  free(command->fields);
}


bool parse_ujtag_state(binn *handle) {
  const unsigned char ret = binn_list_uint8(handle, 2);
  binn_free(handle);
  return ret;
}


// controls logging inside FP implementation
bool parse_logging(binn *handle) {
  const unsigned char ret = binn_list_uint8(handle, 2);
  binn_free(handle);
  return ret;
}


// controls logging of the API calls/timeouts
bool parse_server_file_logging(binn *handle) {
  const unsigned char ret = binn_list_uint8(handle, 2);
  binn_free(handle);
  return ret;
}


char* parse_set_port(binn *handle) {
  char *const ret = binn_list_str(handle, 2);
  binn_free(handle);
  return ret;
}


void parse_timeouts(binn *handle, int *hardware, int *client) {
  *hardware = binn_list_int32(handle, 2);
  *client   = binn_list_int32(handle, 3);
  binn_free(handle);
}



int parse_speed(binn *handle) {
  const int ret = binn_list_int32(handle, 2);
  binn_free(handle);
  return ret;
}


int parse_response_basic(binn *handle) {
  const int ret = binn_list_int32(handle, 1);
  binn_free(handle);
  return ret;
}


int parse_response_speed_div(binn *handle, int *khz) {
  int ret = binn_list_int32(handle, 1);
  *khz    = binn_list_int32(handle, 2);
  binn_free(handle);
  return ret;
}


void parse_response_hello(binn *handle, int *codeVersion, int *apiVersion) {
  *codeVersion = binn_list_int32(handle, 1);
  *apiVersion  = binn_list_int32(handle, 2);
  binn_free(handle);
}


void parse_response_profiling(binn *handle, char **str) {
  *str = binn_list_str(handle, 1);
  binn_free(handle);
}

