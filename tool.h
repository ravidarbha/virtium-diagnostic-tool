#if !defined __TOOL__H_

#define __TOOL__H_
#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/ata.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <sysexits.h>

#if ((ULONG_MAX) == (UINT_MAX))
# define IS32BIT
#else
# define IS64BIT
#endif

#define SECTOR_SIZE  512
#define MAX_NO_DISKS 3

// FreeBSD implementation
#define SYSTEM_CODE IOCATAREQUEST

#define ATA_CMD_CONTROL                 0x01
#define ATA_CMD_READ                    0x02
#define ATA_CMD_WRITE                   0x04
#define ATA_CMD_ATAPI                   0x08
// Use 32 bit aligning
#define MALLOC_ALIGN 32
#define mfgname "VTDCFAPI004G-KC0"
#define MASK_OFFSET 0 
#define DATA_OFFSET 2 

#define SAVE_U32(buffer, sector, offset, value) do { \
    buffer[sector * SECTOR_SIZE + offset + 0] = ((value >> 24) & 0xFF); \
    buffer[sector * SECTOR_SIZE + offset + 1] = ((value >> 16) & 0xFF); \
    buffer[sector * SECTOR_SIZE + offset + 2] = ((value >>  8) & 0xFF); \
    buffer[sector * SECTOR_SIZE + offset + 3] = ((value      ) & 0xFF); } while(0)
 

struct ata_ioc_request* build_basic_command(uint32_t lba, uint32_t cnt, uint8_t feat,uint8_t comm, uint32_t op, uint32_t size);
int execute_command(struct ata_ioc_request *req, int fd);
int validate_card(uint8_t *data);
void
noupdate_details_disk(char *disk);
void show_details_disk(char *device);

#endif // __TOOL__H_
