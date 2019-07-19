/***************************************************************************
 *   Copyright (C) 2015-2018 Microchip Technology Inc.                     *
 *   http://www.microchip.com/support                                      *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. *
 ***************************************************************************/
 
/* Uncomment this for FlashPro JTAG scan logging: */
// #define MICROSEMI_FLASHPRO_DEBUG 1
 
 /*
 * Microsemi FlashPro JTAG driver (via FpcommWrapper API/DLL) for OpenOCD
 * http://www.microsemi.com/products/fpga-soc/design-resources/programming/flashpro
 */
 
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* For struct jtag_interface */
#include <jtag/interface.h>

#include "microsemi_api_calls.c"
#include "microsemi_serialize.c"
#include "microsemi_parse.c"
#include "microsemi_socket_client.h"
#include "microsemi_timeout.h"


/* Useful defines */
#define HZ_PER_KHZ                        1000        /* 1KHz = 1000Hz! :-) */


#ifdef MICROSEMI_FLASHPRO_DEBUG 
static void print_scan_bits(char *pstr, int nbits, const uint8_t *pscanbits)
{
    int i = 0;    
    for (*pstr = '\0'; (i * 8) < nbits; i++)
    {
        sprintf((pstr + (i * 2)), "%02x", (NULL == pscanbits) ? 0 : *(pscanbits + i));
    }
}
#endif


static const Jim_Nvp jim_nvp_boolean_options[] = {
    { .name = "off",      .value = 0  },
    { .name = "on",       .value = 1  },
    { .name = "disable",  .value = 0  },
    { .name = "enable",   .value = 1  },
    { .name = "0",        .value = 0  },
    { .name = "1",        .value = 1  },
    { .name = NULL,       .value = -1 },
};

const char jim_nvp_boolean_options_description[] = "['off'|'on'|'disable'|'enable'|'0'|'1']";


static int microsemi_flashpro_speed(int speed)
{
#ifdef MICROSEMI_FLASHPRO_DEBUG 
    LOG_INFO("%s(%d)", __FUNCTION__, speed);
#endif
    binn *request  = binn_list();
    binn *response;
    microsemi_fp_request delay_type = serialize_speed(request, speed);
    if (microsemi_socket_send(request, &response, delay_type)) {
        LOG_ERROR("fpClient, call 'speed' to fpServer expired.");
        return ERROR_COMMAND_CLOSE_CONNECTION;
        // return ERROR_FAIL;
    }
    else {
        return parse_response_basic(response);
    }
}


static int microsemi_flashpro_speed_div(int speed, int *khz)
{
#ifdef MICROSEMI_FLASHPRO_DEBUG 
    LOG_INFO("%s(%d)", __FUNCTION__, speed);
#endif
    int retval = ERROR_OK;
    
    binn *request  = binn_list();
    binn *response;
    microsemi_fp_request delay_type = serialize_speed_div(request, speed);

    if (microsemi_socket_send(request, &response, delay_type)) {
        LOG_ERROR("fpClient, call 'speed_div' to fpServer expired.");
        return ERROR_COMMAND_CLOSE_CONNECTION;
        // return -1;
    }
    else {
        retval = parse_response_speed_div(response, khz);
        //printf("Got response back with code %d and khz=%d \n", retval, khz);
        return retval;
    }
}


static int microsemi_flashpro_khz(int khz, int *jtag_speed)
{
#ifdef MICROSEMI_FLASHPRO_DEBUG 
    LOG_INFO("%s(%d)", __FUNCTION__, khz);
#endif
    *jtag_speed = khz * HZ_PER_KHZ;
    return ERROR_OK;
}


static int microsemi_flashpro_execute_scan(struct scan_command *cmd)
{    
#ifdef MICROSEMI_FLASHPRO_DEBUG 
    LOG_INFO("%s ir_scan=%d end_state=%d num_fields=%d", __FUNCTION__, cmd->ir_scan, cmd->end_state, cmd->num_fields);
    // struct scan_field *field_it = cmd->fields;

    // for (int i=0; i<cmd->num_fields; i++, field_it++) {
    //     LOG_INFO("%s field=%d num_bits=%d", __FUNCTION__, i, field_it->num_bits);
    // }
#endif    
    binn *request = binn_list();
    binn *response;
    microsemi_fp_request delay_type = serialize_scan_command(request, cmd);
    if (microsemi_socket_send(request, &response, delay_type)) {
        LOG_ERROR("fpClient, call 'execute_scan' to fpServer expired.");
        return 1;
    }
    else {
        mutate_scan_command(response, cmd);
    }
    return 0;
}


static int microsemi_flashpro_execute_statemove(struct statemove_command *cmd)
{    
#ifdef MICROSEMI_FLASHPRO_DEBUG 
    LOG_INFO("%s state=%d", __FUNCTION__, cmd->end_state);
#endif    
    binn *request = binn_list();
    binn *response;
    microsemi_fp_request delay_type = serialize_statemove_command(request, cmd);
    if (microsemi_socket_send(request, &response, delay_type)) {
        LOG_ERROR("fpClient, call 'execute_statemove' to fpServer expired.");
        return 1;
    }
    int response_code = parse_response_basic(response); // will free the response handler
    //printf("Got response back with code %d \n", response_code); 
    return 0;
}


static int microsemi_flashpro_execute_runtest(struct runtest_command *cmd)
{
#ifdef MICROSEMI_FLASHPRO_DEBUG 
    LOG_INFO("%s num_cycles=%d", __FUNCTION__, cmd->num_cycles);
#endif
    binn *request = binn_list();
    binn *response;
    microsemi_fp_request delay_type = serialize_runtest_command(request, cmd);
    if (microsemi_socket_send(request, &response, delay_type)) {
        LOG_ERROR("fpClient, call 'execute_runtest' to fpServer expired.");
        return 1;
    }
    int response_code = parse_response_basic(response); // will free the response handler
    //printf("Got response back with code %d \n", response_code); 
    return 0;
}


static int microsemi_flashpro_execute_reset(struct reset_command *cmd)
{
#ifdef MICROSEMI_FLASHPRO_DEBUG
    LOG_INFO("%s trst=%d", __FUNCTION__, cmd->trst);
#endif     
    binn *request = binn_list();
    binn *response;
    microsemi_fp_request delay_type = serialize_reset_command(request, cmd);
    if (microsemi_socket_send(request, &response, delay_type)) {
        LOG_ERROR("fpClient, call 'execute_reset' to fpServer expired.");
        return 1;
    }
    int response_code = parse_response_basic(response); // will free the response handler
    //printf("Got response back with code %d \n", response_code); 
    return 0;
}


static int microsemi_flashpro_execute_pathmove(struct pathmove_command *cmd)
{
#ifdef MICROSEMI_FLASHPRO_DEBUG    
    LOG_INFO("%s num_states=%d", __FUNCTION__, cmd->num_states);
#endif
    binn *request = binn_list();
    binn *response;
    microsemi_fp_request delay_type = serialize_pathmove(request, cmd);
    if (microsemi_socket_send(request, &response, delay_type)) {
        LOG_ERROR("fpClient, call 'execute_pathmove' to fpServer expired.");
        return 1;
    }
    int response_code = parse_response_basic(response); // will free the response handler
    //printf("Got response back with code %d \n", response_code); 
    return 0;
}


static int microsemi_flashpro_execute_sleep(struct sleep_command *cmd)
{
#ifdef MICROSEMI_FLASHPRO_DEBUG 
    LOG_INFO("%s us=%d", __FUNCTION__, cmd->us);
#endif
    binn *request = binn_list();
    binn *response;
    microsemi_fp_request delay_type = serialize_sleep_command(request, cmd);
    if (microsemi_socket_send(request, &response, delay_type)) {
        LOG_ERROR("fpClient, call 'execute_sleep' to fpServer expired.");
        return 1;
    }
    int response_code = parse_response_basic(response); // will free the response handler
    //printf("Got response back with code %d \n", response_code); 
    return 0;
}


static int microsemi_flashpro_execute_command(struct jtag_command *cmd)
{
    switch (cmd->type) 
    {
        case JTAG_SCAN: 
        {
            return microsemi_flashpro_execute_scan(cmd->cmd.scan);
        }

        case JTAG_TLR_RESET: 
        {
            return microsemi_flashpro_execute_statemove(cmd->cmd.statemove);
        }

        case JTAG_RUNTEST: 
        {
            return microsemi_flashpro_execute_runtest(cmd->cmd.runtest);
        }

        case JTAG_RESET: 
        {
            return microsemi_flashpro_execute_reset(cmd->cmd.reset);
        }        
        
        case JTAG_PATHMOVE: 
        {
            return microsemi_flashpro_execute_pathmove(cmd->cmd.pathmove);
        }

        case JTAG_SLEEP: 
        {
            return microsemi_flashpro_execute_sleep(cmd->cmd.sleep);
        }
        
        default: 
        {
            LOG_ERROR("Unknown JTAG command type encountered: %d", cmd->type);
            break;
        }
    }
    return 0;
}


static int microsemi_flashpro_execute_queue(void)
{
    for (struct jtag_command *cmd = jtag_command_queue; cmd; cmd = cmd->next) 
    {
        if (microsemi_flashpro_execute_command(cmd)) {
            // if any given of commands returned error, ignore the whole queue and return a error
            return ERROR_COMMAND_CLOSE_CONNECTION;
        }
    }

    // return OK only if all listed commands executed without issue
    return ERROR_OK;
}


static int microsemi_flashpro_initialize(void)
{
#ifdef MICROSEMI_FLASHPRO_DEBUG
    LOG_INFO("%s start", __FUNCTION__);
#endif
    binn *request = binn_list();
    binn *response;
    microsemi_fp_request delay_type = serialize_init_request(request);
    // printf("Delay type for init %d \n", (int)delay_type);
    if (microsemi_socket_send(request, &response, delay_type)) {
        LOG_ERROR("fpClient, call 'flashpro_initialize' to fpServer expired.");
        return ERROR_COMMAND_CLOSE_CONNECTION;
    }
    else {
        return parse_response_basic(response);
    }
}


static int microsemi_flashpro_quit(void)
{
#ifdef MICROSEMI_FLASHPRO_DEBUG
    LOG_INFO("%s", __FUNCTION__);
#endif
    binn *request = binn_list();
    binn *response;
    microsemi_fp_request delay_type =  serialize_quit_request(request);
    if (microsemi_socket_send(request, &response, delay_type)) {
        LOG_ERROR("fpClient, call 'flashpro_quit' to fpServer expired.");
        return ERROR_COMMAND_CLOSE_CONNECTION;
    }
    else {
        //printf("Got response back with code %d \n", parse_response_basic(response));
        microsemi_socket_close();
        return parse_response_basic(response);
    }
}



/* ------------------ FlashPro custom OpenOCD commands ------------------------------------*/



COMMAND_HANDLER(handle_microsemi_flashpro_port_command)
{
#ifdef MICROSEMI_FLASHPRO_DEBUG
    LOG_INFO("%s", __FUNCTION__);
#endif

    if (CMD_ARGC != 1) 
    {
        LOG_ERROR("Single argument specifying FlashPro port expected");
        return ERROR_COMMAND_SYNTAX_ERROR;
    }

    binn *request = binn_list();
    binn *response;
    microsemi_fp_request delay_type = serialize_set_usb_port(request, CMD_ARGV[0]);
    if (microsemi_socket_send(request, &response, delay_type)) {
        LOG_ERROR("fpClient, call 'flashpro_port_command' to fpServer expired.");
        return ERROR_COMMAND_CLOSE_CONNECTION;
    }
    else {
        //printf("Got response back with code %d \n", parse_response_basic(response));
        return parse_response_basic(response);
    }
}


COMMAND_HANDLER(handle_microsemi_fpserver_binary_command)
{
#ifdef MICROSEMI_FLASHPRO_DEBUG
    LOG_INFO("%s", __FUNCTION__);
#endif

    if (CMD_ARGC != 1) 
    {
        LOG_ERROR("Single argument specifying path to FlashPro server binary (max 512 chars) expected");
        return ERROR_COMMAND_SYNTAX_ERROR;
    }
    
    if (microsemi_socket_set_server_path(CMD_ARGV[0])) {
        return ERROR_COMMAND_SYNTAX_ERROR;
    }

    return ERROR_OK;
}


COMMAND_HANDLER(handle_microsemi_fpserver_ip_command)
{
#ifdef MICROSEMI_FLASHPRO_DEBUG
    LOG_INFO("%s", __FUNCTION__);
#endif

    if (CMD_ARGC != 1) 
    {
        LOG_ERROR("Single argument specifying IPv4 address expected");
        return ERROR_COMMAND_SYNTAX_ERROR;
    }
    
    if (microsemi_socket_set_ipv4((char *)CMD_ARGV[0])) {
        return ERROR_COMMAND_SYNTAX_ERROR;
    }

    return ERROR_OK;
}


COMMAND_HANDLER(handle_microsemi_fpserver_port_command)
{
#ifdef MICROSEMI_FLASHPRO_DEBUG
    LOG_INFO("%s", __FUNCTION__);
#endif

    if (CMD_ARGC != 1) 
    {
        LOG_ERROR("Single argument specifying port number expected");
        return ERROR_COMMAND_SYNTAX_ERROR;
    }
    
    if (microsemi_socket_set_port(atoi(CMD_ARGV[0]))) {
        return ERROR_COMMAND_SYNTAX_ERROR;
    }

    return ERROR_OK;
}


COMMAND_HANDLER(handle_microsemi_fpserver_autostart_command)
{
#ifdef MICROSEMI_FLASHPRO_DEBUG
    LOG_INFO("%s", __FUNCTION__);
#endif

    if (CMD_ARGC != 1) 
    {
        LOG_ERROR("Single boolean argument specifying autostart state expected");
        return ERROR_COMMAND_SYNTAX_ERROR;
    }
    
    const Jim_Nvp* n = Jim_Nvp_name2value_simple(jim_nvp_boolean_options, CMD_ARGV[0]);
    if (n->name == NULL) {
        return ERROR_COMMAND_SYNTAX_ERROR;
    }

    microsemi_server_set_autostart(n->value);

    return ERROR_OK;
}


COMMAND_HANDLER(handle_microsemi_fpserver_autokill_command)
{
#ifdef MICROSEMI_FLASHPRO_DEBUG
    LOG_INFO("%s", __FUNCTION__);
#endif
    if (CMD_ARGC != 1) 
    {
        LOG_ERROR("Single boolean argument specifying autokill state expected");
        return ERROR_COMMAND_SYNTAX_ERROR;
    }

    const Jim_Nvp* n = Jim_Nvp_name2value_simple(jim_nvp_boolean_options, CMD_ARGV[0]);
    if (n->name == NULL) {
        return ERROR_COMMAND_SYNTAX_ERROR;
    }
    
    microsemi_server_set_autokill(n->value);

    return ERROR_OK;
}


COMMAND_HANDLER(handle_microsemi_flashpro_tunnel_jtag_via_ujtag_command)
{
#ifdef MICROSEMI_FLASHPRO_DEBUG
    LOG_INFO("%s", __FUNCTION__);
#endif
    if (CMD_ARGC != 1) {
        LOG_ERROR("Single boolean argument specifying JTAG tunnel state expected expected");
        return ERROR_COMMAND_SYNTAX_ERROR;
    }

    const Jim_Nvp* n = Jim_Nvp_name2value_simple(jim_nvp_boolean_options, CMD_ARGV[0]);
    if (n->name == NULL) {
            return ERROR_COMMAND_SYNTAX_ERROR;
    }

    binn *request = binn_list();
    binn *response;
    microsemi_fp_request delay_type =  serialize_ujtag_set(request, n->value);

    // printf("Delay type for ujtag %d \n", (int)delay_type);

    if (microsemi_socket_send(request, &response, delay_type)) {
        LOG_ERROR("fpClient, call 'flashpro_tunnel_jtag_via_ujtag' to fpServer expired.");
        return ERROR_COMMAND_CLOSE_CONNECTION;
    }
    else {
        //printf("Got response back with code %d \n", parse_response_basic(response));
        return parse_response_basic(response);
    }
}


COMMAND_HANDLER(handle_microsemi_flashpro_logging_command)
{
	LOG_INFO("%s", __FUNCTION__);

	if (CMD_ARGC != 1) 
	{
		LOG_ERROR("Single boolean argument specifying logging state expected");
		return ERROR_COMMAND_SYNTAX_ERROR;
	}

	const Jim_Nvp* n = Jim_Nvp_name2value_simple(jim_nvp_boolean_options, CMD_ARGV[0]);
	if (n->name == NULL)
	{
		return ERROR_COMMAND_SYNTAX_ERROR;
	}

    binn *request = binn_list();
    binn *response;
    microsemi_fp_request delay_type = serialize_logging(request, n->value);

    // printf("Delay type for ujtag %d \n", (int)delay_type);

    if (microsemi_socket_send(request, &response, delay_type)) {
        LOG_ERROR("fpClient: call 'flashpro_logging' to fpServer expired.");
        return ERROR_COMMAND_CLOSE_CONNECTION;
    }
    else {
    	command_print(CMD_CTX, "microsemi_flashpro logging %s", n->name);
        //printf("Got response back with code %d \n", parse_response_basic(response));
        return parse_response_basic(response);
    }
}


COMMAND_HANDLER(handle_microsemi_fpserver_file_logging_command)
{
	LOG_INFO("%s", __FUNCTION__);

	if (CMD_ARGC != 1) 
	{
		LOG_ERROR("Single boolean argument specifying logging state expected");
		return ERROR_COMMAND_SYNTAX_ERROR;
	}

	const Jim_Nvp* n = Jim_Nvp_name2value_simple(jim_nvp_boolean_options, CMD_ARGV[0]);
	if (n->name == NULL)
	{
		return ERROR_COMMAND_SYNTAX_ERROR;
	}

    binn *request = binn_list();
    binn *response;
    microsemi_fp_request delay_type =  serialize_server_file_logging(request, n->value);

    printf("Delay type for ujtag %d \n", (int)delay_type);

    if (microsemi_socket_send(request, &response, delay_type)) {
        LOG_ERROR("fpClient: call 'microsemi_fpserver_file_logging' to fpServer expired.");
        return ERROR_COMMAND_CLOSE_CONNECTION;
    }
    else {
    	command_print(CMD_CTX, "microsemi_fpserver_file_logging %s", n->name);
        //printf("Got response back with code %d \n", parse_response_basic(response));
        return parse_response_basic(response);
    }
}


static const struct command_registration microsemi_flashpro_exec_command_handlers[] = 
{
    {
        .name =    "port",
        .handler = handle_microsemi_flashpro_port_command,
        .mode =    COMMAND_CONFIG,
        .help =    "identify a specific FlashPro port to be used",
        .usage =   "<flashpro-port-name> e.g. usb71682 (FlashPro3/4/LCPS), S200XTYRZ3 (FlashPro5) etc.",
    },
    {
        .name =    "fpserver_binary",
        .handler = handle_microsemi_fpserver_binary_command,
        .mode =    COMMAND_CONFIG,
        .help =    "path to the fpServer binary",
        .usage =   "<path> defaults to \"fpServer\"",
    },
    {
        .name =    "fpserver_ip",
        .handler = handle_microsemi_fpserver_ip_command,
        .mode =    COMMAND_CONFIG,
        .help =    "IPv4 address to the fpServer, defaults to 127.0.0.1",
        .usage =   "<ip-v4-address>",
    },
    {
        .name =    "fpserver_port",
        .handler = handle_microsemi_fpserver_port_command,
        .mode =    COMMAND_CONFIG,
        .help =    "identify a specific TCP fpserver_port to be used, defaults to 3334",
        .usage =   "<port>",
    },
    {
        .name =    "fpserver_autostart",
        .handler = handle_microsemi_fpserver_autostart_command,
        .mode =    COMMAND_CONFIG,
        .help =    "autostart fpserver with openocd, default off",
        .usage =   jim_nvp_boolean_options_description,
    },
    {
        .name =    "fpserver_autokill",
        .handler = handle_microsemi_fpserver_autokill_command,
        .mode =    COMMAND_CONFIG,
        .help =    "autokill fpserver which is running at the same port, default off",
        .usage =   jim_nvp_boolean_options_description,
    },
	{
		.name =    "fpserver_file_logging",
		.handler = handle_microsemi_fpserver_file_logging_command,
		.mode =    COMMAND_ANY,
		.help =    "control whether fpServer's API and timeouts file logging is on or not",
		.usage =   jim_nvp_boolean_options_description,
	},    
    {
        .name =    "tunnel_jtag_via_ujtag",
        .handler = handle_microsemi_flashpro_tunnel_jtag_via_ujtag_command,
        .mode =    COMMAND_ANY,
        .help =    "control whether or not JTAG traffic is \"tunnelled\" via UJTAG",
        .usage =   jim_nvp_boolean_options_description,
    },
	{
		.name =    "logging",
		.handler = handle_microsemi_flashpro_logging_command,
		.mode =    COMMAND_ANY,
		.help =    "control whether or not logging is on",
		.usage =   jim_nvp_boolean_options_description,
	},    
    COMMAND_REGISTRATION_DONE
};


static const struct command_registration microsemi_flashpro_command_handlers[] = 
{
    {
        .name =  "microsemi_flashpro",
        .mode =  COMMAND_EXEC,
        .help =  "Microsemi FlashPro command group",
        .usage = "",
        .chain = microsemi_flashpro_exec_command_handlers,
    },
    {
        .name =    "microsemi_flashpro_port",
        .handler = handle_microsemi_flashpro_port_command,
        .mode =    COMMAND_CONFIG,
        .help =    "identify a specific FlashPro port to be used",
        .usage =   "<flashpro-port-name> e.g. usb71682 (FlashPro3/4/LCPS), S200XTYRZ3 (FlashPro5) etc.",
    },
    COMMAND_REGISTRATION_DONE
};


struct jtag_interface microsemi_flashpro_interface = 
{
    .name =           "microsemi-flashpro",
    .supported =      0,    /* Don't support DEBUG_CAP_TMS_SEQ */
    .commands =       microsemi_flashpro_command_handlers,
    .transports =     jtag_only,
    .init =           microsemi_flashpro_initialize,
    .quit =           microsemi_flashpro_quit,
    .speed =          microsemi_flashpro_speed,
    .speed_div =      microsemi_flashpro_speed_div,
    .khz =            microsemi_flashpro_khz,
    .execute_queue =  microsemi_flashpro_execute_queue,
};
