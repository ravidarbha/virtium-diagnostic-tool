#include "tool.h"

#ifdef IS32BIT
uint8_t *alignedbuffer, *unalignedbuffer;

void free_aligned() {

    free(unalignedbuffer);
}

uint8_t *malloc_aligned(uint32_t size) {

    const size_t ALIGN_BYTES = 64;
    const size_t ALIGN_BITMASK = (ALIGN_BYTES - 1);

    unalignedbuffer = malloc(size + ALIGN_BYTES);
    size_t currentaddress = (uint32_t)unalignedbuffer & 0x0000FFFF;
    size_t nextalignaddress = ((currentaddress | ALIGN_BITMASK) + 1);
    alignedbuffer = (unalignedbuffer + (nextalignaddress - currentaddress));

    return alignedbuffer;
}
#endif 

void *alloc_aligned_buffer(uint32_t size) {

    void *ptr = NULL;

    if (size) {
        // Alloc the buffer using the aligned version.
#ifdef IS64BIT 
        ptr = malloc(size);
#else
        ptr = malloc_aligned(size);
#endif
    }

    return ptr;
}

void free_request(struct ata_ioc_request *rq)
{
    if (rq->data) {
#ifdef IS64BIT
    free(rq->data);
#else
    free_aligned();
#endif
    }
    free(rq);
}

struct ata_ioc_request* build_basic_command(uint32_t lba, uint32_t cnt, 
    uint8_t feat,uint8_t comm, uint32_t op, uint32_t size) {

    struct ata_ioc_request *rq = NULL;

    rq = malloc(sizeof(struct ata_ioc_request));
    memset(rq, 0, sizeof(*rq));
    rq->timeout = 30;
    rq->u.ata.lba = lba;
    rq->u.ata.count = cnt;
    rq->u.ata.feature = feat;
    rq->u.ata.command = comm;
    rq->count = size;
    rq->flags = ATA_CMD_CONTROL;
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

int validate_card(uint8_t *data) {

    int i, valmfg = 0;

    for (i=54; i<69; i+=2) {
        if(data[i+1] != mfgname[i-54]) valmfg = 1;
        if(data[i] != mfgname[i-53]) valmfg = 1;
    }

    // Not the right type, just return.
    if (valmfg == 1) {
        return -1;
    }

    return 0;
}

int validate_mfg_card(int fd) {

    struct ata_ioc_request *req;
    int ret = 0;

    req = build_basic_command(0, 1, 0, ATA_ATA_IDENTIFY, ATA_CMD_READ, SECTOR_SIZE);
    ret = execute_command(req, fd);

    if (ret)
        err(1, "Validate mfg execute command failed.%d %d ",ret, errno);

    ret = validate_card(req->data);
    free_request(req);

    return ret;
}

int check_anchor_block(char *device, uint8_t *data) {
    
    printf("\nMaxBlockEraseCount = 0x%2.2x%2.2x%2.2x%2.2x \n",data[100] & 0xff,data[101] & 0xff ,data[102] & 0xff,data[103] & 0xff );

    if(((data[100] & 0xff) == 0x00)
         && ((data[101] & 0xff) == 0x00)
         && ((data[102] & 0xff) == 0x00)
         && ((data[103] & 0xff) == 0x00))
    {
        show_details_disk(device);
        return 0;
    }

    noupdate_details_disk(device);

    return 1;
}

int read_anchor_block(char *device, int fd) {
    struct ata_ioc_request *req;
    int ret = 0;

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

    ret = check_anchor_block(device, req->data);
    // Free the request before we head back.
    free_request(req);

    return ret;
}

void prepare_write_buffer(uint8_t *data, int reset) {

    SAVE_U32(data, MASK_OFFSET, 100, 0xFFFFFFFF);
 
    if(!reset)
    {
       SAVE_U32(data, DATA_OFFSET, 100, 100000);
       SAVE_U32(data, DATA_OFFSET, 140, 100000);
       SAVE_U32(data, DATA_OFFSET, 144, 100000);
       SAVE_U32(data, DATA_OFFSET, 148, 100000);
    }
}

int fix_anchor_block(int fd, int reset) {
    struct ata_ioc_request *req;
    int ret = 0;

    req = build_basic_command(0, 0x08, 0x07, 0x92, ATA_CMD_WRITE, 8192);
    // Prepare the write buffer.
    prepare_write_buffer(req->data, reset);

    ret = execute_command(req, fd);

    free_request(req);

    return ret;
}

int execute_command(struct ata_ioc_request *req, int fd) {
    int ret = 0;

    ret = ioctl(fd, SYSTEM_CODE, req);

    return ret;
}


int reset_channel(int fd) {
    struct ata_ioc_request *req;
    int ret = 0;

    req = build_basic_command(0, 0x03, 0, 0xc2, 0, 0);
    ret = execute_command(req, fd);
    free_request(req);

    return ret;
}

char *compute_disks() {

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
show_details_disk(char *disk)
{
    fprintf(stdout, " The disk :%s requires an update. \n",disk);
}

void 
noupdate_details_disk(char *disk)
{
    fprintf(stdout, " The disk :%s doesnt need an update. ",disk);
    fprintf(stdout, "\n");
}

int main(int argc, char **argv)
{
    char *disklist;
    char *disk, ch;
    int reset = 0, disk_cnt, ret, fd, read = 0;

    if (argc < 2) {
         usage();
    }

    while ((ch = getopt(argc, argv, "rwf")) != -1) 
                switch (ch) {
                case 'r':
                        read = 1;
                        break;
                case 'w':
                        read = 0;
                        reset = 0;
                        break;
		case 'f':
                        read = 0;
			reset = 1;
			break;
                case '?':
                default:
                        usage();
                        /* NOTREACHED */
                }

    // Disks_computed.
    disklist = compute_disks();

    for (disk_cnt = 0; disk_cnt < MAX_NO_DISKS; disk_cnt++) {
        char device[64];

        disk = strsep(&disklist, " ");
        if (disk == NULL) 
            break;

        sprintf(device, "/dev/%s", disk);

        if ((fd = open(device, O_RDONLY)) < 0)
             err(1, "device not found");

        // If mfg is different or only reading move on.
        if((ret = validate_mfg_card(fd))) {
            close(fd);
            continue;
        }

        if((ret = read_anchor_block(device, fd)) < 0) {
            err(1, "Error returned by Anchor Block Read .exit ! \n");
            exit(0);
        }
 
        // Read succeeded but decide the fate based on other flags.
        if (read) {
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

        if((ret = read_anchor_block(device, fd))) {
            printf("Set to correct value .\n");
        }
        close(fd);
   }
   exit(0);
}
