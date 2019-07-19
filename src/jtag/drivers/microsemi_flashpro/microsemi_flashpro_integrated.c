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
  
 /*
 * Microsemi FlashPro JTAG driver (via FpcommWrapper API/DLL) for OpenOCD
 * http://www.microsemi.com/products/fpga-soc/design-resources/programming/flashpro
 */

/* TODO: implement --enable-microsemi-flashpro config option. At the moment
 * Microsemi FlashPro support is enabled of --enable-ftdi is specified.
 * See also interfaces.c/h
 */
 
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* For struct jtag_interface */
#include <jtag/interface.h>

/* FlashPro FpcommWrapper library header */
#include "fpcommwrapper/include/FpcommWrapper.h"

/* Useful defines */
#define HZ_PER_KHZ						1000		/* 1KHz = 1000Hz! :-) */
#define AFTER_SCAN_GOTO_IDLE			0			/* See JtagDrScan()/JtagIrScan() */
#define AFTER_SCAN_GOTO_PAUSE			1			/* See JtagDrScan()/JtagIrScan() */
#define FLASHPRO_EXECUTE_IMMEDIATELY	1			/* See JtagDelay() */
#define FLASHPRO_ENABLE_PORT			1			/* See EnableProgrammingPort() */

/* Support for tunnelling JTAG via UJTAG/uj_jtag */
#define ENTRY_LEN_NUM_BITS				3
#define SHIFT_LEN_NUM_BITS				6
#define EXIT_LEN_NUM_BITS				3
#define ENTRY_MAX_LEN					6
#define EXIT_MAX_LEN					7
#define SELECT_UJTAG_SLAVE				0x33
#define MAX_SCAN_CHUNK_BITS				56 
#define TAP_RESET_TMS_PATH				0x1f
#define TAP_RESET_TMS_PATHLEN			5

/* Function declarations */

/* Interface functions - called from outside via struct jtag_interface */
static int microsemi_flashpro_initialize(void);
static int microsemi_flashpro_quit(void);
static int microsemi_flashpro_speed(int speed);
static int microsemi_flashpro_speed_div(int speed, int *khz);
static int microsemi_flashpro_khz(int khz, int *jtag_speed);
static int microsemi_flashpro_execute_queue(void);

/* Private implementation functions */
static JtagState_t openocd_to_flashpro_tap_state(tap_state_t openocd_tap_state);
static void microsemi_flashpro_execute_command(struct jtag_command *cmd);
static void microsemi_flashpro_execute_pathmove(struct jtag_command *cmd);
static void microsemi_flashpro_execute_runtest(struct jtag_command *cmd);
static void microsemi_flashpro_execute_reset(struct jtag_command *cmd);
static void microsemi_flashpro_execute_scan(struct jtag_command *cmd);
static void microsemi_flashpro_execute_sleep(struct jtag_command *cmd);
static void microsemi_flashpro_set_tap_state(tap_state_t openocd_end_state);
static void microsemi_flashpro_execute_statemove(struct jtag_command *cmd);
static bool microsemi_flashpro_scan_field(bool f_irscan, struct scan_field *p_scan_field);

/* Support for tunnelling JTAG via UJTAG/uj_jtag */
static void microsemi_flashpro_ujtag_execute_pathmove(struct jtag_command *cmd);
static void microsemi_flashpro_ujtag_execute_reset(struct jtag_command *cmd);
static void microsemi_flashpro_ujtag_execute_runtest(struct jtag_command *cmd);
static void microsemi_flashpro_ujtag_set_tap_state(tap_state_t openocd_end_state);
static void microsemi_flashpro_ujtag_execute_statemove(struct jtag_command *cmd);
static bool microsemi_flashpro_scan_field_tunnelled(bool f_irscan, struct scan_field *p_scan_field);

static tap_state_t microsemi_flashpro_ujtag_tap_next_state(
	uint8_t *ptms_path, 
	uint8_t *ptms_pathlen, 
	tap_state_t current_state, 
	uint8_t tms_bit);
static uint8_t microsemi_flashpro_ujtag_tms_path(
	tap_state_t from_state, 
	tap_state_t to_state, 
	uint8_t *ptms_pathlen);

/* 
 * Static globals 
 */
 
/* FlashPro related info */
  typedef struct 
 {
	PrgHdl_t	handle;
	char		sz_port[MAX_BUF_SIZE];
	bool		f_tunnel_jtag_via_ujtag;
	uint8_t		c_leading_bypassed_taps;
	uint8_t		c_trailing_bypassed_taps;
	tap_state_t ujtag_current_state;
	bool		f_logging;
	PrgInfo_t	info;
 } flashpro_descriptor_t;
 
 static flashpro_descriptor_t sg_flashpro = 
 { 
	/* handle */
	NULL, 
	/* sz_port */
	"", 
	/* f_tunnel_jtag_via_ujtag */
	false, 
	/* c_leading_bypassed_taps */
	0,
	/* c_trailing_bypassed_taps */
	0,
	/* ujtag_current_state */
	TAP_RESET,
	/* f_logging */
	false, 
	/* st_info */
	{ 
		/* type */
		"", 
		/* revision */
		"", 
		/* connectionType */
		"", 
		/* id */
		"" 
	},
};
 
/* Support for tunnelling JTAG via UJTAG/uj_jtag for soft CPU cores in the FPGA
 * fabric possibly connected via CoreJTAGDebug - e.g. Cortex-M1, Mi-V RISC-V
 * 
 * Utility functions for calculating TMS bit sequence and length for moving
 * from one TAP state to another. These are needed for management of the
 * UJTAG/uj_jtag inferior/slave device TAP.
 */

/* Given a current TAP state and TMS bit return the next state and add TMS
 * bit to accumulated tms_path/len
 */
static tap_state_t microsemi_flashpro_ujtag_tap_next_state(
	uint8_t *ptms_path, 
	uint8_t *ptms_pathlen, 
	tap_state_t current_state, 
	uint8_t tms_bit)
{
	tap_state_t next_state;

	/* Max path len catered for is 8 bits. If accumulated path exceeds this
	 * then something has gone seriously wrong! Doesn't happen in practice but
	 * check is here for completeness.
	 */
	if ((*ptms_pathlen) >= 8) 
	{
		LOG_ERROR("TMS pathlen cannot exceed 8");
		exit(-1);
	}

	/* What is the next state given the current state and TMS bit? */
	switch (current_state) 
	{
		case TAP_DREXIT2: 
		{
			next_state = tms_bit ? TAP_DRUPDATE : TAP_DRSHIFT;
			break;
		}
		
		case TAP_DREXIT1: 
		{
			next_state = tms_bit ? TAP_DRUPDATE : TAP_DRPAUSE;
			break;
		}
		
		case TAP_DRSHIFT: 
		{
			next_state = tms_bit ? TAP_DREXIT1 : TAP_DRSHIFT;
			break;
		}
		
		case TAP_DRPAUSE: 
		{
			next_state = tms_bit ? TAP_DREXIT2 : TAP_DRPAUSE;
			break;
		}
		
		case TAP_IRSELECT: 
		{
			next_state = tms_bit ? TAP_RESET : TAP_IRCAPTURE;
			break;
		}
		
		case TAP_DRUPDATE: 
		{
			next_state = tms_bit ? TAP_DRSELECT : TAP_IDLE;
			break;
		}
		
		case TAP_DRCAPTURE: 
		{
			next_state = tms_bit ? TAP_DREXIT1 : TAP_DRSHIFT;
			break;
		}
		
		case TAP_DRSELECT: 
		{
			next_state = tms_bit ? TAP_IRSELECT : TAP_DRCAPTURE;
			break;
		}
		
		case TAP_IREXIT2: 
		{
			next_state = tms_bit ? TAP_IRUPDATE : TAP_IRSHIFT;
			break;
		}
		
		case TAP_IREXIT1: 
		{
			next_state = tms_bit ? TAP_IRUPDATE : TAP_IRPAUSE;
			break;
		}
		
		case TAP_IRSHIFT: 
		{
			next_state = tms_bit ? TAP_IREXIT1 : TAP_IRSHIFT;
			break;
		}
		
		case TAP_IRPAUSE: 
		{
			next_state = tms_bit ? TAP_IREXIT2 : TAP_IRPAUSE;
			break;
		}
		
		case TAP_IDLE: 
		{
			next_state = tms_bit ? TAP_DRSELECT : TAP_IDLE;
			break;
		}
		
		case TAP_IRUPDATE: 
		{
			next_state = tms_bit ? TAP_DRSELECT : TAP_IDLE;
			break;
		}
		
		case TAP_IRCAPTURE: 
		{
			next_state = tms_bit ? TAP_IREXIT1 : TAP_IRSHIFT;
			break;
		}
		
		case TAP_RESET: 
		{
			next_state = tms_bit ? TAP_RESET : TAP_IDLE;
			break;
		}
		
		/* Can't happen but default case here for completeness */
		default: 
		{
			LOG_ERROR("Unexpected TAP state %s", tap_state_name(current_state));
			exit(-1);
			break;
		}
	}		

	/* Update path and len and return next state */
	(*ptms_path) |= (tms_bit << (*ptms_pathlen));
	(*ptms_pathlen)++;
	return next_state;
}

/* TAP state machine regions */
typedef enum tap_region 
{
	TAP_REGION_TLR,
	TAP_REGION_RTI,
	TAP_REGION_DR,
	TAP_REGION_IR
} tap_region_t;

/* Return TAP state machine region for specified TAP state */
static tap_region_t microsemi_flashpro_ujtag_tap_get_region(tap_state_t state)
{
	tap_region_t region = TAP_REGION_TLR;
	
	switch (state) 
	{
		case TAP_RESET: 
		{
			region = TAP_REGION_TLR;
			break;
		}
		
		case TAP_IDLE: 
		{
			region = TAP_REGION_RTI;
			break;
		}
		
		case TAP_DRSELECT:
		case TAP_DRCAPTURE:
		case TAP_DRSHIFT:
		case TAP_DREXIT1:
		case TAP_DRPAUSE:
		case TAP_DREXIT2:
		case TAP_DRUPDATE: 
		{
			region = TAP_REGION_DR;
			break;
		}
		
		case TAP_IRSELECT:
		case TAP_IRCAPTURE:
		case TAP_IRSHIFT:
		case TAP_IREXIT1:
		case TAP_IRPAUSE:
		case TAP_IREXIT2:
		case TAP_IRUPDATE: 
		{
			region = TAP_REGION_IR;
			break;
		}
		
		/* Can't happen but default case here for completeness */
		default: 
		{
			LOG_ERROR("Unexpected TAP state %s", tap_state_name(state));
			exit(-1);
			break;
		}
	}
	
	return region;
}

/* Calculate TMS bit sequence and length required to transition from one
 * state to another state 
 */
static uint8_t microsemi_flashpro_ujtag_tms_path(
	tap_state_t from_state, 
	tap_state_t to_state, 
	uint8_t *ptms_pathlen)
{
	uint8_t tms_path = 0;
	*ptms_pathlen = 0;

	while (from_state != to_state)
	{
		tap_region_t from_region = microsemi_flashpro_ujtag_tap_get_region(from_state);
		tap_region_t to_region = microsemi_flashpro_ujtag_tap_get_region(to_state);
		uint8_t tms_bit = 0;

		if (from_region != to_region) 
		{
			/* First move to the appropriate region */
			switch (from_region)
			{
				case TAP_REGION_TLR: 
				{
					tms_bit = 0;
					break;
				}
				
				case TAP_REGION_RTI: 
				{
					tms_bit = 1;
					break;
				}
				
				case TAP_REGION_DR: 
				{
					tms_bit = ((TAP_DRUPDATE == from_state) && (TAP_IDLE == to_state)) ? 0 : 1;
					break;
				}
				
				case TAP_REGION_IR: 
				{
					tms_bit = ((TAP_IRUPDATE == from_state) && (TAP_IDLE == to_state)) ? 0 : 1;
					break;
				}
			}
		} 
		else 
		{
			/* Now move towards the required state */
			switch (from_region)
			{
				case TAP_REGION_DR: 
				{
					switch (to_state) 
					{
						case TAP_DRSELECT: 
						{
							tms_bit = 1;
							break;
						}
						
						case TAP_DRCAPTURE: 
						{
							tms_bit = (TAP_DRSELECT == from_state) ? 0 : 1;
							break;
						}
						
						case TAP_DRSHIFT: 
						{
							tms_bit = ((TAP_DRSELECT == from_state) || (TAP_DRCAPTURE == from_state) || (TAP_DREXIT2 == from_state)) ? 0 : 1;
							break;
						}
						
						case TAP_DRPAUSE: 
						{
							tms_bit = ((TAP_DRSELECT == from_state) || (TAP_DREXIT1 == from_state)) ? 0 : 1;
							break;
						}
						
						case TAP_DRUPDATE: 
						{
							tms_bit = (TAP_DRSELECT == from_state) ? 0 : 1;
							break;
						}
						
						default: 
						{
							LOG_ERROR("Unexpected TAP state %s", tap_state_name(to_state));
							exit(-1);
							break;
						}
					}
					break;
				}
				
				case TAP_REGION_IR: 
				{
					switch (to_state) 
					{
						case TAP_IRSELECT: 
						{
							tms_bit = 1;
							break;
						}
						
						case TAP_IRCAPTURE: 
						{
							tms_bit = (TAP_IRSELECT == from_state) ? 0 : 1;
							break;
						}
						
						case TAP_IRSHIFT: 
						{
							tms_bit = ((TAP_IRSELECT == from_state) || (TAP_IRCAPTURE == from_state) || (TAP_IREXIT2 == from_state)) ? 0 : 1;
							break;
						}
						
						case TAP_IRPAUSE: 
						{
							tms_bit = ((TAP_IRSELECT == from_state) || (TAP_IREXIT1 == from_state)) ? 0 : 1;
							break;
						}
						
						case TAP_IRUPDATE: 
						{
							tms_bit = (TAP_IRSELECT == from_state) ? 0 : 1;
							break;
						}
						
						default: 
						{
							LOG_ERROR("Unexpected TAP state %s", tap_state_name(to_state));
							exit(-1);
							break;
						}
					}
					break;
				}
				
				default: 
				{
					LOG_ERROR("Unexpected TAP region %d", from_region);
					exit(-1);
					break;
				}
			}
		}
		
		/* Record this single state transition */
		from_state = microsemi_flashpro_ujtag_tap_next_state(
			&tms_path, ptms_pathlen, from_state, tms_bit);
	}

	return tms_path;
}

/* Translate OpenOCD tap_state_t to FlashPro FpcommWrapper JtagState_t */
static JtagState_t openocd_to_flashpro_tap_state(tap_state_t openocd_tap_state)
{
	JtagState_t flashpro_tap_state = enUndefState;
	 
	switch(openocd_tap_state) 
	{
		case TAP_RESET: 
		{
			flashpro_tap_state = enReset;
			break;
		}
		
		case TAP_IDLE: 
		{
			flashpro_tap_state = enIdle;
			break;
		}
		
		case TAP_IRPAUSE: 
		{
			flashpro_tap_state = enIrPause;
			break;
		}
		
		case TAP_DRPAUSE: 
		{
			flashpro_tap_state = enDrPause;
			break;
		}
		
		case TAP_DRSELECT: 
		{
			flashpro_tap_state = enDrSelect;
			break;
		}
		
		case TAP_DRCAPTURE: 
		{
			flashpro_tap_state = enDrCapture;
			break;
		}
		
		case TAP_DRSHIFT: 
		{
			flashpro_tap_state = enDrShift;
			break;
		}
		
		case TAP_DREXIT1: 
		{
			flashpro_tap_state = enDrExit1;
			break;
		}
		
		case TAP_DREXIT2: 
		{
			flashpro_tap_state = enDrExit2;
			break;
		}
		
		case TAP_DRUPDATE: 
		{
			flashpro_tap_state = enDrUpdate;
			break;
		}
		
		case TAP_IRSELECT: 
		{
			flashpro_tap_state = enIrSelect;
			break;
		}
		
		case TAP_IRCAPTURE: 
		{
			flashpro_tap_state = enIrCapture;
			break;
		}
		
		case TAP_IRSHIFT: 
		{
			flashpro_tap_state = enIrShift;
			break;
		}
		
		case TAP_IREXIT1: 
		{
			flashpro_tap_state = enIrExit1;
			break;
		}
		
		case TAP_IREXIT2: 
		{
			flashpro_tap_state = enIrExit2;
			break;
		}
		
		case TAP_IRUPDATE: 
		{
			flashpro_tap_state = enIrUpdate;
			break;
		}
		
		case TAP_INVALID:
		default: 
		{
			flashpro_tap_state = enUndefState;
			break;
		}
	}

	return flashpro_tap_state;
}

static int microsemi_flashpro_speed(int speed)
{
	if (sg_flashpro.f_logging)
	{
		LOG_INFO("%s(%d)", __FUNCTION__, speed);
	}

	int retval = ERROR_OK;
	
	/* FlashPro supports the following speeds:
	 *
	 * FlashPro3: 1MHz, 2MHz, 3MHz, 4MHz, 6MHz
	 * FlashPro4: 1MHz, 2MHz, 3MHz, 4MHz, 5MHz, 6MHz
	 * FlashPro5: 458Hz to 30MHz where speed = 30MHz/(1+divisor) and divisor is
	 *			  0 to 65535. If the requested speed cannot be used then the 
	 *			  closest valid speed is used instead. Note that OpenOCD only
	 *			  allows speeds to be specified in kHz so the effective lowest 
	 *			  speed is 1000Hz/1kHz.
	 */
	 
	if (strcmp(sg_flashpro.info.type, "FlashPro3") == 0)
	{
		if ((1000000 != speed) &&
			(2000000 != speed) &&
			(3000000 != speed) &&
			(4000000 != speed) &&
			(6000000 != speed))
		{	
			LOG_ERROR("Invalid speed %d kHz specified - FlashPro3 speed must be one of 1 MHz, 2 MHz, 3 MHz, 4 MHz or 6 MHz specified in kHz", speed / HZ_PER_KHZ);
			retval = ERROR_JTAG_DEVICE_ERROR;
		}
	}
	else if (strcmp(sg_flashpro.info.type, "FlashPro4") == 0)
	{
		if ((1000000 != speed) &&
			(2000000 != speed) &&
			(3000000 != speed) &&
			(4000000 != speed) &&
			(5000000 != speed) &&
			(6000000 != speed))
		{	
			LOG_ERROR("Invalid speed %d kHz specified - FlashPro4 speed must be one of 1 MHz, 2 MHz, 3 MHz, 4 MHz, 5 MHz or 6 MHz specified in kHz", speed / HZ_PER_KHZ);
			retval = ERROR_JTAG_DEVICE_ERROR;
		}
	}
	else if (strcmp(sg_flashpro.info.type, "FlashPro5") == 0)
	{
		if ((speed < 1000) || (speed > 30000000)) 
		{
			LOG_ERROR("Invalid speed %d kHz specified - FlashPro5 speed must be between 1 kHz and 3 MHz specified in kHz", speed / HZ_PER_KHZ);
			retval = ERROR_JTAG_DEVICE_ERROR;
		}
	}
	else
	{
		LOG_ERROR("%s is not supported", sg_flashpro.info.type);
		retval = ERROR_JTAG_DEVICE_ERROR;
	}

	if (ERROR_OK == retval)
	{
		/* Ask FlashPro to use the specified speed */
		if (JtagSetTckFrequency(sg_flashpro.handle, (unsigned int)speed) != PRGSTAT_OK) 
		{
			LOG_ERROR("JtagSetTckFrequency(%d) failed : %s", 
				speed, GetErrorMessage(sg_flashpro.handle));
			retval = ERROR_JTAG_DEVICE_ERROR;
		}
	}
	
	return retval;
}

static int microsemi_flashpro_speed_div(int speed, int *khz)
{
	if (sg_flashpro.f_logging)
	{
		LOG_INFO("%s(%d)", __FUNCTION__, speed);
	}

	int retval = ERROR_OK;
	
	/* Check what speed actually used */
	unsigned int actual_speed;
	if (GetTckFrequency(sg_flashpro.handle, &actual_speed) != PRGSTAT_OK) 
	{
		LOG_ERROR("GetTckFrequency() failed : %s", GetErrorMessage(sg_flashpro.handle));
		retval = ERROR_JTAG_DEVICE_ERROR;
	}
	else
	{
		*khz = actual_speed / HZ_PER_KHZ;
	}
	
	return retval;
}

static int microsemi_flashpro_khz(int khz, int *jtag_speed)
{
	if (sg_flashpro.f_logging)
	{
		LOG_INFO("%s(%d)", __FUNCTION__, khz);
	}

	*jtag_speed = khz * HZ_PER_KHZ;
	return ERROR_OK;
}

static void microsemi_flashpro_execute_runtest(struct jtag_command *cmd)
{
	if (sg_flashpro.f_logging)
	{
		LOG_INFO("%s %d cycles", __FUNCTION__, cmd->cmd.runtest->num_cycles);
	}

	JtagState_t flashpro_current_state;
	
	/* Go to Run-Test Idle */
	if (JtagGetState(sg_flashpro.handle, &flashpro_current_state) != PRGSTAT_OK) 
	{
		LOG_ERROR("JtagGetState() failed : %s", GetErrorMessage(sg_flashpro.handle));
		exit(-1);
	}
	
	if (enIdle != flashpro_current_state) 
	{
		if (JtagSetState(sg_flashpro.handle, enIdle) != PRGSTAT_OK) 
		{
			LOG_ERROR("JtagSetState() failed : %s", 
				GetErrorMessage(sg_flashpro.handle));
			exit(-1);
		}
	}

	/* Stay in run-test-idle for cmd->cmd.runtest->num_cycles */
	if (JtagDelay(
		sg_flashpro.handle,				/* FlashPro programmer handle */
		cmd->cmd.runtest->num_cycles,	/* TCK tick count */
		0,								/* Sleep period */
		enWaitUnitsTCK,					/* Delay for cmd->cmd.runtest->num_cycles TCK ticks */
		FLASHPRO_EXECUTE_IMMEDIATELY	/* Execute immediately */
	) != PRGSTAT_OK) 
	{
		LOG_ERROR("JtagDelay() failed : %s", GetErrorMessage(sg_flashpro.handle));
		exit(-1);
	}

	/* Go to end state */
	microsemi_flashpro_set_tap_state(cmd->cmd.runtest->end_state);
}

/* Support for tunnelling JTAG via UJTAG/uj_jtag */
static void microsemi_flashpro_ujtag_execute_runtest(struct jtag_command *cmd)
{
	if (sg_flashpro.f_logging)
	{
		LOG_INFO("%s %d cycles", __FUNCTION__, cmd->cmd.runtest->num_cycles);
	}

	int tms_ticks;
	uint8_t tms_pathlen;
	
	/* Go to run-test-idle */
	microsemi_flashpro_ujtag_set_tap_state(TAP_IDLE);
	
	/* Stay in run-test-idle for cmd->cmd.runtest->num_cycles */
	for (tms_ticks = cmd->cmd.runtest->num_cycles; tms_ticks > 0; ) 
	{
		/* On first iteration deal with leading bypassed TAPs if any */
		if (tms_ticks == cmd->cmd.runtest->num_cycles)
		{
			/* Any leading bypassed TAPs in chain ... ? */
			if (0 != sg_flashpro.c_leading_bypassed_taps)
			{
				/* ... yes - so scan bits to account for them */
				if (JtagDrScanAllBits(sg_flashpro.handle, 
					sg_flashpro.c_leading_bypassed_taps, 0, 
					NULL, AFTER_SCAN_GOTO_PAUSE) != PRGSTAT_OK) 
				{
					LOG_ERROR("JtagDrScanAllBits() failed : %s", GetErrorMessage(sg_flashpro.handle));
					exit(-1);
				}
			}
		}
		
		/* 
		 * Entry phase: clock <= ENTRY_MAX_LEN x 0 bits on TMS
		 */
		tms_pathlen = (tms_ticks >= ENTRY_MAX_LEN) ? ENTRY_MAX_LEN : tms_ticks;
		tms_ticks -= tms_pathlen;

		if (JtagDrScan(sg_flashpro.handle, ENTRY_LEN_NUM_BITS, 
			(const char *)&tms_pathlen, 
			NULL, AFTER_SCAN_GOTO_PAUSE) != PRGSTAT_OK) 
		{
			LOG_ERROR("JtagDrScan() failed : %s", GetErrorMessage(sg_flashpro.handle));
			exit(-1);
		}
		
		if (JtagDrScanAllBits(sg_flashpro.handle, tms_pathlen, 0, 
			NULL, AFTER_SCAN_GOTO_PAUSE) != PRGSTAT_OK) 
		{
			LOG_ERROR("JtagDrScanAllBits() failed : %s", GetErrorMessage(sg_flashpro.handle));
			exit(-1);
		}
		
		/* 
		 * Shift phase: no data/length = 0 
		 */
		if (JtagDrScanAllBits(sg_flashpro.handle, SHIFT_LEN_NUM_BITS, 0, 
			NULL, AFTER_SCAN_GOTO_PAUSE) != PRGSTAT_OK) 
		{
			LOG_ERROR("JtagDrScanAllBits() failed : %s", GetErrorMessage(sg_flashpro.handle));
			exit(-1);
		}
		
		/* 
		 * Exit phase: clock <= EXIT_MAX_LEN x 0 bits on TMS 
		 */
		tms_pathlen = (tms_ticks >= EXIT_MAX_LEN) ? EXIT_MAX_LEN : tms_ticks;
		tms_ticks -= tms_pathlen;

		if (JtagDrScan(sg_flashpro.handle, ENTRY_LEN_NUM_BITS, 
			(const char *)&tms_pathlen, NULL, AFTER_SCAN_GOTO_PAUSE) != PRGSTAT_OK) 
		{
			LOG_ERROR("JtagDrScan() failed : %s", GetErrorMessage(sg_flashpro.handle));
			exit(-1);
		}
		
		if (tms_pathlen > 0) 
		{
			if (JtagDrScanAllBits(sg_flashpro.handle, tms_pathlen, 0, 
				NULL, AFTER_SCAN_GOTO_PAUSE) != PRGSTAT_OK) 
			{
				LOG_ERROR("JtagDrScanAllBits() failed : %s", GetErrorMessage(sg_flashpro.handle));
				exit(-1);
			}
		}

		/* On last iteration deal with trailing bypassed TAPs if any */
		if (0 >= tms_ticks)
		{
			/* Any leading bypassed TAPs in chain ... ? */
			if (0 != sg_flashpro.c_leading_bypassed_taps)
			{
				/* ... yes - so scan bits to account for them */
				if (JtagDrScanAllBits(sg_flashpro.handle, 
					sg_flashpro.c_leading_bypassed_taps, 0, 
					NULL, AFTER_SCAN_GOTO_PAUSE) != PRGSTAT_OK) 
				{
					LOG_ERROR("JtagDrScanAllBits() failed : %s", GetErrorMessage(sg_flashpro.handle));
					exit(-1);
				}
			}
		}
		
		/* 
		 * Finished: move FPGA TAP to run-test-idle
		 */
		if (JtagSetState(sg_flashpro.handle, enIdle) != PRGSTAT_OK) 
		{
			LOG_ERROR("JtagSetState() failed : %s", GetErrorMessage(sg_flashpro.handle));
			exit(-1);
		}	
	}

	/* Move to end state */
	microsemi_flashpro_ujtag_set_tap_state(cmd->cmd.runtest->end_state);
}

static void microsemi_flashpro_set_tap_state(tap_state_t openocd_end_state)
{
	if (sg_flashpro.f_logging)
	{
		LOG_INFO("%s state = %s", __FUNCTION__, tap_state_name(openocd_end_state));
	}

	JtagState_t flashpro_current_state;
	JtagState_t flashpro_end_state;
	
	if (JtagGetState(sg_flashpro.handle, &flashpro_current_state) != PRGSTAT_OK) 
	{
		LOG_ERROR("JtagGetState() failed : %s", GetErrorMessage(sg_flashpro.handle));
		exit(-1);
	}
	
	flashpro_end_state = openocd_to_flashpro_tap_state(openocd_end_state);
	
	if (flashpro_current_state != flashpro_end_state) 
	{
		if (JtagSetState(sg_flashpro.handle, flashpro_end_state) != PRGSTAT_OK) 
		{
			LOG_ERROR("JtagSetState() failed : %s", GetErrorMessage(sg_flashpro.handle));
			exit(-1);
		}
	}
}

/* Support for tunnelling JTAG via UJTAG/uj_jtag */
static void microsemi_flashpro_ujtag_set_tap_state(tap_state_t openocd_end_state)
{
	if (sg_flashpro.f_logging)
	{
		LOG_INFO("%s state = %s", __FUNCTION__, tap_state_name(openocd_end_state));
	}

	if ((TAP_RESET == openocd_end_state) ||
		(sg_flashpro.ujtag_current_state != openocd_end_state)) 
	{
		uint8_t tms_path;
		uint8_t tms_pathlen;
		
		/* 
		 * Entry phase and shift phase are empty 
		 */
		if (JtagDrScanAllBits(sg_flashpro.handle, 
			sg_flashpro.c_leading_bypassed_taps +		
			ENTRY_LEN_NUM_BITS + SHIFT_LEN_NUM_BITS, 
			0, NULL, AFTER_SCAN_GOTO_PAUSE) != PRGSTAT_OK) 
		{
			LOG_ERROR("JtagDrScanAllBits() failed : %s", GetErrorMessage(sg_flashpro.handle));
			exit(-1);
		}
			
		/* 
		 * Exit phase: reset or go from current state to end state 
		 */
		if (TAP_RESET == openocd_end_state) 
		{
			tms_path = TAP_RESET_TMS_PATH;
			tms_pathlen = TAP_RESET_TMS_PATHLEN;
		} 
		else 
		{
			tms_path = 
				microsemi_flashpro_ujtag_tms_path(
					sg_flashpro.ujtag_current_state, 
					openocd_end_state, &tms_pathlen);
		}
		
		if (JtagDrScan(sg_flashpro.handle, EXIT_LEN_NUM_BITS, (char *)&tms_pathlen, 
			NULL, AFTER_SCAN_GOTO_PAUSE) != PRGSTAT_OK) 
		{
			LOG_ERROR("JtagDrScan() failed : %s", GetErrorMessage(sg_flashpro.handle));
			exit(-1);
		}
		
		if (JtagDrScan(sg_flashpro.handle, tms_pathlen, (char *)&tms_path, 
			NULL, AFTER_SCAN_GOTO_PAUSE) != PRGSTAT_OK) 
		{
			LOG_ERROR("JtagDrScan() failed : %s", GetErrorMessage(sg_flashpro.handle));
			exit(-1);
		}

		/* Any trailing bypassed TAPs in chain ... ? */
		if (0 != sg_flashpro.c_trailing_bypassed_taps)
		{
			/* ... yes - so scan bits to account for them */
			if (JtagDrScanAllBits(sg_flashpro.handle, 
				sg_flashpro.c_trailing_bypassed_taps, 0, 
				NULL, AFTER_SCAN_GOTO_PAUSE) != PRGSTAT_OK) 
			{
				LOG_ERROR("JtagDrScanAllBits() failed : %s", GetErrorMessage(sg_flashpro.handle));
				exit(-1);
			}
		}

		/* 
		 * Finished: move FPGA TAP to run-test-idle
		 */
		if (JtagSetState(sg_flashpro.handle, enIdle) != PRGSTAT_OK) 
		{
			LOG_ERROR("JtagSetState() failed : %s", GetErrorMessage(sg_flashpro.handle));
			exit(-1);
		}	
			
		/* Update current state */
		sg_flashpro.ujtag_current_state = openocd_end_state;
	}
}

static void microsemi_flashpro_execute_statemove(struct jtag_command *cmd)
{	
	if (sg_flashpro.f_logging)
	{
		LOG_INFO("%s state = %s", __FUNCTION__, 
			tap_state_name(cmd->cmd.statemove->end_state));
	}

	microsemi_flashpro_set_tap_state(cmd->cmd.statemove->end_state);
}

/* Support for tunnelling JTAG via UJTAG/uj_jtag */
static void microsemi_flashpro_ujtag_execute_statemove(struct jtag_command *cmd)
{	
	if (sg_flashpro.f_logging)
	{
		LOG_INFO("%s state = %s", __FUNCTION__, 
			tap_state_name(cmd->cmd.statemove->end_state));
	}

	microsemi_flashpro_ujtag_set_tap_state(cmd->cmd.statemove->end_state);
}

static void print_scan_bits(char *pstr, int nbits, const uint8_t *pscanbits)
{
	int i = 0;	
	for (*pstr = '\0'; (i * 8) < nbits; i++)
	{
		sprintf((pstr + (i * 2)), "%02x", (NULL == pscanbits) ? 0 : *(pscanbits + i));
	}
}

static void print_jtag_chain(void)
{
	uint8_t c_leading;
	uint8_t c_trailing;
	struct jtag_tap *p_tap;
    
	// Count # of leading TAPs enabled and in bypass
	// Could use for loop but easier to read as follows?    
	c_leading = 0;	
	p_tap = jtag_tap_next_enabled(NULL);
	while ((NULL != p_tap) && (1 == p_tap->bypass))
	{
		c_leading++;
		p_tap = jtag_tap_next_enabled(p_tap);
	}

	// Should be at the single TAP not in bypass now?
	// ASSERT((NULL != p_tap) && (0 == p_tap->bypass));

	// Count # of trailing TAPs enabled and in bypass
	c_trailing = 0;
	p_tap = jtag_tap_next_enabled(p_tap);
	while ((NULL != p_tap) && (1 == p_tap->bypass))
	{
		c_trailing++;
		p_tap = jtag_tap_next_enabled(p_tap);
	}
   
	// Should be at end of list now?
	// ASSERT(NULL == p_tap);

	LOG_INFO("jtag chain: %d taps, %d enabled, %d leading, %d trailing",
		jtag_tap_count(), jtag_tap_count_enabled(), c_leading, c_trailing);
}

/* "Regular" scan of a scan field */

static bool microsemi_flashpro_scan_field(
	bool f_irscan, struct scan_field *p_scan_field)
{
	/* Assume success until we know otherwise */
	bool f_success = true;
	
	if (f_irscan) 
	{
		/* IR scan */
		if (NULL != p_scan_field->out_value)
		{
			/* Output buffer pointer is not NULL so scan the data specified */
			if (JtagIrScan(
				sg_flashpro.handle,
				p_scan_field->num_bits,
				(const char *)(p_scan_field->out_value),
				(char *)(p_scan_field->in_value),
				AFTER_SCAN_GOTO_PAUSE) != PRGSTAT_OK) 
			{
				LOG_ERROR("JtagIrScan() failed : %s", GetErrorMessage(sg_flashpro.handle));
				f_success = false;
			} 
		}
		else
		{
			/* Output buffer pointer is NULL so scan zeros */
			if (JtagIrScanAllBits(
				sg_flashpro.handle,
				p_scan_field->num_bits,
				0,
				(char *)(p_scan_field->in_value),
				AFTER_SCAN_GOTO_PAUSE) != PRGSTAT_OK) 
			{
				LOG_ERROR("JtagIrScanAllBits() failed : %s", GetErrorMessage(sg_flashpro.handle));
				f_success = false;
			} 
		}
	} 
	else 
	{
		/* DR scan */
		if (NULL != p_scan_field->out_value)
		{
			/* Output buffer pointer is not NULL so scan the data specified */
			if (JtagDrScan(
				sg_flashpro.handle,
				p_scan_field->num_bits,
				(const char *)(p_scan_field->out_value),
				(char *)(p_scan_field->in_value),
				AFTER_SCAN_GOTO_PAUSE) != PRGSTAT_OK) 
			{
				LOG_ERROR("JtagDrScan() failed : %s", GetErrorMessage(sg_flashpro.handle));
				f_success = false;
			} 
		}
		else
		{
			/* Output buffer pointer is NULL so scan zeros */
			if (JtagDrScanAllBits(
				sg_flashpro.handle,
				p_scan_field->num_bits,
				0,
				(char *)(p_scan_field->in_value),
				AFTER_SCAN_GOTO_PAUSE) != PRGSTAT_OK) 
			{
				LOG_ERROR("JtagDrScanAllBits() failed : %s", GetErrorMessage(sg_flashpro.handle));
				f_success = false;
			} 
		}
	}
	
	return f_success;
}		 

/* "Tunnelled" scan of a scan field via UJTAG/uj_jtag using only drscans 
 * using "tunnelled" protocol 
 */

static bool microsemi_flashpro_scan_field_tunnelled(
	bool f_irscan, struct scan_field *p_scan_field)
{
	/* Assume success until we know otherwise */
	bool f_success = true;

	/* drscan or irscan? */
	tap_state_t scan_state;
	scan_state = f_irscan ? TAP_IRSHIFT : TAP_DRSHIFT;
	
	/* uj_jtag can do a ("tunnelled") drscan/irscan of <= 63 bits in one go.
	 * Restrict this to 56 bits (7 bytes) max for simplicity.
	 * Large scan fields need to be broken into 7 byte/56 bit (or shorter)
	 * chunks.
	 * When capturing input data care must be taken to deal with the fact
	 * uj_jtag delays captured/TDO data by one clock tick with 
	 * respect to the outgoing/TDI scan. Accordingly the capture of the 
	 * last data bit is overlapped with the output of the first bit of the
	 * exit phase TMS length and the captured data must be shifted/masked
	 * to be aligned correctly.
	 */
	uint8_t capturing;
	int num_bits_left;
	int num_bits_sent;
	int i;

	capturing = (p_scan_field->in_value != NULL);
	num_bits_left = p_scan_field->num_bits;
	num_bits_sent = 0;	

	/* Any leading bypassed TAPs in chain ... ? */
	if (0 != sg_flashpro.c_leading_bypassed_taps)
	{
		/* ... yes - so scan bits to account for them */
		if (JtagDrScanAllBits(sg_flashpro.handle, 
			sg_flashpro.c_leading_bypassed_taps, 0, 
			NULL, AFTER_SCAN_GOTO_PAUSE) != PRGSTAT_OK) 
		{
			LOG_ERROR("JtagDrScanAllBits() failed : %s", GetErrorMessage(sg_flashpro.handle));
			f_success = false;
		}
	}
	
	/* Scan field in chunks of 56 bits or less */
	while (f_success && (num_bits_left > 0)) 
	{
		uint8_t tms_path;
		uint8_t tms_pathlen;
		
		/* 
		 * 1. uj_jtag entry phase: 
		 * move to ir-scan or dr-scan state 
		 */
		 
		tms_path = microsemi_flashpro_ujtag_tms_path(
			sg_flashpro.ujtag_current_state, scan_state, &tms_pathlen);
		
		if (tms_pathlen > ENTRY_MAX_LEN) 
		{
			LOG_ERROR("Entry TMS path length (%d) is too long", tms_pathlen);
			f_success = false;
			continue;
		}
		
		/* scan out entry TMS length */
		if (JtagDrScan(sg_flashpro.handle, ENTRY_LEN_NUM_BITS, 
			(char *)&tms_pathlen, NULL, AFTER_SCAN_GOTO_PAUSE) != PRGSTAT_OK) 
		{
			LOG_ERROR("JtagDrScan() failed : %s", GetErrorMessage(sg_flashpro.handle));
			f_success = false;
			continue;
		}
		
		/* scan out entry TMS bits */
		if (JtagDrScan(sg_flashpro.handle, tms_pathlen, (char *)&tms_path, 
			NULL, AFTER_SCAN_GOTO_PAUSE) != PRGSTAT_OK) 
		{
			LOG_ERROR("JtagDrScan() failed : %s", GetErrorMessage(sg_flashpro.handle));
			f_success = false;
			continue;
		}

		/* 
		 * 2. uj_jtag shift phase
		 */ 
		uint8_t capture_data_final_bit;
		uint8_t num_chunk_bits;	
		uint8_t num_chunk_bytes;
		
		/* chunk size is 56 bits/7 bytes or less if fewer bits left to scan */
		num_chunk_bits = num_bits_left > MAX_SCAN_CHUNK_BITS ? MAX_SCAN_CHUNK_BITS : num_bits_left;
		num_chunk_bytes = ((num_chunk_bits + 7) / 8);
		
		/* scan out shift data length */
		if (JtagDrScan(sg_flashpro.handle, SHIFT_LEN_NUM_BITS, 
			(const char *)&num_chunk_bits, 
			NULL, AFTER_SCAN_GOTO_PAUSE) != PRGSTAT_OK) 
		{
			LOG_ERROR("JtagDrScan() failed : %s", GetErrorMessage(sg_flashpro.handle));
			f_success = false;
			continue;
		}
		
		/* scan out shift data - capture {data[num_chunk_bits-2:0],x} if necessary */
		if (NULL != p_scan_field->out_value)
		{
			/* Output buffer pointer is not NULL so scan data specified */
			if (JtagDrScan(sg_flashpro.handle, 
				num_chunk_bits, 
				(const char *)&(p_scan_field->out_value[num_bits_sent / 8]),
				capturing ? (char *)&(p_scan_field->in_value[num_bits_sent / 8]) : NULL,
				AFTER_SCAN_GOTO_PAUSE) != PRGSTAT_OK) 
			{
				LOG_ERROR("JtagDrScan() failed : %s", GetErrorMessage(sg_flashpro.handle));
				f_success = false;
				continue;
			}
		}
		else
		{
			/* Output buffer pointer is NULL so scan zeros */
			if (JtagDrScanAllBits(sg_flashpro.handle, 
				num_chunk_bits, 
				0,
				capturing ? (char *)&(p_scan_field->in_value[num_bits_sent / 8]) : NULL,
				AFTER_SCAN_GOTO_PAUSE) != PRGSTAT_OK) 
			{
				LOG_ERROR("JtagDrScan() failed : %s", GetErrorMessage(sg_flashpro.handle));
				f_success = false;
				continue;
			}
		}

		/* 
		 * 3. uj_jtag exit phase: 
		 * go from exit1-dr/ir to pause-dr/ir (TMS = 1'b0)
		 */
		 
		/* scan out exit TMS length - capture final data bit
		 * capture_data[num_chunk_bits-1] if necessary 
		 */
		tms_pathlen = 1;
		if (JtagDrScan(sg_flashpro.handle, EXIT_LEN_NUM_BITS, 
			(char *)&tms_pathlen, 
			capturing ? (char *)&capture_data_final_bit : NULL, 
			AFTER_SCAN_GOTO_PAUSE) != PRGSTAT_OK) 
		{
			LOG_ERROR("JtagDrScan() failed : %s", GetErrorMessage(sg_flashpro.handle));
			f_success = false;
			continue;
		}
		
		/* scan out exit TMS bits */
		tms_path = 0;
		if (JtagDrScan(sg_flashpro.handle, tms_pathlen, (char *)&tms_path, NULL, 
			AFTER_SCAN_GOTO_PAUSE) != PRGSTAT_OK) 
		{
			LOG_ERROR("JtagDrScan() failed : %s", GetErrorMessage(sg_flashpro.handle));
			f_success = false;
			continue;
		}
		
		/* update current state */
		sg_flashpro.ujtag_current_state = 
			(TAP_IRSHIFT == scan_state) ? TAP_IRPAUSE : TAP_DRPAUSE;
		
		/* if capturing then adjust captured data */
		if (capturing) 
		{
			/* align captured data - shift {capture_data[num_chunk_bits-2:0],X} 
			 * right one bit to give {X,capture_data[num_chunk_bits-2:0]}
			 * (X = "don't care").
			 */
			uint8_t *pcapture_data;
			for (i = 0, pcapture_data = &(p_scan_field->in_value[num_bits_sent / 8]);
				i < num_chunk_bytes; 
				i++, pcapture_data++) 
			{
				*pcapture_data >>= 1;
				if (i < (num_chunk_bytes - 1)) 
				{
					/* shift LSb of higher byte into MSb of lower byte */
					if (*(pcapture_data + 1) & 0x01) 
					{
						*pcapture_data |= 0x80;
					} 
					else 
					{
						*pcapture_data &= 0x7f;
					}
				}
			}
			
			/* mask final bit into {X,capture_data[num_chunk_bits-2:0]} to give
			 * capture_data[num_chunk_bits-1:0] (X = "don't care")
			 */
			pcapture_data = 
				&(p_scan_field->in_value[num_bits_sent / 8])
				+ num_chunk_bytes - 1;
			
			if (capture_data_final_bit & 0x01) 
			{
				*pcapture_data |= (1 << ((num_chunk_bits - 1) % 8));
			} 
			else 
			{
				*pcapture_data &= ~(1 << ((num_chunk_bits - 1) % 8));
			}
		}
			
		/* Update counters and status */
		num_bits_sent += num_chunk_bits;
		num_bits_left -= num_chunk_bits;

		if (0 >= num_bits_left)
		{
			/* Any trailing bypassed TAPs in chain ... ? */
			if (0 != sg_flashpro.c_trailing_bypassed_taps)
			{
				/* ... yes - so scan bits to account for them */
				if (JtagDrScanAllBits(sg_flashpro.handle, 
					sg_flashpro.c_trailing_bypassed_taps, 0, 
					NULL, AFTER_SCAN_GOTO_PAUSE) != PRGSTAT_OK) 
				{
					LOG_ERROR("JtagDrScanAllBits() failed : %s", GetErrorMessage(sg_flashpro.handle));
					f_success = false;
				}
			}			
		}
		
		/* 
		 * Finished: move FPGA TAP to run-test-idle
		 * TODO: 
		 * is this correct? 
		 * does it screw up chained debugging?
		 * should it be done using tunnelled protocol instead?
		 */
		if (JtagSetState(sg_flashpro.handle, enIdle) != PRGSTAT_OK) 
		{
			LOG_ERROR("JtagSetState() failed : %s", GetErrorMessage(sg_flashpro.handle));
			f_success = false;
			continue;
		}	
	}
		
	return f_success;
}	

static void microsemi_flashpro_execute_scan(struct jtag_command *cmd)
{	
	char outbuf[1024];
	char inbuf[1024];

	if (sg_flashpro.f_logging)
	{
		LOG_INFO("%s - start; # scan fields = %d", 
			__FUNCTION__, cmd->cmd.scan->num_fields);
		
		print_jtag_chain();
	}

	/* Fatal scan error? */
	uint8_t f_scan_error = 0;
	
	/* TAP for current scan field */
	struct jtag_tap *p_tap = NULL;

	/* Iterate over scan fields */
	for (int current_field = 0; current_field < cmd->cmd.scan->num_fields; current_field++) 
	{
		if (sg_flashpro.f_logging)
		{
			print_scan_bits(
				outbuf, 
				cmd->cmd.scan->fields[current_field].num_bits, 
				cmd->cmd.scan->fields[current_field].out_value
			);
		}

		/* Get TAP for this scan field - i.e. next enabled TAP */
#if 0
		/* Disable attempt at UJTAG chain debug support for SC v5.3 - revisit later */
		p_tap = jtag_tap_next_enabled(p_tap);
#endif

		/* Regular or tunnelled scan ...? */
		if (sg_flashpro.f_tunnel_jtag_via_ujtag)
		{
			/* ... tunnelled scan - current TAP NOT in bypass ...? */ 
#if 0
			/* Disable attempt at UJTAG chain debug support for SC v5.3 - revisit later */
			if ((NULL != p_tap) && (0 == p_tap->bypass))
#endif
			{
				/* ... yes - execute tunnelled scan */
				f_scan_error = !(microsemi_flashpro_scan_field_tunnelled(
					cmd->cmd.scan->ir_scan,
					&(cmd->cmd.scan->fields[current_field])));
			}
			/* ... no - ignore this field as it's taken care of 
			 * by drscanning extra bits for bypassed TAPs in 
			 * microsemi_flashpro_scan_field_tunnelled()
			 */
		}
		else
		{
			/* ... regular scan */
			f_scan_error = !(microsemi_flashpro_scan_field(
				cmd->cmd.scan->ir_scan,
				&(cmd->cmd.scan->fields[current_field])));
		}
		
		if (sg_flashpro.f_logging)
		{
			if (cmd->cmd.scan->fields[current_field].in_value)
			{
				print_scan_bits(
					inbuf, 
					cmd->cmd.scan->fields[current_field].num_bits, 
					cmd->cmd.scan->fields[current_field].in_value
				);
			}
			
			LOG_INFO("%sscan field #%d\n%s %d\t%s%s%s\t%s", 
				sg_flashpro.f_tunnel_jtag_via_ujtag ? "tunnelled " : "",
				current_field,
				cmd->cmd.scan->ir_scan ? "irscan" : "drscan", 
				cmd->cmd.scan->fields[current_field].num_bits,
				outbuf,
				cmd->cmd.scan->fields[current_field].in_value ? "\n" : "",
				cmd->cmd.scan->fields[current_field].in_value ? "captured" : "",
				cmd->cmd.scan->fields[current_field].in_value ? inbuf : "");
		}

		/* Exit on scan error */
		if (f_scan_error)
		{
			LOG_ERROR("Fatal scan error in function %s() - exiting", __FUNCTION__);
			exit(-1);
		}
	}
	
	/* Go to end state */
	if (sg_flashpro.f_tunnel_jtag_via_ujtag)
	{
		microsemi_flashpro_ujtag_set_tap_state(cmd->cmd.scan->end_state);
	}
	else
	{
		microsemi_flashpro_set_tap_state(cmd->cmd.scan->end_state);
	}
	
	if (sg_flashpro.f_logging)
	{
		LOG_INFO("%s - end", __FUNCTION__);
	}
}

static void microsemi_flashpro_execute_reset(struct jtag_command *cmd)
{
	if (sg_flashpro.f_logging)
	{
		LOG_INFO("%s trst = %d, srst = %d", __FUNCTION__, 
			cmd->cmd.reset->trst, cmd->cmd.reset->srst);
	}

	/* FlashPro doesn't support SRSTn so ignore cmd->cmd.reset->srst.
	 * Deal with cmd->cmd.reset->trst. Note that TRSTn is active low. 
	 */ 
	switch(cmd->cmd.reset->trst) 
	{
		case 0: 
		{
			/* De-assert - i.e. drive high */
			if (JtagSetTRST(sg_flashpro.handle, enPinHigh) != PRGSTAT_OK) 
			{
				LOG_ERROR("JtagSetTRST(enPinHigh) failed : %s", 
					GetErrorMessage(sg_flashpro.handle));
				exit(-1);
			}
			break;
		}
		
		case 1: 
		{
			/* Assert - i.e. drive low */
			if (JtagSetTRST(sg_flashpro.handle, enPinLow) != PRGSTAT_OK) 
			{
				LOG_ERROR("JtagSetTRST(enPinLow) failed : %s", 
					GetErrorMessage(sg_flashpro.handle));
				exit(-1);
			}
			break;
		}
		
		case -1:
		default: 
		{
			/* No change - do nothing */
			break;
		}
	}
}

/* Support for tunnelling JTAG via UJTAG/uj_jtag */
static void microsemi_flashpro_ujtag_execute_reset(struct jtag_command *cmd)
{
	if (sg_flashpro.f_logging)
	{
		LOG_INFO("%s trst = %d, srst = %d", __FUNCTION__,
			cmd->cmd.reset->trst, cmd->cmd.reset->srst);
	}

	/* Ignore cmd->cmd.reset->trst/srst and just reset UJTAG/uj_jtag inferior
	 * device using TMS
	 */
	microsemi_flashpro_ujtag_set_tap_state(TAP_RESET);
}

static void microsemi_flashpro_execute_sleep(struct jtag_command *cmd)
{
	if (sg_flashpro.f_logging)
	{
		LOG_INFO("%s %d usec", __FUNCTION__, cmd->cmd.sleep->us);
	}

	if (JtagDelay(
		sg_flashpro.handle,				/* FlashPro programmer handle */
		0,								/* TCK tick count */
		cmd->cmd.sleep->us,				/* Sleep period */
		enWaitUS,						/* Delay for cmd->cmd.sleep->us microseconds */
		FLASHPRO_EXECUTE_IMMEDIATELY	/* Execute immediately */
	) != PRGSTAT_OK) 
	{
		LOG_ERROR("JtagDelay() failed : %s", GetErrorMessage(sg_flashpro.handle));
		exit(-1);	
	}
}

static void microsemi_flashpro_execute_pathmove(struct jtag_command *cmd)
{
	if (sg_flashpro.f_logging)
	{
		LOG_INFO("%s", __FUNCTION__);
	}

	int num_states = cmd->cmd.pathmove->num_states;
	
	/* Check that start and end states are stable */
	if (num_states > 0) 
	{
		if (!tap_is_state_stable(cmd->cmd.pathmove->path[0]) ||
			!tap_is_state_stable(cmd->cmd.pathmove->path[num_states - 1])) 
		{
			LOG_ERROR("Start and end states must be stable");
			exit(-1);
		}
	}
	
	/* Visit all states specified */
	for (num_states = 0; num_states < cmd->cmd.pathmove->num_states; num_states++) {
		microsemi_flashpro_set_tap_state(cmd->cmd.pathmove->path[num_states]);
	}
}

/* Support for tunnelling JTAG via UJTAG/uj_jtag */
static void microsemi_flashpro_ujtag_execute_pathmove(struct jtag_command *cmd)
{
	if (sg_flashpro.f_logging)
	{
		LOG_INFO("%s", __FUNCTION__);
	}

	int num_states = cmd->cmd.pathmove->num_states;
	
	/* Check that start and end states are stable */
	if (num_states > 0) 
	{
		if (!tap_is_state_stable(cmd->cmd.pathmove->path[0]) ||
			!tap_is_state_stable(cmd->cmd.pathmove->path[num_states - 1])) 
		{
			LOG_ERROR("Start and end states must be stable");
			exit(-1);
		}
	}
	
	/* Visit all states specified */
	for (num_states = 0; num_states < cmd->cmd.pathmove->num_states; num_states++) {
		microsemi_flashpro_ujtag_set_tap_state(cmd->cmd.pathmove->path[num_states]);
	}
}

static void microsemi_flashpro_execute_command(struct jtag_command *cmd)
{
	switch (cmd->type) 
	{
		case JTAG_RESET: 
		{
			if (!sg_flashpro.f_tunnel_jtag_via_ujtag)
			{
				microsemi_flashpro_execute_reset(cmd);
			}
			else
			{
				microsemi_flashpro_ujtag_execute_reset(cmd);
			}
			break;
		}
		
		case JTAG_RUNTEST: 
		{
			if (!sg_flashpro.f_tunnel_jtag_via_ujtag)
			{
				microsemi_flashpro_execute_runtest(cmd);
			} 
			else 
			{
				microsemi_flashpro_ujtag_execute_runtest(cmd);
			}
			break;
		}
		
		case JTAG_TLR_RESET: 
		{
			if (!sg_flashpro.f_tunnel_jtag_via_ujtag)
			{
				microsemi_flashpro_execute_statemove(cmd);
			}
			else
			{
				microsemi_flashpro_ujtag_execute_statemove(cmd);
			}
			break;
		}
		
		case JTAG_SCAN: 
		{
			microsemi_flashpro_execute_scan(cmd);
			break;
		}
		
		case JTAG_SLEEP: 
		{
			microsemi_flashpro_execute_sleep(cmd);
			break;
		}
		
		case JTAG_PATHMOVE: 
		{
			if (!sg_flashpro.f_tunnel_jtag_via_ujtag)
			{
				microsemi_flashpro_execute_pathmove(cmd);
			}
			else
			{
				microsemi_flashpro_ujtag_execute_pathmove(cmd);
			}
			break;
		}
		
		default: 
		{
			LOG_ERROR("Unknown JTAG command type encountered: %d", cmd->type);
			break;
		}
	}
}

static int microsemi_flashpro_execute_queue(void)
{
	for (struct jtag_command *cmd = jtag_command_queue; cmd; cmd = cmd->next) 
	{
		microsemi_flashpro_execute_command(cmd);
	}

	return ERROR_OK;
}

static int microsemi_flashpro_initialize(void)
{
	if (sg_flashpro.f_logging)
	{
		LOG_INFO("%s start", __FUNCTION__);
	}

	/* Assume failure until we know otherwise */
	int retval = ERROR_JTAG_INIT_FAILED;
	
	/* Variables for doing port enumeration */
	int c_ports;
	char *pszPort;
	char strbuf[MAX_BUF_SIZE];
	int i;
	int fValidPort = 0;
	
	/* Enumerate ports */
	if (EnumeratePorts(&c_ports, strbuf) != PRGSTAT_OK) 
	{
		c_ports = 0;
	}
	
	/* Note: we only concern ourselves with USB ports for FlashPro3/4/5. We
	 * skip/omit LPT ports because they are for FlashPro Lite which only works
	 * on ProASIC+ and thus irrelevant to us here.
	 */
	strbuf[0] = '\0';
	for (i = 0; i < c_ports; i++) 
	{
		/* Get next port name */
		pszPort = GetPortAt(i);

		/* Skip LPT ports */
		if (strncasecmp(pszPort, "LPT", 3) == 0)
		{
			continue;
		}
		
		/* Avoid buffer overflow; +3 allows for ', \0' so may be more
		 * pessimistic than necessary but simpler to check for.
		 */
		if ((strlen(strbuf) + strlen(pszPort) + 3) > MAX_BUF_SIZE)
		{
			break;
		}

		/* Append port name to list & add comma separator/space if required */
		strcat(strbuf, pszPort);
		strcat(strbuf, (i < (c_ports - 1)) ? ", " : "");
		
		/* If we don't already have a valid port... */
		if (!fValidPort)
		{
			/* No port specified...? */
			if (strlen(sg_flashpro.sz_port) == 0)
			{
				/* ...yes - so use the first port available */
				strcpy(sg_flashpro.sz_port, pszPort);
				fValidPort = 1;
			}
			else
			{
				/* ...no - so try to match or expand it... */
				if (strncasecmp(
						pszPort, 
						sg_flashpro.sz_port, 
						strlen(sg_flashpro.sz_port)) == 0)
				{
					/* current port matches so use it */
					strcpy(sg_flashpro.sz_port, pszPort);
					fValidPort = 1;
				}
			} 
		}
	}

	/* No ports found? */
	if (strlen(strbuf) == 0) 
	{
		strcpy(strbuf, "none");
	}

	/* Final check - allow for generic/catch-all port name "usb" */
	if (!fValidPort)
	{
		fValidPort = (strcmp(sg_flashpro.sz_port, "usb") == 0);
	}

	/* Display available and used ports */
	LOG_INFO("FlashPro ports available: %s", strbuf);
	LOG_INFO("FlashPro port selected:   %s", sg_flashpro.sz_port);

	/* No valid port found? */
	if (!fValidPort)
	{
		LOG_ERROR("'%s' does not match any available port", sg_flashpro.sz_port);
		goto cleanupAndReturn;
	}

	/* Create FlashPro programmer */
	sg_flashpro.handle = CreateProgrammer();
	if (NULL == sg_flashpro.handle) 
	{
		LOG_ERROR("CreateProgrammer() failed");
		goto cleanupAndReturn;
	}
	
	/* Initialize FlashPro programmer */
	if (InitializeProgrammer(sg_flashpro.handle, sg_flashpro.sz_port) != PRGSTAT_OK) 
	{
		LOG_ERROR("InitializeProgrammer(%s) failed : %s", 
			sg_flashpro.sz_port, GetErrorMessage(sg_flashpro.handle));
		goto cleanupAndReturn;
	}

	/* Get programmer info */
	if (GetProgrammerInfo(sg_flashpro.handle, &sg_flashpro.info) != PRGSTAT_OK)
	{
		LOG_ERROR("GetProgrammerInfo() failed : %s", GetErrorMessage(sg_flashpro.handle));
		goto cleanupAndReturn;
	}

	if (sg_flashpro.f_logging)
	{
		LOG_INFO("Programmer info - type = %s, revision = %s, connection type = %s, id = %s",
			sg_flashpro.info.type, sg_flashpro.info.revision, 
			sg_flashpro.info.connectionType, sg_flashpro.info.id
		);
	}

	/* Enable programmer */
	if (EnableProgrammingPort(sg_flashpro.handle, FLASHPRO_ENABLE_PORT) != PRGSTAT_OK) 
	{
		LOG_ERROR("EnableProgrammingPort() failed : %s", GetErrorMessage(sg_flashpro.handle));
		goto cleanupAndReturn;
	}

	/* Success! */
	retval = ERROR_OK;
	
cleanupAndReturn:
		
	/* Clean up after failed init */
	if (ERROR_OK != retval) 
	{
		microsemi_flashpro_quit();
	}
	
	if (sg_flashpro.f_logging)
	{
		LOG_INFO("%s end", __FUNCTION__);
	}

	return retval;
}

static int microsemi_flashpro_quit(void)
{
	if (sg_flashpro.f_logging)
	{
		LOG_INFO("%s", __FUNCTION__);
	}

	int retval = ERROR_OK;

	if (NULL != sg_flashpro.handle) 
	{
		if (DeleteProgrammer(sg_flashpro.handle) != PRGSTAT_OK) 
		{
			LOG_ERROR("DeleteProgrammer() failed : %s", 
				GetErrorMessage(sg_flashpro.handle));
			retval = ERROR_JTAG_DEVICE_ERROR;
		}
		
		sg_flashpro.handle = NULL;
	}

	return retval;
}

/* FlashPro custom commands */
 
COMMAND_HANDLER(handle_microsemi_flashpro_port_command)
{
	if (sg_flashpro.f_logging)
	{
		LOG_INFO("%s", __FUNCTION__);
	}

	if (CMD_ARGC != 1) 
	{
		LOG_ERROR("Single argument specifying FlashPro port expected");
		return ERROR_COMMAND_SYNTAX_ERROR;
	}

	sg_flashpro.sz_port[0] = '\0';
	strncat(sg_flashpro.sz_port, CMD_ARGV[0], sizeof(sg_flashpro.sz_port) - 1); 
	command_print(CMD_CTX, "microsemi_flashpro port %s", sg_flashpro.sz_port);
	return ERROR_OK;
}

COMMAND_HANDLER(handle_microsemi_flashpro_tunnel_jtag_via_ujtag_command)
{
	if (sg_flashpro.f_logging)
	{
		LOG_INFO("%s", __FUNCTION__);
	}

	static const Jim_Nvp nvp_tunnel_jtag_modes[] = {
		{ .name = "off",	.value = 0	},
		{ .name = "on",		.value = 1	},
		{ .name = "disable",.value = 0	},
		{ .name = "enable", .value = 1	},
		{ .name = "0",		.value = 0	},
		{ .name = "1",		.value = 1	},
		{ .name = NULL,		.value = -1 },
	};
	const Jim_Nvp *n;

	if (CMD_ARGC != 1) 
	{
		LOG_ERROR("Single argument specifying JTAG tunnel state expected");
		return ERROR_COMMAND_SYNTAX_ERROR;
	}

	n = Jim_Nvp_name2value_simple(nvp_tunnel_jtag_modes, CMD_ARGV[0]);
	if (n->name == NULL)
	{
		return ERROR_COMMAND_SYNTAX_ERROR;
	}

	sg_flashpro.f_tunnel_jtag_via_ujtag = (n->value == 1);

	sg_flashpro.c_leading_bypassed_taps = 0;
	sg_flashpro.c_trailing_bypassed_taps = 0;

#if 0
	/* Disable attempt at UJTAG chain debug support for SC v5.3 - revisit later */
	if (sg_flashpro.f_tunnel_jtag_via_ujtag)
	{
		/* Count # leading/trailing TAPS enabled and in bypass */
		struct jtag_tap *p_tap;
	
		/* leading */
		p_tap = jtag_tap_next_enabled(NULL);
		while ((NULL != p_tap) && (1 == p_tap->bypass))
		{
			sg_flashpro.c_leading_bypassed_taps++;
			p_tap = jtag_tap_next_enabled(p_tap);
		}

		/* Should be at the single TAP not in bypass now?
		 * ASSERT((NULL != p_tap) && (0 == p_tap->bypass));
		 */
	
		/* trailing */
		p_tap = jtag_tap_next_enabled(p_tap);
		while ((NULL != p_tap) && (1 == p_tap->bypass))
		{
			sg_flashpro.c_trailing_bypassed_taps++;
			p_tap = jtag_tap_next_enabled(p_tap);
		}
	}
#endif
	 
	command_print(CMD_CTX, "microsemi_flashpro tunnel_jtag_via_ujtag %s", n->name);
	return ERROR_OK;
}

COMMAND_HANDLER(handle_microsemi_flashpro_logging_command)
{
	if (sg_flashpro.f_logging)
	{
		LOG_INFO("%s", __FUNCTION__);
	}

	static const Jim_Nvp nvp_tunnel_jtag_modes[] = {
		{ .name = "off",	.value = 0	},
		{ .name = "on",		.value = 1	},
		{ .name = "disable",.value = 0	},
		{ .name = "enable", .value = 1	},
		{ .name = "0",		.value = 0	},
		{ .name = "1",		.value = 1	},
		{ .name = NULL,		.value = -1 },
	};
	const Jim_Nvp *n;

	if (CMD_ARGC != 1) 
	{
		LOG_ERROR("Single argument specifying logging state expected");
		return ERROR_COMMAND_SYNTAX_ERROR;
	}

	n = Jim_Nvp_name2value_simple(nvp_tunnel_jtag_modes, CMD_ARGV[0]);
	if (n->name == NULL)
	{
		return ERROR_COMMAND_SYNTAX_ERROR;
	}

	sg_flashpro.f_logging = (n->value == 1);
	 
	command_print(CMD_CTX, "microsemi_flashpro logging %s", n->name);
	return ERROR_OK;
}

static const struct command_registration microsemi_flashpro_exec_command_handlers[] = 
{
	{
		.name = "port",
		.handler = handle_microsemi_flashpro_port_command,
		.mode = COMMAND_CONFIG,
		.help = "identify a specific FlashPro port to be used",
		.usage = "<flashpro-port-name> e.g. usb71682 (FlashPro3/4/LCPS), S200XTYRZ3 (FlashPro5) etc.",
	},
	{
		.name = "tunnel_jtag_via_ujtag",
		.handler = handle_microsemi_flashpro_tunnel_jtag_via_ujtag_command,
		.mode = COMMAND_ANY,
		.help = "control whether or not JTAG traffic is \"tunnelled\" via UJTAG",
		.usage = "['off'|'on'|'disable'|'enable'|'0'|'1']",
	},
	{
		.name = "logging",
		.handler = handle_microsemi_flashpro_logging_command,
		.mode = COMMAND_ANY,
		.help = "control whether or not logging is on",
		.usage = "['off'|'on'|'disable'|'enable'|'0'|'1']",
	},
	COMMAND_REGISTRATION_DONE
};

static const struct command_registration microsemi_flashpro_command_handlers[] = {
	{
		.name = "microsemi_flashpro",
		.mode = COMMAND_EXEC,
		.help = "Microsemi FlashPro command group",
		.usage = "",
		.chain = microsemi_flashpro_exec_command_handlers,
	},
	// microsemi_flashpro_port is deprecated but provided for backward 
	// compatibility with SoftConsole v4.0. Use microsemi_flashpro port instead.
	// The two versions of this command use the same handler.
	{
		.name = "microsemi_flashpro_port",
		.handler = handle_microsemi_flashpro_port_command,
		.mode = COMMAND_CONFIG,
		.help = "identify a specific FlashPro port to be used",
		.usage = "<flashpro-port-name> e.g. usb71682 (FlashPro3/4/LCPS), S200XTYRZ3 (FlashPro5) etc.",
	},
	COMMAND_REGISTRATION_DONE
};

struct jtag_interface microsemi_flashpro_interface = 
{
	.name =				"microsemi-flashpro",
	.supported =		0,	/* Don't support DEBUG_CAP_TMS_SEQ */
	.commands =			microsemi_flashpro_command_handlers,
	.transports =		jtag_only,
	
	.init =				microsemi_flashpro_initialize,
	.quit =				microsemi_flashpro_quit,
	.speed =			microsemi_flashpro_speed,
	.speed_div =		microsemi_flashpro_speed_div,
	.khz =				microsemi_flashpro_khz,
	.execute_queue =	microsemi_flashpro_execute_queue,
};

