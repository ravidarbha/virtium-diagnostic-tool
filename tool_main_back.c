#include "tool.h"

int8_t *align_ptr, *buf;

int free_aligned()
{
    // Free the parent not just he alligned address.
    free(align_ptr);
}

uint8_t *malloc_aligned(uint32_t size, int align) { 

    align_ptr = malloc((size + align - 1) * sizeof(uint8_t));
    uint32_t mask = (align - 1);

    if (align_ptr == NULL)  return NULL;
    buf = (uint8_t *)(((uintptr_t)(align_ptr + align - 1)) & ~(uintptr_t)mask);
 
    return buf;
}

void *alloc_aligned_buffer(uint32_t size) {

    void *ptr = NULL;

    if (size) {
        // Alloc the buffer using the aligned version.
        ptr = malloc_aligned(size, MALLOC_ALIGN);
    }
 
    return ptr;
}

void free_request(struct ata_ioc_request *rq)
{
    free_aligned();
    free(rq); 
}

struct ata_ioc_request* build_basic_command(uint32_t lba, uint32_t cnt, uint8_t feat,uint8_t comm, uint32_t op, uint32_t size)
{
    struct ata_ioc_request *rq = NULL;

    rq = malloc(sizeof(struct ata_ioc_request));

    memset(rq, 0, sizeof(*rq));

    rq->timeout = 30;
    rq->u.ata.lba = lba;
    rq->u.ata.count = cnt;
    rq->u.ata.feature = feat;
    rq->u.ata.command = comm;
    rq->count= size;
    rq->flags = ATA_CMD_CONTROL;
    // Dont allocate for the writes, we need bigger
    rq->data = (uint8_t*)(void*)alloc_aligned_buffer(size);

    if (rq->data) {
        memset(rq->data, 0, size);
    }

    if (op == ATA_CMD_READ) {
        rq->flags |= ATA_CMD_READ;
    }
    else if (op == ATA_CMD_WRITE) {
        rq->flags |= ATA_CMD_WRITE;
    }
    return rq;
}

int validate_card(uint8_t *data)
{
    int i, valmfg = 0;

    for (i=54; i<69; i+=2) {
        if(data[i+1] != mfgname[i-54]) valmfg = 1;
        if(data[i] != mfgname[i-53]) valmfg = 1;
    }

    // Not the right type, just return.
    if(valmfg == 1) {
       return -1;
    }

    return 0;
}

int validate_mfg_card(char *device, int fd)
{
    int ret = 0;
    struct ata_ioc_request *req;

    req = build_basic_command(0, 1, 0, ATA_ATA_IDENTIFY, ATA_CMD_READ, SECTOR_SIZE);

    ret = execute_command(req, fd);
    if(ret)
      err(1, "Validate mfg execute command failed.%d %d ",ret, errno);

    ret = validate_card(req->data);

    if (ret == 0) {
        show_details_disk(req->data, device);
    }
    else {
        noupdate_details_disk(device);
    }
    
    free_request(req);

    return ret;
}

int print_anchor_block(uint8_t *data)
{
    int i;
    for (i=0;i<512; i++) {
         if (i%16 == 0) {
               printf("%3.3d-%3.3d ",i,i+15);
         }
	 // Only char strings for now.
         printf("%2.2x", data[i] & 0xff);
         if (i%2 == 1) {
            printf("\n");
	}

    }

    printf("\nMaxBlockEraseCount = 0x%2.2x%2.2x%2.2x%2.2x",data[100] & 0xff,data[101] & 0xff ,data[102] & 0xff,data[103] & 0xff );

    if(((data[100] & 0xff) == 0x00)
         && ((data[101] & 0xff) == 0x00)
         && ((data[102] & 0xff) == 0x00)
         && ((data[103] & 0xff) == 0x00))
    {
        return 0;
    }
    return 1;
}

int read_anchor_block(int fd)
{
    int ret = 0;
    struct ata_ioc_request *req;

    req = build_basic_command(0, 0x88, 0x57, ATA_SETFEATURES, 0, 0);
    if((ret = execute_command(req, fd))) {
       err(1, "Read Anchor block set features failed..");
       return ret;
    }
    req = build_basic_command(0, 1, 0x75, ATA_READ, ATA_CMD_READ, SECTOR_SIZE);
 
    if((ret = execute_command(req, fd))) {
       err(1, "Read Anchor block Read failed..");
       return ret;
    }
    // Return the error code.
    return print_anchor_block(req->data);
}

int prepare_write_buffer(uint8_t *data, int flag)
{

    SAVE_U32(data, MASK_OFFSET, 100, 0xFFFFFFFF);
 
    if(!flag)
    {
       SAVE_U32(data, DATA_OFFSET, 100, 100000);
       SAVE_U32(data, DATA_OFFSET, 140, 80000);
       SAVE_U32(data, DATA_OFFSET, 144, 100000);
       SAVE_U32(data, DATA_OFFSET, 148, 100002);
    }
    else
    {
      // Reset this to 0.
      SAVE_U32(data, DATA_OFFSET, 100, 0);
    }

    printf("rq->data.:%x %x %x %x\n",data[1024 + 100], data[1024+101], data[1024+102], data[1024+103]);
   
}

int fix_anchor_block(int fd, int flag)
{
    int ret = 0;
    struct ata_ioc_request *req;

    req = build_basic_command(0, 0x08, 0x07, 0x92, ATA_CMD_WRITE, 8192);
    // Prepare the write buffer.
    prepare_write_buffer(req->data , flag);

    ret = execute_command(req, fd);
    return ret;
}


int execute_command(struct ata_ioc_request *req, int fd)
{
    int ret = 0;

    ret = ioctl(fd, SYSTEM_CODE, req);

    return ret;
}


int reset_channel(int fd)
{
    struct ata_ioc_request *req;
    int ret = 0, channel = 0;

    req = build_basic_command(0, 0x03, 0x07, 0xc2, 0, 0);
    
    ret = execute_command(req, fd);
    return ret;
}

char *compute_disks() {

    int disk_cnt;
    int error;
    size_t listsize;
    char *disklist;

    error = sysctlbyname("kern.disks", NULL, &listsize, NULL, 0);
    if (error) {
        warn("kern.disks sysctl not available");
        return NULL;
    }

    if (listsize == 0)
        return (NULL);

    disklist = (char *)malloc(listsize + 1);
    if (disklist == NULL) {
        return NULL;
    }
    memset(disklist, 0, listsize + 1);
    error = sysctlbyname("kern.disks", disklist, &listsize, NULL, 0);
    if (error || disklist[0] == 0) {
        free(disklist);
        return NULL;
    }

    return disklist;
}


void usage()
{
    fprintf(stderr,
	"usage:  virt-diag <command> args:\n"
	"        virt-diag -r <read the card details> \n"
	"        virt-diag -w < read card details and update> \n"
	"        virt-diag -f < Reset the card to 0 (Only for testing)> \n"
	);
	exit(EX_USAGE);
}


void 
show_details_disk(uint8_t *data, char *disk)
{
    int i;

    fprintf(stdout, " The disk :%s requires an update. Serial# ",disk);
    for (i=54; i<93; i+=2) {
        fprintf(stdout, "%c%c",data[i+1],data[i]);
    }

    fprintf(stdout, "\n");
}

void 
noupdate_details_disk(char *disk)
{
    fprintf(stdout, " The disk :%s doesnt need an update. ",disk);
    fprintf(stdout, "\n");
}

// This is the diagnostic tool. It should be run only once to correct the wear level value on the virtium cards.
int main(int argc, char *argv)
{
    char *disklist;
    char *disk, ch;
    int reset = 0, disk_cnt, ret, fd, read = 0;

    if (argc < 2) {
         usage();
    }

    while ((ch = getopt(argc, argv, "rw")) != -1) 
                switch (ch) {
                case 'r':
                        read = 1;
                        break;
                case 'w':
                        read = 0;
                        break;
		case 'f':
			reset = 1;
			break;
                case '?':
                default:
                        usage();
                        /* NOTREACHED */
                }

    // Disks_computed.
    disklist = compute_disks();

    for (disk_cnt = 0; disk_cnt < MAX_NO_DISKS; disk_cnt++)
    {
        int mode;
        char device[64];

        disk = strsep(&disklist, " ");
        if (disk == NULL) 
            break;

        sprintf(device, "/dev/%s", disk);

        if ((fd = open(device, O_RDONLY)) < 0)
             err(1, "device not found");

        // If mfg is different or only reading  move on.
        if(((ret = validate_mfg_card(device, fd))) || read == 1) {
            close(fd);
            continue;
        }

        if((ret = read_anchor_block(fd)) && !reset) {
            printf("The value is Already Set. \n");
            close(fd);
            continue;
        }
        printf(" Setting the Card ..Might Take a Moment ..Please wait..\n");

        if((ret = fix_anchor_block(fd ,reset))) {
            err(1, "Fixing Anchor block failed.");
        }

        if((ret = reset_channel(fd))) {
            err(1, "Reset of channel failed for some reason.\n");
        }

        if((ret = read_anchor_block(fd))) {
            printf("Set to correct value .\n");
        }
        close(fd);
   }
   exit(0);

}

