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
 * OpenOCD driver for Microsemi SmartFusion (A2FXXX) eNVM (embedded NVM)
 * 
 * Based to a large extent on the SmartFusion MSS eNVM firmware driver.
 * 
 * Reference material:
 * http://www.microsemi.com/products/fpga-soc/soc-fpga/smartfusion#documents
 * SmartFusion Microcontroller Subsystem (MSS) User's Guide - Chapter 4 - Embedded Nonvolatile Memory (eNVM) Controller
 * http://www.microsemi.com/document-portal/doc_download/130935-smartfusion-microcontroller-subsystem-mss-user-s-guide
 *
 * Native base address in the SF MSS Cortex-M3 memory map is 0x60000000
 * Size can be 128kB (A2F060), 256kB (A2F200) or 512kB (A2F500)
 * 512kB eNVM devices have two "blocks" of 256kB but OpenOCD flash bank command
 * treats eNVM as a single linearly addressable space so does not concern 
 * itself with the number of blocks. This is an internal matter for the driver.
 * A 128 (0x7F) byte page is the unit of programming
 * For the purpose of this driver a sector means an eNVM page
 * even though SmartFusion eNVM has its own internal concept of "sector".
 * SmartFusion eNVM also supports "spare" and "auxiliary" pages but
 * we ignore these here and just deal with "regular" pages.
 */

/* eNVM address and size details */
#define ENVM_BASE_ADDRESS				0x60000000u
#define ENVM_BLOCK_SIZE					0x00040000u	/* 256kB */
#define ENVM_BLOCK0_BASE_ADDRESS		ENVM_BASE_ADDRESS
#define ENVM_BLOCK1_BASE_ADDRESS		(ENVM_BASE_ADDRESS + ENVM_BLOCK_SIZE)
#define ENVM_MAX_SIZE					(ENVM_BLOCK_SIZE * 2)
#define ENVM_PAGE_SIZE					0x80u		/* 128 bytes per page */

#define ENVM_OFFSET_MASK				(ENVM_MAX_SIZE - 1)	/* Mask for converting raw address to offset from start of eNVM */
#define ENVM_PAGE_OFFSET_MASK			(ENVM_PAGE_SIZE - 1)
#define ENVM_PAGE_BASE_MASK				(~(ENVM_PAGE_SIZE - 1)) /* Mask for eNVM page aligning a raw address */
#define ENVM_PAGE_NUM(x)				(((x) & (ENVM_MAX_SIZE - 1)) >> 7)

/* eNVM controller command details */
#define ENVM_PROGRAM_CMD				0x10000000u
#define ENVM_UNPROTECT_CMD				0x02000000u
#define ENVM_DISCARD_PAGE_CMD			0x04000000u

/* eNVM block 0 and block 1 status bits */
#define ENVM_BLOCK0_BUSY				0x00000001u
#define ENVM_BLOCK0_PROT_ERROR			0x00000002u
#define ENVM_BLOCK0_PROG_ERROR			0x00000004u
#define ENVM_BLOCK0_THRESHOLD_ERROR		0x00000010u
#define ENVM_BLOCK0_ECC1_ERROR			0x00000020u
#define ENVM_BLOCK0_ECC2_ERROR			0x00000040u
#define ENVM_BLOCK0_ILLEGAL_CMD_ERROR	0x00008000u

#define ENVM_BLOCK0_PROTECTION_ERROR	(ENVM_BLOCK0_PROT_ERROR)
#define ENVM_BLOCK0_PROGRAM_ERROR	(ENVM_BLOCK0_PROG_ERROR | \
					ENVM_BLOCK0_ECC1_ERROR | \
					ENVM_BLOCK0_ECC2_ERROR | \
					ENVM_BLOCK0_ILLEGAL_CMD_ERROR)

#define ENVM_BLOCK1_BUSY				0x00010000u
#define ENVM_BLOCK1_PROT_ERROR			0x00020000u
#define ENVM_BLOCK1_PROG_ERROR			0x00040000u
#define ENVM_BLOCK1_THRESHOLD_ERROR		0x00100000u
#define ENVM_BLOCK1_ECC1_ERROR			0x00200000u
#define ENVM_BLOCK1_ECC2_ERROR			0x00400000u
#define ENVM_BLOCK1_ILLEGAL_CMD_ERROR	0x80000000u

#define ENVM_BLOCK1_PROTECTION_ERROR	(ENVM_BLOCK1_PROT_ERROR)
#define ENVM_BLOCK1_PROGRAM_ERROR	(ENVM_BLOCK1_PROG_ERROR | \
					ENVM_BLOCK1_ECC1_ERROR | \
					ENVM_BLOCK1_ECC2_ERROR | \
					ENVM_BLOCK1_ILLEGAL_CMD_ERROR)

#define ENVM_STICKY_BITS_RESET			0xFFFFFFFFu

/* eNVM controller register addresses */
#define ENVM_STATUS_REG					0x60100000u
#define ENVM_CONTROL_REG				0x60100004u

#define ENVM_CR_SYSREG					0xE0042004u

#define ENVM_CR_MODE					0x000000C0u
#define ENVM_CR_6_CYCLES_MODE			0x000000C0u


/* List of FPGA device names and their respective eNVM sizes */
typedef struct fpga_device
{
	char *psz_name;
	uint32_t envm_size;
} st_fpga_device;

static st_fpga_device g_afpga_devices[] = 
{
	/* SmartFusion A2FXXX devices */
	{ "A2F060", ENVM_BLOCK_SIZE / 2 },	/* 128kBytes */
	{ "A2F200", ENVM_BLOCK_SIZE }, 		/* 256kBytes */
	{ "A2F500", ENVM_BLOCK_SIZE * 2 }, 	/* 512kBytes */
	{ NULL, 	ENVM_MAX_SIZE }, 		/* 512kBytes - unknown device, allow for max envm size */
};

/* eNVM block 0/1 status masks */
typedef struct envm_block_status
{
	uint32_t busy;
	uint32_t protection_error;
	uint32_t program_error;
} envm_block_status_t;

static envm_block_status_t g_aenvm_block[] =
{
	/* eNVM block 0 */
	{
		ENVM_BLOCK0_BUSY,
		ENVM_BLOCK0_PROTECTION_ERROR,
		ENVM_BLOCK0_PROGRAM_ERROR,
	},
	/* eNVM block 1 */
	{
		ENVM_BLOCK1_BUSY,
		ENVM_BLOCK1_PROTECTION_ERROR,
		ENVM_BLOCK1_PROGRAM_ERROR,
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
	ENVM_PROGRAM_ERROR,
	ENVM_TARGET_ACCESS_ERROR
} envm_status_t;

/*
 * flash bank <device> microsemi_smartfusion_envm <base> <size> <chip_width> <bus_width> <target#>
 * flash bank A2FXXX.envm microsemi_smartfusion_envm 0x60000000 <size> 0 0 A2FXXX.cpu
 *
 * CMD_ARGV[0] = microsemi_smartfusion_envm
 * CMD_ARGV[1] = <base> e.g. 0x60000000
 * CMD_ARGV[2] = <size> e.g. 0x00080000
 * CMD_ARGV[3] = <chip_width> e.g. 0
 * CMD_ARGV[4] = <bus_width> e.g. 0
 * CMD_ARGV[5] = <target#> e.g. A2F500.cpu
 *
 * Note that this ARG arrangement is what happens in practice but does not match
 * the comments in driver.h...
 */

FLASH_BANK_COMMAND_HANDLER(microsemi_smartfusion_envm_flash_bank_command)
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
		LOG_ERROR("invalid envm base address (0x%08x) and/or size (0x%08x) specified", bank->base, bank->size);
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

static int microsemi_smartfusion_envm_erase(
	struct flash_bank *bank, 
	int first, 
	int last
)
{
	/* SmartFusion eNVM does not use an explicit erase. Data can be written 
	 * any time. We don't waste a write cycle by writing all 0s or 1s. 
	 * Instead we just mark the sector (page) erased in the driver.
	 */
	for ( ; first <= last; first++)
	{
		bank->sectors[first].is_erased = 1;
	}

	return ERROR_OK;
}

static int microsemi_smartfusion_envm_protect(
	struct flash_bank *bank, 
	int set, 
	int first, 
	int last
)
{
	/* SmartFusion eNVM does not support lock/unlock as discrete operations
	 * only as part of a page write operation. As such we don't implement
	 * this method.
	 */
	LOG_ERROR("Microsemi SmartFusion eNVM driver does not implement the protect method");
	return ERROR_OK;
}

static int microsemi_smartfusion_envm_write(
	struct flash_bank *bank, 
	const uint8_t *buffer, 
	uint32_t offset, 
	uint32_t count
)
{
	envm_status_t status = ENVM_SUCCESS;

	uint32_t envm_cr_old;
	uint32_t envm_cr_new;

	uint32_t envm_write_address;
	uint8_t progress = 0;
	
	/* Old and new page buffers */
	uint8_t old_page[ENVM_PAGE_SIZE];
	uint8_t new_page[ENVM_PAGE_SIZE];

	/* Set eNVM mode to 6 cycles (6:1:1:1) */
	if (target_read_u32(bank->target, ENVM_CR_SYSREG, &envm_cr_old) == ERROR_OK)
	{
		envm_cr_new = (envm_cr_old & ~ENVM_CR_MODE) | ENVM_CR_6_CYCLES_MODE;
		if (target_write_u32(bank->target, ENVM_CR_SYSREG, envm_cr_new) 
			!= ERROR_OK)
		{
			status = ENVM_TARGET_ACCESS_ERROR;
		}
	}
	else 
	{
		status = ENVM_TARGET_ACCESS_ERROR;
	}
	
	/* Start address for write */
	envm_write_address = ENVM_BASE_ADDRESS + offset;

	LOG_INFO("Microsemi SmartFusion eNVM - writing %d (0x%x) bytes to address 0x%08x (. = 1024 bytes)", count, count, bank->base + offset);	

	while ((count > 0) && (ENVM_SUCCESS == status))
	{
		uint32_t command;
		uint32_t error;
		uint32_t page_start_addr;
		uint32_t envm_status;
		uint8_t ready;
		uint32_t page_bytes_written;

		envm_block_id_t envm_block_id;

		/* Get page start address */		
		page_start_addr = envm_write_address & ENVM_PAGE_BASE_MASK;
		
		/* Number of page bytes to be written */
		page_bytes_written = 
			(page_start_addr + ENVM_PAGE_SIZE) - envm_write_address;
		if (count < page_bytes_written)
		{
			page_bytes_written = count;
		}
		
		/* Read page, copy, modify, compare to see if page write needed */
		if (target_read_buffer(
			bank->target,
			page_start_addr,
			ENVM_PAGE_SIZE,
			old_page) != ERROR_OK)
		{
			status = ENVM_TARGET_ACCESS_ERROR;
			continue;
		}
		
		memcpy(new_page, old_page, ENVM_PAGE_SIZE);
		memcpy(new_page + (envm_write_address & ENVM_PAGE_OFFSET_MASK), 
			buffer, page_bytes_written);
		
		/* Have page contents changed necessitating a page write...? */
		if (memcmp(new_page, old_page, ENVM_PAGE_SIZE) != 0) 
		{
			/* ... yes! */
			
			/* Which block is this page in? */
			envm_block_id = (page_start_addr < ENVM_BLOCK1_BASE_ADDRESS) ? 
				envm_block_0 : envm_block_1;

			/* Reset status reg sticky bits */
			if (target_write_u32(bank->target, 
				ENVM_STATUS_REG, ENVM_STICKY_BITS_RESET) != ERROR_OK)
			{
				status = ENVM_TARGET_ACCESS_ERROR;
				continue;
			}

			/* Unprotect page just in case */
			command = ENVM_UNPROTECT_CMD | (page_start_addr & ENVM_OFFSET_MASK);
			if (target_write_u32(bank->target, 
				ENVM_CONTROL_REG, command) != ERROR_OK)
			{
				status = ENVM_TARGET_ACCESS_ERROR;
				continue;
			}

			/* Wait for command to complete... */
			do {
				if (target_read_u32(bank->target, 
					ENVM_STATUS_REG, &envm_status) == ERROR_OK)
				{
					ready = 
						((envm_status & g_aenvm_block[envm_block_id].busy) == 0);
				}
				else
				{
					ready = 0;
					status = ENVM_TARGET_ACCESS_ERROR;
				}
			} while (!ready && (ENVM_SUCCESS == status));

			/* Check for errors */
			if (ENVM_SUCCESS != status)
			{
				continue;
			}

			error = envm_status & g_aenvm_block[envm_block_id].protection_error;
			if (error)
			{
				status = ENVM_PROTECTION_ERROR;
				continue;
			}

			/* Copy new page data to page buffer */
			if (target_write_memory(bank->target, 
				page_start_addr, 1, ENVM_PAGE_SIZE, new_page) != ERROR_OK)
			{
				status = ENVM_TARGET_ACCESS_ERROR;
				continue;
			}

			/* Program page */
			command = ENVM_PROGRAM_CMD | (page_start_addr & ENVM_OFFSET_MASK);
			if (target_write_u32(bank->target, 
				ENVM_CONTROL_REG, command) != ERROR_OK)
			{
				status = ENVM_TARGET_ACCESS_ERROR;
				continue;
			}

			/* Wait for command to complete... */
			do {
				if (target_read_u32(bank->target, 
					ENVM_STATUS_REG, &envm_status) == ERROR_OK)
				{
					ready = 
						((envm_status & g_aenvm_block[envm_block_id].busy) == 0);
				}
				else
				{
					ready = 0;
					status = ENVM_TARGET_ACCESS_ERROR;
				}
			} while (!ready && (ENVM_SUCCESS == status));

			/* Check for errors */
			if (ENVM_SUCCESS != status)
			{
				continue;
			}

			error = envm_status & g_aenvm_block[envm_block_id].program_error;
			if (error)
			{
				status = ENVM_PROGRAM_ERROR;
				continue;
			}
		}
		
		/* Decrement remaining count and increment buffer and write address pointers */
		count -= page_bytes_written;
		buffer += page_bytes_written;
		envm_write_address += page_bytes_written;
		
		/* Log progress and yield every 1kBytes (8 x 128 pages) written */
		if (((++progress) % 8) == 0) 
		{
			LOG_USER_N(".");
			keep_alive();
		}
	}
	
	/* End progress */
	LOG_USER(((progress % 8) == 0) ? "" : ".");
	
	/* Issue discard page command [why?] */
	target_write_u32(bank->target, ENVM_CONTROL_REG, ENVM_DISCARD_PAGE_CMD);

	/* Restore original eNVM mode */
	target_write_u32(bank->target, ENVM_CR_SYSREG, envm_cr_old);

	/* Any problems? */
	char *psz_error;
	switch (status)
	{		
		case ENVM_PROTECTION_ERROR:
			psz_error = "protection error";
			break;

		case ENVM_PROGRAM_ERROR:
			psz_error = "programming error";
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
			envm_write_address - ENVM_BASE_ADDRESS + bank->base, 
            ENVM_PAGE_NUM(envm_write_address - ENVM_BASE_ADDRESS + bank->base), 
            psz_error);
	}
	
	return (ENVM_SUCCESS == status) ? ERROR_OK : ERROR_FAIL;
}

static int microsemi_smartfusion_envm_protect_check(struct flash_bank *bank)
{
	/* SmartFusion eNVM does not provide any way to check the 
	 * protection/locking status of pages so this method just assumes
	 * that the protection status of all sectors (pages) is unknown.
	 */

	for (int i = 0; i < (bank->num_sectors); i++)
	{
		bank->sectors[i].is_erased = -1;
	}

	return ERROR_OK;
}

static int microsemi_smartfusion_envm_info(
	struct flash_bank *bank, 
	char *buf, 
	int buf_size
)
{
	snprintf(buf, buf_size, "Microsemi SmartFusion (A2FXXX) eNVM flash driver");
	return ERROR_OK;
}

static int microsemi_smartfusion_envm_erase_check(struct flash_bank *bank)
{
	/* SmartFusion eNVM does not use an explicit erase. Data can be written 
	 * any time. Since sectors (pages) are always writeable (subject to 
	 * protection and locking) we just assume that they are erased.
	 */

	for (int i = 0; i < (bank->num_sectors); i++)
	{
		bank->sectors[i].is_erased = 1;
	}

	return ERROR_OK;
}

static int microsemi_smartfusion_envm_probe(struct flash_bank *bank)
{
	microsemi_smartfusion_envm_protect_check(bank);
	microsemi_smartfusion_envm_erase_check(bank);
	return ERROR_OK;
}

struct flash_driver microsemi_smartfusion_envm_flash = {
	.name = "microsemi-smartfusion-envm",
	.usage = "flash bank A2F<XXX>.envm microsemi-smartfusion-envm 0x60000000 <size> 0 0 A2F<XXX>.cpu",
	.flash_bank_command = microsemi_smartfusion_envm_flash_bank_command,
	.erase = microsemi_smartfusion_envm_erase,
	.protect = microsemi_smartfusion_envm_protect,
	.write = microsemi_smartfusion_envm_write,
	.read = default_flash_read,
	.probe = microsemi_smartfusion_envm_probe,
	.auto_probe = microsemi_smartfusion_envm_probe,
	.erase_check = microsemi_smartfusion_envm_erase_check,
	.protect_check = microsemi_smartfusion_envm_protect_check,
	.info = microsemi_smartfusion_envm_info,
	.free_driver_priv = default_flash_free_driver_priv,
};

