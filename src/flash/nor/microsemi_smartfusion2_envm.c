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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "imp.h"
#include <target/image.h>

/*
 * OpenOCD driver for Microsemi SmartFusion2 (M2SXXX) eNVM (embedded NVM)
 * 
 * Based to a large extent on the SmartFusion2 MSS eNVM firmware driver.
 * 
 * Reference material:
 * http://www.microsemi.com/products/fpga-soc/soc-fpga/smartfusion2#documentation
 * SmartFusion2 Microcontroller Subsystem User's Guide - Chapter 4 - Embedded NVM (eNVM) Controllers
 * http://www.microsemi.com/document-portal/doc_download/130918-ug0331-smartfusion2-microcontroller-subsystem-user-guide
 *
 * Native base address in the SF2 MSS Cortex-M3 memory map is 0x60000000
 * Size can be 128kB (M2S005), 256kB (M2S010/025/050) or 512kB (M2S090/100/150)
 * 512kB eNVM devices have two "blocks" of 256kB but OpenOCD flash bank command
 * treats eNVM as a single linearly addressable space so does not concern 
 * itself with the number of blocks. This is an internal matter for the driver.
 * A 128 (0x7F) byte page is the unit of programming
 * A 4kB sector comprises 32 x 128 byte pages but these sectors are not relevant here
 * For the purpose of this driver a sector means an eNVM page
 */

/* eNVM address and size details */
#define ENVM_BLOCK_SIZE					0x00040000u	/* 256kB */
#define ENVM_MAX_SIZE					(ENVM_BLOCK_SIZE * 2)
#define ENVM_PAGE_SIZE					0x80u		/* 128 bytes per page */

#define ENVM_PAGE_OFFSET_MASK			(ENVM_PAGE_SIZE - 1)
#define ENVM_PAGE_BASE_MASK				(ENVM_MAX_SIZE - ENVM_PAGE_SIZE)
#define ENVM_PAGE_NUM(x)				(((x) & ENVM_PAGE_BASE_MASK) >> 7)

/* ENVM_CR:NV_FREQRNG register:field details - see SAR 57543*/
#define ENVM_CR							0x4003800Cu
#define ENVM_FREQRNG_MASK				0xFFFFE01Fu
#define ENVM_FREQRNG_MAX				((uint32_t)0xFF << 5u)

/* eNVM controller command details */
#define ENVM_PROG_ADS					0x08000000u	/* One shot page program with data in WD buffer */
#define ENVM_VERIFY_ADS					0x10000000u	/* One shot page verify with data in WD buffer */
#define ENVM_USER_UNLOCK				0x13000000u	/* User unlock */
#define ENVM_FREE_ACCESS				0x00000000u
#define ENVM_REQUEST_ACCESS				0x00000001u
#define ENVM_M3_ACCESS_GRANTED			0x00000005u
#define ENVM_FABRIC_ACCESS_GRANTED		0x00000006u
#define ENVM_PROTECTION_FAIL_CLEAR		0x00000002u
#define ENVM_UNLOCK_PAGE				0x00000000u

/* eNVM status bits */
#define ENVM_READY						0x00000001u	/* Status bit 0 set to 1 when last operation completed */
#define ENVM_VERIFY_FAIL				0x00000002u	/* Verify failed */
#define ENVM_ERASE_VERIFY_FAIL			0x00000004u	/* Erase verify failed */
#define ENVM_WRITE_VERIFY_FAIL			0x00000008u	/* Write verify failed */
#define ENVM_PROGRAM_ERASE_LOCK_FAIL	0x00000010u	/* Program/erase failed due to page lock */
#define ENVM_WRITE_COUNT_EXCEEDED_FAIL	0x00000020u	/* Page write count exceeded */
#define ENVM_WRITE_PROTECTION_FAIL		0x00040000u	/* Write denied due to page protection */
#define ENVM_WRITE_ERROR_MASK			(ENVM_VERIFY_FAIL | \
										ENVM_ERASE_VERIFY_FAIL | \
										ENVM_WRITE_VERIFY_FAIL | \
										ENVM_PROGRAM_ERASE_LOCK_FAIL | \
										ENVM_WRITE_PROTECTION_FAIL)

/* List of FPGA device names and their respective eNVM sizes */
typedef struct fpga_device
{
	char *psz_name;
	uint32_t envm_size;
} st_fpga_device;

static st_fpga_device g_afpga_devices[] = 
{
	/* SmartFusion2 M2SXXX devices */
	{ "M2S005", ENVM_BLOCK_SIZE / 2 },	/* 128kBytes */
	{ "M2S010", ENVM_BLOCK_SIZE },		/* 256kBytes */
	{ "M2S025", ENVM_BLOCK_SIZE }, 		/* 256kBytes */
	{ "M2S050", ENVM_BLOCK_SIZE }, 		/* 256kBytes */
	{ "M2S060", ENVM_BLOCK_SIZE },		/* 512kBytes */
	{ "M2S090", ENVM_BLOCK_SIZE * 2 },	/* 512kBytes */
	{ "M2S100", ENVM_BLOCK_SIZE * 2 },	/* 512kBytes */
	{ "M2S150", ENVM_BLOCK_SIZE * 2 },	/* 512kBytes */
	{ NULL,		ENVM_MAX_SIZE },		/* 512kBytes - unknown device, allow for max envm size */
};

/* eNVM block 0/1 controller register addresses */
typedef struct envm_block
{
	uint32_t write_data_buffer;
	uint32_t status_reg;
	uint32_t pagelock_reg;
	uint32_t command_reg;
	uint32_t clrhint_reg;
	uint32_t request_access_reg;
} envm_block_t;

static envm_block_t g_aenvm_block[] =
{
	/* eNVM block 0 */
	{
		0x60080080, /* write_data_buffer */
		0x60080120, /* status_reg */
		0x60080140, /* pagelock_reg */
		0x60080148, /* command_reg */
		0x60080158, /* clrhint_reg */
		0x600801fc, /* request_access_reg */
	},
	/* eNVM block 1 */
	{
		0x600c0080, /* write_data_buffer */
		0x600c0120, /* status_reg */
		0x600c0140, /* pagelock_reg */
		0x600c0148, /* command_reg */
		0x600c0158, /* clrhint_reg */
		0x600c01fc, /* request_access_reg */
	},
};

typedef enum envm_block_id
{
	envm_block_0 = 0,
	envm_block_1,
} envm_block_id_t;

typedef enum envm_status
{
	ENVM_SUCCESS = 0,
	ENVM_PROTECTION_ERROR,
	ENVM_VERIFY_ERROR,
	ENVM_PAGE_LOCK_ERROR,
	ENVM_WRITE_THRESHOLD_ERROR,
	ENVM_IN_USE_BY_OTHER_MASTER,
	ENVM_TARGET_ACCESS_ERROR
} envm_status_t;

/* Forward declarations for private implementation functions */
static envm_status_t envm_lock_controller(struct flash_bank *bank, envm_block_id_t envm_block_id);
static envm_status_t envm_unlock_controllers(struct flash_bank *bank);
static envm_status_t envm_lock_controllers(struct flash_bank *bank, uint32_t offset, uint32_t count);
static envm_status_t envm_wait_ready(struct flash_bank *bank, envm_block_id_t envm_block_id);
static envm_status_t envm_status_from_hw_status(uint32_t hw_status);
static uint32_t envm_get_remaining_page_length(uint32_t offset, uint32_t length);
static uint32_t envm_write_page(
	struct flash_bank *bank,
	const uint8_t *pdata,
	uint32_t offset,
	uint32_t length,
	envm_status_t *pstatus);
static envm_status_t envm_fill_page_buffer(
	struct flash_bank *bank,
	uint32_t offset,
	uint32_t count,
	const uint8_t *pdata,
	envm_block_id_t envm_block_id,
	uint8_t *pf_modified);

/*
 * flash bank <device> microsemi_smartfusion2_envm <base> <size> <chip_width> <bus_width> <target#>
 * flash bank M2SXXX.envm microsemi_smartfusion2_envm 0x60000000 <size> 0 0 M2SXXX.cpu
 *
 * CMD_ARGV[0] = microsemi_smartfusion2_envm
 * CMD_ARGV[1] = <base> e.g. 0x60000000
 * CMD_ARGV[2] = <size> e.g. 0x00080000
 * CMD_ARGV[3] = <chip_width> e.g. 0
 * CMD_ARGV[4] = <bus_width> e.g. 0
 * CMD_ARGV[5] = <target#> e.g. M2S100.cpu
 *
 * Note that this ARG arrangement is what happens in practice but does not match
 * the comments in driver.h...
 */

FLASH_BANK_COMMAND_HANDLER(microsemi_smartfusion2_envm_flash_bank_command)
{
	uint32_t max_size;
	uint32_t i;
	
	/* Validate base address and size */
	for (i = 0;
		((g_afpga_devices[i].psz_name != NULL) && 
		(strncasecmp(bank->name, g_afpga_devices[i].psz_name, strlen(g_afpga_devices[i].psz_name)) != 0));
		i++)
	{
		/* Do nothing */
	}
	max_size = g_afpga_devices[i].envm_size;

	/* Don't restrict the base address to 0x60000000 in order to allow for 
     * the possibility of using envm mirrored @ 0x00000000 which can simplify
     * things by allowing programs compiled for ebvm to have LMA == VMA and no
     * for mirroring/remapping.
     */
	if ((bank->size > max_size) ||
		(((bank->base) & ENVM_PAGE_OFFSET_MASK) != 0) ||
		(((bank->size) % ENVM_PAGE_SIZE) != 0))
	{
		LOG_ERROR("invalid eNVM base address (0x%08x) and/or size (0x%08x) specified", bank->base, bank->size);
		return ERROR_FAIL;
	}		

	/* Build sector list - in this case an eNVM page is a "sector" */
	bank->num_sectors = bank->size / ENVM_PAGE_SIZE;
	bank->sectors = malloc(sizeof(struct flash_sector) * (bank->num_sectors));
	if (NULL == bank->sectors)
	{
		LOG_ERROR("no memory for sector list");
		return ERROR_FAIL;
	}
	
	for (i = 0; i < (uint32_t)(bank->num_sectors); i++)
	{
		bank->sectors[i].offset = i * ENVM_PAGE_SIZE;
		bank->sectors[i].size = ENVM_PAGE_SIZE;
		/* Erased? Assume so since eNVM has no specific erase mode. */
		bank->sectors[i].is_erased = 1;
		/* Protected? Don't know. */
		bank->sectors[i].is_protected = -1;
	}
	
	return ERROR_OK;
}

static int microsemi_smartfusion2_envm_erase(struct flash_bank *bank, int first, int last)
{
	/* SmartFusion2 eNVM does not use an explicit erase. Data can be written 
	 * any time. We don't waste a write cycle by writing all 0s or 1s. 
	 * Instead we just mark the sector (page) erased in the driver.
	 */
	for ( ; first <= last; first++)
	{
		bank->sectors[first].is_erased = 1;
	}

	return ERROR_OK;
}

static int microsemi_smartfusion2_envm_protect(
	struct flash_bank *bank, 
	int set, 
	int first, 
	int last
)
{
	/* SmartFusion2 eNVM does not support lock/unlock as discrete operations
	 * only as part of a page write operation. As such we don't implement
	 * this method.
	 */
	LOG_ERROR("Microsemi SmartFusion2 eNVM driver does not implement the protect method");
	return ERROR_OK;
}

static int microsemi_smartfusion2_envm_write(
	struct flash_bank *bank, 
	const uint8_t *buffer, 
	uint32_t offset, 
	uint32_t count
)
{
	envm_status_t status;
	uint32_t remaining_length = count;
	uint32_t progress = 0;
	
	LOG_INFO("Microsemi SmartFusion2 eNVM - writing %d (0x%x) bytes to address 0x%08x (. = 1024 bytes)", 
		count, count, bank->base + offset);

	/* Lock eNVM controller(s) */
	status = envm_lock_controllers(bank, offset, count);

	/* Write a (possibly partial) page at a time */
	while ((remaining_length > 0) && (ENVM_SUCCESS == status))
	{
		remaining_length -= envm_write_page(
			bank, 
			&buffer[count - remaining_length], 
			offset + (count - remaining_length),
			remaining_length,
			&status);
			
		/* Log progress and yield every 1kBytes (8 x 128 pages) written */
		if (((++progress) % 8) == 0) 
		{
			LOG_USER_N(".");
			keep_alive();
		}
	}
	
	/* End progress */
	LOG_USER(((progress % 8) == 0) ? "" : ".");

	/* Unlock eNVM controller(s) */
	if (ENVM_SUCCESS != envm_unlock_controllers(bank))
	{
		LOG_ERROR("error unlocking eNVM controller");
	}

	/* Any problems? */
	char *psz_error;
	switch (status)
	{
		case ENVM_PROTECTION_ERROR:
			psz_error = "protection error";
			break;

		case ENVM_VERIFY_ERROR:
			psz_error = "verify error";
			break;

		case ENVM_PAGE_LOCK_ERROR:
			psz_error = "page lock error";
			break;

		case ENVM_IN_USE_BY_OTHER_MASTER:
			psz_error = "eNVM locked by another master";
			break;

		case ENVM_TARGET_ACCESS_ERROR:
			psz_error = "target access error";
			break;

		default:
			psz_error = NULL;
			break;
	}
	
	if (NULL != psz_error) 
	{
		LOG_ERROR("eNVM write failed at address 0x%08x/page %d - %s",
			bank->base + offset + count - remaining_length,
			ENVM_PAGE_NUM(offset + count - remaining_length),
			psz_error);
	}
	
	return (ENVM_SUCCESS == status) ? ERROR_OK : ERROR_FAIL;
}

static int microsemi_smartfusion2_envm_protect_check(struct flash_bank *bank)
{
	/* SmartFusion2 eNVM does not provide any way to check the 
	 * protection/locking status of pages so this method just assumes
	 * that the protection status of all sectors (pages) is unknown.
	 */

	for (int i = 0; i < (bank->num_sectors); i++)
	{
		bank->sectors[i].is_erased = -1;
	}

	return ERROR_OK;
}

static int microsemi_smartfusion2_envm_info(
	struct flash_bank *bank, 
	char *buf, 
	int buf_size
)
{
	snprintf(buf, buf_size, "Microsemi SmartFusion2 (M2SXXX) eNVM flash driver");
	return ERROR_OK;
}

static int microsemi_smartfusion2_envm_erase_check(struct flash_bank *bank)
{
	/* SmartFusion2 eNVM does not use an explicit erase. Data can be written 
	 * any time. Since sectors (pages) are always writeable (subject to 
	 * protection and locking) we just assume that they are erased.
	 */

	for (int i = 0; i < (bank->num_sectors); i++)
	{
		bank->sectors[i].is_erased = 1;
	}

	return ERROR_OK;
}

static int microsemi_smartfusion2_envm_probe(struct flash_bank *bank)
{
	microsemi_smartfusion2_envm_protect_check(bank);
	microsemi_smartfusion2_envm_erase_check(bank);
	return ERROR_OK;
}

struct flash_driver microsemi_smartfusion2_envm_flash = {
	.name = "microsemi-smartfusion2-envm",
	.usage = "flash bank M2S<XXX>.envm microsemi-smartfusion2-envm 0x60000000 <size> 0 0 M2S<XXX>.cpu",
	.flash_bank_command = microsemi_smartfusion2_envm_flash_bank_command,
	.erase = microsemi_smartfusion2_envm_erase,
	.protect = microsemi_smartfusion2_envm_protect,
	.write = microsemi_smartfusion2_envm_write,
	.read = default_flash_read,
	.probe = microsemi_smartfusion2_envm_probe,
	.auto_probe = microsemi_smartfusion2_envm_probe,
	.erase_check = microsemi_smartfusion2_envm_erase_check,
	.protect_check = microsemi_smartfusion2_envm_protect_check,
	.info = microsemi_smartfusion2_envm_info,
	.free_driver_priv = default_flash_free_driver_priv,
};

/* Which eNVM controllers are currently in use? */
static uint8_t g_envm_controller_locks = 0x00u;

/* Lock controller for the specified eNVM block */
static envm_status_t envm_lock_controller(
	struct flash_bank *bank, 
	envm_block_id_t envm_block_id
)
{
	uint32_t u32temp;
	envm_status_t status = ENVM_TARGET_ACCESS_ERROR;
	
	/* Request access */
	if (target_write_u32(bank->target, 
		g_aenvm_block[envm_block_id].request_access_reg, 
		ENVM_REQUEST_ACCESS) != ERROR_OK)
	{
		goto cleanup_and_return;
	}

	/* Check if granted */
	if (target_read_u32(bank->target, 
		g_aenvm_block[envm_block_id].request_access_reg, 
		&u32temp) != ERROR_OK)
	{
		return status;
	}

	if ((ENVM_M3_ACCESS_GRANTED == u32temp) ||
		(ENVM_FABRIC_ACCESS_GRANTED == u32temp))
	{
		/* Successfully got access */
		/* Note M3 or fabric access considered success - the latter can happen
		 * when fabric CPU is accessing MSS eNVM via FIC
		 */
		status = ENVM_SUCCESS;

		/* Remember which eNVM controller we locked */
		g_envm_controller_locks |= (1 << envm_block_id);
	}
	else
	{
		/* eNVM in use by another master */
		status = ENVM_IN_USE_BY_OTHER_MASTER;
	}

cleanup_and_return:

	return status;
}

/* Saved ENVM_CR - see SAR 57543 */
static uint32_t g_envm_cr = 0u;

/* Release eNVM block controllers */
static envm_status_t envm_unlock_controllers(struct flash_bank *bank)
{
	uint8_t target_access_errors = 0;
	uint8_t block_locked;

	/* Unlock envm block 0 if necessary */
	block_locked = g_envm_controller_locks & (1 << envm_block_0);
	if (block_locked)
	{
		target_access_errors += 
			(target_write_u32(bank->target, 
				g_aenvm_block[envm_block_0].request_access_reg, 
				ENVM_FREE_ACCESS) != ERROR_OK);
		g_envm_controller_locks &= ~(1 << envm_block_0);
	}

	/* Unlock envm block 1 if necessary */
	block_locked = g_envm_controller_locks & (1 << envm_block_1);
	if (block_locked)
	{
		target_access_errors += 
			(target_write_u32(bank->target, 
				g_aenvm_block[envm_block_1].request_access_reg, 
				ENVM_FREE_ACCESS) != ERROR_OK);
		g_envm_controller_locks &= ~(1 << envm_block_1);
	}

	/* Restore saved ENVM_CR */
	target_access_errors += 
		(target_write_u32(bank->target, ENVM_CR, g_envm_cr) != ERROR_OK);

	return ((0 == target_access_errors) ? ENVM_SUCCESS : ENVM_TARGET_ACCESS_ERROR);
}

/* Lock controllers for eNVM block(s) based on target offset and count */
static envm_status_t envm_lock_controllers(
	struct flash_bank *bank, 
	uint32_t offset, 
	uint32_t count
)
{
	envm_status_t access_req_status = ENVM_SUCCESS;

	/* Need access to eNVM block 0...? */
	if (offset < ENVM_BLOCK_SIZE)
	{
		/* ... yes! */
		access_req_status = envm_lock_controller(bank, envm_block_0);
		if (ENVM_SUCCESS == access_req_status)
		{
			/* Also need access to eNVM block 1...? */
			if ((offset + count - 1u) >= ENVM_BLOCK_SIZE)
			{
				/* ... yes! */
				access_req_status = envm_lock_controller(bank, envm_block_1);
				if (ENVM_SUCCESS != access_req_status)
				{
					/* Couldn't get both so release */
					envm_unlock_controllers(bank);
				}
			}
		}
	}
	/* ... no! Just eNVM block 1 */
	else
	{
		access_req_status = envm_lock_controller(bank, envm_block_1);
	}

	/* Save current ENVM_CR */
	if (ENVM_SUCCESS == access_req_status)
	{
		if (target_read_u32(bank->target, ENVM_CR, &g_envm_cr) != ERROR_OK)
		{
			access_req_status = ENVM_TARGET_ACCESS_ERROR;
		}		
	}

	return access_req_status;
}

/* Wait for eNVM to become ready after submitting a command */
static envm_status_t envm_wait_ready(
	struct flash_bank *bank, 
	envm_block_id_t envm_block_id
)
{
	envm_status_t status = ENVM_SUCCESS;
	uint32_t hw_status;
	uint8_t i;

	/* SmartFusion2 errata dictates that the busy bit must read as 1
	 * TWICE before assuming that the last operation has completed 
	 */
	for (i = 0; i < 2; i++)
	{
		do {
			if (target_read_u32(
				bank->target, 
				g_aenvm_block[envm_block_id].status_reg, 
				&hw_status) != ERROR_OK)
			{
				status = ENVM_TARGET_ACCESS_ERROR;
				goto cleanup_and_return;
			}
		} while (0 == (hw_status & ENVM_READY));
	}

	if ((hw_status & ENVM_WRITE_ERROR_MASK) != 0)
	{
		status = envm_status_from_hw_status(hw_status);
	}

cleanup_and_return:
		
	return status;
}

/* Convert eNVM hw status code to a status enum value */
static envm_status_t envm_status_from_hw_status(uint32_t hw_status)
{
	envm_status_t status = ENVM_SUCCESS;

	if (hw_status & ENVM_WRITE_PROTECTION_FAIL)
	{
		status = ENVM_PROTECTION_ERROR;
	}
	else if (hw_status & ENVM_PROGRAM_ERASE_LOCK_FAIL)
	{
		status = ENVM_PAGE_LOCK_ERROR;
	}
	else if (hw_status & 
		(ENVM_VERIFY_FAIL | ENVM_ERASE_VERIFY_FAIL | ENVM_WRITE_VERIFY_FAIL))
	{
		status = ENVM_VERIFY_ERROR;
	}		

	return status;
}

#define ENVM_VERIFY_FAIL				0x00000002u	/* Verify failed */
#define ENVM_ERASE_VERIFY_FAIL			0x00000004u	/* Erase verify failed */
#define ENVM_WRITE_VERIFY_FAIL			0x00000008u	/* Write verify failed */

/* Return number of bytes between offset location and the end of the page
 * containing the first offset location. This tells us how many actual bytes
 * can be programmed with a single ProgramADS command. Also tells us if we 
 * are programming a full page. If the return value is ENVM_PAGE_SIZE then we
 * will be programming an entire page. Alternatively this function returning
 * a value other/less than ENVM_PAGE_SIZE indicates that the page WD buffer 
 * will need to be seeded with the existing contents of that eNVM page before
 * copying in the data that is to be changed as a result of this page program
 * operation
 */

static uint32_t envm_get_remaining_page_length(uint32_t offset, uint32_t length)
{
	uint32_t start_page_plus_one;
	uint32_t last_page;

	start_page_plus_one = (offset / ENVM_PAGE_SIZE) + 1;
	last_page = (offset + length) / ENVM_PAGE_SIZE;

	if (last_page >= start_page_plus_one)
	{
		length = ENVM_PAGE_SIZE - (offset % ENVM_PAGE_SIZE);
	}

	return length;
}

static uint32_t envm_write_page(
	struct flash_bank *bank,
	const uint8_t *pdata,
	uint32_t offset,
	uint32_t length,
	envm_status_t *pstatus
)
{
	envm_block_id_t envm_block_id;
	uint32_t length_written;
	uint8_t f_modified;

	*pstatus = ENVM_SUCCESS;

	/* How many bytes to write to relevant page? */
	length_written = envm_get_remaining_page_length(offset, length);

	/* Which eNVM block? */
	envm_block_id = 
		(offset < ENVM_BLOCK_SIZE) ? 
		envm_block_0 : envm_block_1;

	/* Fill page WD buffer */
	*pstatus = envm_fill_page_buffer(bank, 
		offset, length_written, pdata, envm_block_id, &f_modified);
	if (ENVM_SUCCESS != *pstatus)
	{
		goto cleanup_and_return;
	}
	
	/* Have page contents changed necessitating a page write...? */
	if (f_modified) 
	{
		/* ... yes! - so a page write is required */
		
		/* Adjust offset to be eNVM block relative if necessary */
		offset -= (envm_block_id * ENVM_BLOCK_SIZE);

		/* Unlock page just in case */
		if (target_write_u32(
			bank->target,
			g_aenvm_block[envm_block_id].pagelock_reg,
			ENVM_UNLOCK_PAGE | (offset & ENVM_PAGE_BASE_MASK)) != ERROR_OK)
		{
			*pstatus = ENVM_TARGET_ACCESS_ERROR;
			goto cleanup_and_return;
		}

		/* Program page */
		if (target_write_u32(
			bank->target,
			g_aenvm_block[envm_block_id].command_reg,
			ENVM_PROG_ADS | (offset & ENVM_PAGE_BASE_MASK)) != ERROR_OK)
		{
			*pstatus = ENVM_TARGET_ACCESS_ERROR;
			goto cleanup_and_return;
		}		

		/* Wait until finished */
		*pstatus = envm_wait_ready(bank, envm_block_id);

		/* Check result */
		if (ENVM_SUCCESS == *pstatus)
		{
			/* Verify page */
			if (target_write_u32(
				bank->target,
				g_aenvm_block[envm_block_id].command_reg,
				ENVM_VERIFY_ADS | (offset & ENVM_PAGE_BASE_MASK)) != ERROR_OK)
			{
				*pstatus = ENVM_TARGET_ACCESS_ERROR;
				goto cleanup_and_return;
			}

			/* Wait until finished */
			*pstatus = envm_wait_ready(bank, envm_block_id);
		}
	} 
	
cleanup_and_return:

	return (ENVM_SUCCESS == *pstatus) ? length_written : 0;
}

static envm_status_t envm_fill_page_buffer(
	struct flash_bank *bank,
	uint32_t offset,
	uint32_t count,
	const uint8_t *pdata,
	envm_block_id_t envm_block_id,
	uint8_t *pf_modified
)
{
	/* Page offset and number */
	uint32_t page_offset = offset & ENVM_PAGE_OFFSET_MASK;	
	
	/* Assume success & page contents unchanged until we know otherwise */
	envm_status_t status = ENVM_SUCCESS;	
	*pf_modified = 0;
	
	/* Are we writing a full page? */
	if (ENVM_PAGE_SIZE == count) 
	{
		/* Yes. Is page write needed? Fill WD buffer & run verify check */
		uint32_t page_base_addr = offset & ENVM_PAGE_BASE_MASK;

		/* Write page data to WD buffer */
		if (target_write_memory(
			bank->target, 
			g_aenvm_block[envm_block_id].write_data_buffer,
			1,
			count,
			pdata) != ERROR_OK)
		{
			status = ENVM_TARGET_ACCESS_ERROR;
			goto cleanup_and_return;			
		}

		/* Verify page */
		if (target_write_u32(
			bank->target,
			g_aenvm_block[envm_block_id].command_reg,
			ENVM_VERIFY_ADS | page_base_addr) != ERROR_OK)
		{
			status = ENVM_TARGET_ACCESS_ERROR;
			goto cleanup_and_return;
		}

		/* Wait until finished */
		status = envm_wait_ready(bank, envm_block_id);
		
		/* Check for verify failure which means page contents changed */
		if (ENVM_VERIFY_ERROR == status)
		{
			*pf_modified = 1;
			status = ERROR_OK;
		}
	}
	else 
	{
		/* Not a full page so we need to do a read, modify, compare */
		uint8_t wd_old[ENVM_PAGE_SIZE];
		uint8_t wd_new[ENVM_PAGE_SIZE];
		uint32_t page_num = ENVM_PAGE_NUM(offset);
		
		/* Read existing page contents */
		if (target_read_buffer(
			bank->target,
			bank->base + bank->sectors[page_num].offset,
			ENVM_PAGE_SIZE,
			wd_old) != ERROR_OK)
		{
			status = ENVM_TARGET_ACCESS_ERROR;
			goto cleanup_and_return;			
		}
		
		/* Make a copy of existing page contents */
		memcpy(wd_new, wd_old, ENVM_PAGE_SIZE);
		
		/* Modify copy  */
		memcpy(wd_new + page_offset, pdata, count);
		
		/* Check if page contents changed - if so then page needs to be written,
		 * if not then no page write required.
		 */
		if (memcmp(wd_new, wd_old, ENVM_PAGE_SIZE) != 0) 
		{
			/* Page contents changed so need to write */
			*pf_modified = 1;
			
			/* Write page data to WD buffer */
			if (target_write_memory(
				bank->target, 
				g_aenvm_block[envm_block_id].write_data_buffer,
				1,
				ENVM_PAGE_SIZE,
				wd_new) != ERROR_OK)
			{
				status = ENVM_TARGET_ACCESS_ERROR;
			}
		}
	}

cleanup_and_return:

	return status;
}
