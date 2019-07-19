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
 * OpenOCD driver for Microsemi CoreAhbNvm which interfaces to/wraps 
 * Fusion ([M1]AFSXXX[X]) eNVM (embedded NVM).
 * 
 * Based to a large extent on the CoreAhbNvm firmware driver.
 * 
 * Reference material:
 * http://www.microsemi.com/products/fpga-soc/fpga/fusion#documents
 * CoreAhbNvm 
 * http://soc.microsemi.com/products/ip/search/detail.aspx?id=669
 *
 * Base address of CoreAhbNvm Fusion eNVM depends on the configuration of 
 * CoreAhbNvm and the AMBA bus in the target SoC.
 * Size can be 256kB ([M1]AFS090, [M1]AFS250), 512kB ([M1]AFS600) or 1MB ([M1]AFS1500).
 * This is 1, 2 or 4 x 2MBit eNVM blocks. However the OpenOCD flash bank
 * command and driver treat CoreAhbNvm Fusion eNVM as a single linearly 
 * addressable space so does not concern itself with the number of 2Mbit 
 * blocks used.
 * A 128 (0x7F) byte page is the unit of programming
 * For the purpose of this driver a sector means an eNVM page
 * even though Fusion eNVM has its own internal concept of "sector".
 * Fusion eNVM also supports "spare" and "auxiliary" pages but
 * we ignore these here and just deal with "regular" pages.
 */

/* eNVM address and size details */
#define ENVM_BLOCK_SIZE					0x00040000u	/* 256kB */
#define ENVM_MAX_SIZE					(ENVM_BLOCK_SIZE * 4)
#define ENVM_PAGE_SIZE					0x80u		/* 128 bytes per page */
#define ENVM_PAGE_OFFSET_MASK			(ENVM_PAGE_SIZE - 1)
#define ENVM_PAGE_ALIGN_MASK			~(ENVM_PAGE_OFFSET_MASK) 
#define ENVM_PAGE_BASE_MASK				(ENVM_MAX_SIZE - ENVM_PAGE_SIZE)
#define ENVM_PAGE_NUM(x)				(((x) & ENVM_PAGE_BASE_MASK) >> 7)

/* CoreAhbNvm commands */
#define ENVM_READ_ARRAY_CMD				0xFF
#define ENVM_READ_STATUS_CMD			0x70
#define ENVM_CLEAR_STATUS_CMD			0x50
#define ENVM_ERASE_PAGE_CMD				0x20
#define ENVM_SINGLE_WRITE_CMD			0x40
#define ENVM_MULTI_WRITE_CMD			0xE8
#define ENVM_CONFIRM_CMD				0xD0

/* CoreAhbNvm status register bits */
#define ENVM_READY_BIT_MASK				0x80
#define ENVM_READ_ERROR_BIT_MASK		0x02
#define ENVM_WRITE_ERROR_BIT_MASK		0x10

/* List of FPGA device names and their respective eNVM sizes */
typedef struct fpga_device
{
	char *psz_name;
	uint32_t envm_size;
} st_fpga_device;

static st_fpga_device g_afpga_devices[] = 
{
	/* SmartFusion [M1]AFSXXX[X] devices */
	{ "AFS090",		ENVM_BLOCK_SIZE },		/* 256kBytes */
	{ "M1AFS090",	ENVM_BLOCK_SIZE },		/* 256kBytes */
	{ "AFS250",		ENVM_BLOCK_SIZE }, 		/* 256kBytes */
	{ "M1AFS250",	ENVM_BLOCK_SIZE }, 		/* 256kBytes */
	{ "AFS600", 	ENVM_BLOCK_SIZE * 2 }, 	/* 512kBytes */
	{ "M1AFS600", 	ENVM_BLOCK_SIZE * 2 }, 	/* 512kBytes */
	{ "AFS1500",	ENVM_BLOCK_SIZE * 4 },	/* 1MBytes */
	{ "M1AFS1500",	ENVM_BLOCK_SIZE * 4 },	/* 1MBytes */
	{ NULL, 		ENVM_MAX_SIZE },		/* 1MBytes - unknown device, allow for max envm size */
};

typedef enum envm_status
{
    ENVM_SUCCESS = 0,
    ENVM_PROTECTION_ERROR,
    ENVM_WRITE_ERROR,
	ENVM_TARGET_ACCESS_ERROR
} envm_status_t;

FLASH_BANK_COMMAND_HANDLER(microsemi_fusion_coreahbnvm_flash_bank_command)
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
	
	if (((bank->size) > max_size) ||
		(((bank->base) & ENVM_PAGE_OFFSET_MASK) != 0) ||
		(((bank->size) % ENVM_PAGE_SIZE) != 0))
	{
		LOG_ERROR("invalid envm base address ('0x%08x') and/or size ('0x%08x') specified", bank->base, bank->size);
		return ERROR_FAIL;
	}
	
	/* Validate bus_width */
	switch (bank->bus_width) 
	{
		case 0:
			/* if 0 then default to 4 bytes */
			bank->bus_width = 4;
			break;
			
		case 1:
		case 2:
		case 4:
			/* these are all valid values */
			break;
			
		default:
			LOG_WARNING("invalid bus_width %d specified - should be 1, 2 or 4 (bytes) - defaulting to 4", bank->bus_width);
			break;
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

static int microsemi_fusion_coreahbnvm_erase(
	struct flash_bank *bank, 
	int first, 
	int last
)
{
	/* Fusion eNVM does not use an explicit erase. Data can be written 
	 * any time. We don't waste a write cycle by writing all 0s or 1s. 
	 * Instead we just mark the sector (page) erased in the driver.
	 */
	for ( ; first <= last; first++)
	{
		bank->sectors[first].is_erased = 1;
	}

	return ERROR_OK;
}

static int microsemi_fusion_coreahbnvm_protect(
	struct flash_bank *bank, 
	int set, 
	int first, 
	int last
)
{
	/* Fusion eNVM does not support lock/unlock as discrete operations
	 * only as part of a page write operation. As such we don't implement
	 * this method.
	 */
	LOG_ERROR("Microsemi Fusion CoreAhbNvm eNVM driver does not implement the protect method");
	return ERROR_OK;
}

static envm_status_t microsemi_fusion_coreahbnvm_wait(
	struct flash_bank *bank, 
	uint32_t page_start_addr
)
{
	envm_status_t retval;
	uint8_t ready;

	retval = ENVM_SUCCESS;
	ready = 0;

	/* Wait for status ready bit to go high. */
	do {
		uint8_t status_reg;
		
		if (target_read_u8(bank->target, page_start_addr, &status_reg) 
			!= ERROR_OK)
		{
			retval = ENVM_TARGET_ACCESS_ERROR;
			continue;
		}
		
		if (status_reg & ENVM_WRITE_ERROR_BIT_MASK)
		{
			retval = ENVM_WRITE_ERROR;
			continue;
		}
		
		ready = ((status_reg & ENVM_READY_BIT_MASK) == ENVM_READY_BIT_MASK);
	} while ((!ready) && (ENVM_SUCCESS == retval));
	
	return retval;
}

static int microsemi_fusion_coreahbnvm_write(
	struct flash_bank *bank, 
	const uint8_t *buffer, 
	uint32_t offset, 
	uint32_t count
)
{
	envm_status_t status = ENVM_SUCCESS;

	uint32_t envm_write_address;
	uint8_t progress = 0;
	
	/* Old and new page buffers */
	uint8_t old_page[ENVM_PAGE_SIZE];
	uint8_t new_page[ENVM_PAGE_SIZE];
	
	/* Start address for write */
	envm_write_address = bank->base + offset;

	LOG_INFO("Microsemi Fusion CoreAhbNvm eNVM - writing %d (0x%x) bytes to address 0x%08lx (. = 1024 bytes)", count, count, (unsigned long)((bank->base) + offset));	

	while ((count > 0) && (ENVM_SUCCESS == status))
	{
		uint32_t page_start_addr;
		uint32_t page_bytes_written;

		/* Get page start address */
		page_start_addr = envm_write_address & ENVM_PAGE_ALIGN_MASK;
		
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
			
			/* Write the command "Setup  Write Buffer" to the page address */
			if (target_write_u8(bank->target, 
				page_start_addr, ENVM_MULTI_WRITE_CMD) != ERROR_OK)
			{
				status = ENVM_TARGET_ACCESS_ERROR;
				continue;
			}
			
			/* Wait until target ready */
			if (microsemi_fusion_coreahbnvm_wait(bank, page_start_addr) 
				!= ENVM_SUCCESS)
			{
				continue;
			}
			
			/* Tell controller how many writes to expect (-1) */
			if (target_write_u8(
				bank->target, 
				page_start_addr, 
				((ENVM_PAGE_SIZE / bank->bus_width) - 1)) != ERROR_OK)
			{
				status = ENVM_TARGET_ACCESS_ERROR;
				continue;
			}

			/* Write new page data bank->bus_width bytes at a time */
			for (uint8_t i = 0; 
				((i < ENVM_PAGE_SIZE) && (ENVM_SUCCESS == status)); 
				i += bank->bus_width)
			{
				if (target_write_memory(
					bank->target, 
					page_start_addr + i,
					bank->bus_width,
					1,
					new_page + i) != ERROR_OK)
				{
					status = ENVM_TARGET_ACCESS_ERROR;
				}
			}
			
			/* Initiate page write */
			if (target_write_u8(
				bank->target, 
				page_start_addr, 
				ENVM_CONFIRM_CMD) != ERROR_OK)
			{
				status = ENVM_TARGET_ACCESS_ERROR;
				continue;
			}
			
			/* Wait for status ready bit to go high. */
			/* Wait until target ready */
			if (microsemi_fusion_coreahbnvm_wait(bank, page_start_addr) 
				!= ENVM_SUCCESS)
			{
				status = ENVM_TARGET_ACCESS_ERROR;
				continue;
			}

			/* Make array readable. */
			if (target_write_u8(
				bank->target, 
				page_start_addr, 
				ENVM_READ_ARRAY_CMD) != ERROR_OK)
			{
				status = ENVM_TARGET_ACCESS_ERROR;
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

	/* Any problems? */
	char *psz_error;
	switch (status)
	{
		case ENVM_PROTECTION_ERROR:
			psz_error = "protection error";
			break;

		case ENVM_WRITE_ERROR:
			psz_error = "write error";
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
			envm_write_address, ENVM_PAGE_NUM(envm_write_address), psz_error);
	}
	
	return (ENVM_SUCCESS == status) ? ERROR_OK : ERROR_FAIL;
}

static int microsemi_fusion_coreahbnvm_protect_check(struct flash_bank *bank)
{
	/* Fusion eNVM does not provide any way to check the 
	 * protection/locking status of pages so this method just assumes
	 * that the protection status of all sectors (pages) is unknown.
	 */

	for (int i = 0; i < (bank->num_sectors); i++)
	{
		bank->sectors[i].is_erased = -1;
	}

	return ERROR_OK;
}

static int microsemi_fusion_coreahbnvm_info(
	struct flash_bank *bank, 
	char *buf, 
	int buf_size
)
{
	snprintf(buf, buf_size, "Microsemi Fusion ([M1]AFSXXX[X]) CoreAhbNvm eNVM flash driver");
	return ERROR_OK;
}

static int microsemi_fusion_coreahbnvm_erase_check(struct flash_bank *bank)
{
	/* Fusion eNVM does not use an explicit erase. Data can be written 
	 * any time. Since sectors (pages) are always writeable (subject to 
	 * protection and locking) we just assume that they are erased.
	 */

	for (int i = 0; i < (bank->num_sectors); i++)
	{
		bank->sectors[i].is_erased = 1;
	}

	return ERROR_OK;
}

static int microsemi_fusion_coreahbnvm_probe(struct flash_bank *bank)
{
	microsemi_fusion_coreahbnvm_protect_check(bank);
	microsemi_fusion_coreahbnvm_erase_check(bank);
	return ERROR_OK;
}

struct flash_driver microsemi_fusion_coreahbnvm_flash = {
	.name = "microsemi-fusion-coreahbnvm",
	.usage = "flash bank [M1]AFS<XXX[X]>.envm microsemi-fusion-coreahbnvm 0x00000000 <size> 0 0 [M1]AFS<XXX[X]>.cpu",
	.flash_bank_command = microsemi_fusion_coreahbnvm_flash_bank_command,
	.erase = microsemi_fusion_coreahbnvm_erase,
	.protect = microsemi_fusion_coreahbnvm_protect,
	.write = microsemi_fusion_coreahbnvm_write,
	.read = default_flash_read,
	.probe = microsemi_fusion_coreahbnvm_probe,
	.auto_probe = microsemi_fusion_coreahbnvm_probe,
	.erase_check = microsemi_fusion_coreahbnvm_erase_check,
	.protect_check = microsemi_fusion_coreahbnvm_protect_check,
	.info = microsemi_fusion_coreahbnvm_info,
	.free_driver_priv = default_flash_free_driver_priv,
};
