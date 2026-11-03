/* SDK FatFs只操作内存副本；输出新镜像通过exclusive create保护原文件。
 * SDK FatFs mutates an in-memory copy only; exclusive output creation preserves originals. */
#include "ff.h"
#include "diskio.h"
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define VOLUME_BYTES 0x7e0000u
static uint8_t *image;
static size_t image_bytes;
static unsigned sector_bytes;
static bool writable;

DSTATUS disk_initialize(BYTE drive) { return drive || !image ? STA_NOINIT : (writable ? 0 : STA_PROTECT); }
DSTATUS disk_status(BYTE drive) { return disk_initialize(drive); }
static bool valid_range(LBA_t sector, UINT count) {
    return count && sector <= image_bytes / sector_bytes && count <= image_bytes / sector_bytes - sector;
}
DRESULT disk_read(BYTE drive, BYTE *buffer, LBA_t sector, UINT count) {
    if(drive || !buffer || !valid_range(sector,count))return RES_PARERR;
    memcpy(buffer,image+(size_t)sector*sector_bytes,(size_t)count*sector_bytes);return RES_OK;
}
DRESULT disk_write(BYTE drive,const BYTE *buffer,LBA_t sector,UINT count) {
    if(!writable)return RES_WRPRT;
    if(drive || !buffer || !valid_range(sector,count))return RES_PARERR;
    memcpy(image+(size_t)sector*sector_bytes,buffer,(size_t)count*sector_bytes);return RES_OK;
}
DRESULT disk_ioctl(BYTE drive,BYTE command,void *buffer) {
    if(drive)return RES_PARERR;
    if(command==CTRL_SYNC)return RES_OK;
    if(!buffer)return RES_PARERR;
    if(command==GET_SECTOR_COUNT){*(LBA_t*)buffer=(LBA_t)(image_bytes/sector_bytes);return RES_OK;}
    if(command==GET_SECTOR_SIZE){*(WORD*)buffer=(WORD)sector_bytes;return RES_OK;}
    if(command==GET_BLOCK_SIZE){*(DWORD*)buffer=1;return RES_OK;}
    return RES_PARERR;
}
static void fail(const char *operation,int code) {
    fprintf(stderr,"volume %s failed (%d)\n",operation,code);exit(2);
}
static void require(FRESULT result,const char *operation) { if(result!=FR_OK)fail(operation,result); }
static FILE *regular_read(const char *path) {
    struct stat info;
    if(lstat(path,&info)||!S_ISREG(info.st_mode))fail("regular-file check",errno);
    FILE *file=fopen(path,"rb");if(!file)fail("file open",errno);return file;
}
static void load_image(const char *path) {
    FILE *file=regular_read(path);if(fseek(file,0,SEEK_END))fail("image seek",errno);
    long bytes=ftell(file);if(bytes!=(long)VOLUME_BYTES)fail("image size",0);
    rewind(file);image_bytes=(size_t)bytes;image=malloc(image_bytes);if(!image)fail("allocation",0);
    if(fread(image,1,image_bytes,file)!=image_bytes||ferror(file))fail("image read",errno);
    if(fclose(file))fail("image close",errno);
    sector_bytes=(unsigned)image[11]|(unsigned)image[12]<<8;
    if(sector_bytes!=512&&sector_bytes!=1024&&sector_bytes!=2048&&sector_bytes!=4096)fail("sector size",0);
}
static void json_string(const char *value) {
    putchar('"');for(const unsigned char *p=(const unsigned char*)value;*p;p++) {
        if(*p=='"'||*p=='\\'){putchar('\\');putchar(*p);}
        else if(*p<32)printf("\\u%04x",*p);else putchar(*p);
    }putchar('"');
}
static bool first_record;
static void walk(const char *path,unsigned depth) {
    if(depth>32)fail("directory depth",0);
    FF_DIR directory;FILINFO info;require(f_opendir(&directory,path),"directory open");
    while(true) {
        require(f_readdir(&directory,&info),"directory read");if(!info.fname[0])break;
        if(!strcmp(info.fname,".")||!strcmp(info.fname,".."))continue;
        char child[4096];int n=snprintf(child,sizeof child,"%s/%s",path,info.fname);
        if(n<0||(size_t)n>=sizeof child)fail("path length",0);
        if(!first_record)putchar(',');first_record=false;
        printf("{\"path\":");json_string(child+2);
        printf(",\"size\":%llu,\"attributes\":%u,\"directory\":%s",(unsigned long long)info.fsize,info.fattrib,info.fattrib&AM_DIR?"true":"false");
        if(!(info.fattrib&AM_DIR)) {
            FIL file;require(f_open(&file,child,FA_READ),"inventory file open");
            printf(",\"first_cluster\":%u",(unsigned)file.obj.sclust);require(f_close(&file),"inventory file close");
        }
        putchar('}');if(info.fattrib&AM_DIR)walk(child,depth+1);
    }
    require(f_closedir(&directory),"directory close");
}
static void inventory(FATFS *filesystem) {
    DWORD free_clusters;FATFS *fs;require(f_getfree("0:",&free_clusters,&fs),"free space");
    printf("{\"sector_bytes\":%u,\"cluster_sectors\":%u,\"fat_type\":%u,\"free_clusters\":%u,\"free_bytes\":%llu,\"files\":[",sector_bytes,(unsigned)filesystem->csize,(unsigned)filesystem->fs_type,(unsigned)free_clusters,(unsigned long long)free_clusters*filesystem->csize*sector_bytes);
    first_record=true;walk("0:",0);puts("]}");
}
static void cat_file(const char *path) {
    char full[4096];int n=snprintf(full,sizeof full,"0:%s",path);
    if(n<0||(size_t)n>=sizeof full||path[0]!='/')fail("file path",0);
    FIL file;require(f_open(&file,full,FA_READ),"file read open");uint8_t data[16384];
    while(true){UINT count=0;require(f_read(&file,data,sizeof data,&count),"file read");if(!count)break;if(fwrite(data,1,count,stdout)!=count)fail("stdout write",errno);}
    require(f_close(&file),"file read close");
}
static void append_file(const char *pack) {
    FILE *source=regular_read(pack);FIL destination;
    // CREATE_NEW拒绝已存在文件；原卷始终只读，失败时不会生成输出镜像。
    // CREATE_NEW rejects existing files; the original remains read-only and failures create no image.
    require(f_open(&destination,"0:/MIMI.PAK",FA_WRITE|FA_CREATE_NEW),"CREATE_NEW MIMI.PAK");
    uint8_t data[16384];size_t bytes;
    while((bytes=fread(data,1,sizeof data,source))!=0) {
        UINT written=0;require(f_write(&destination,data,(UINT)bytes,&written),"file write");
        if(written!=bytes)fail("insufficient free space",0);
    }
    if(ferror(source))fail("pack read",errno);if(fclose(source))fail("pack close",errno);
    require(f_sync(&destination),"file sync");require(f_close(&destination),"file close");
}
static void save_new(const char *path) {
    int descriptor=open(path,O_WRONLY|O_CREAT|O_EXCL,0600);if(descriptor<0)fail("exclusive output create",errno);
    size_t done=0;while(done<image_bytes){ssize_t n=write(descriptor,image+done,image_bytes-done);if(n<=0){close(descriptor);unlink(path);fail("output write",errno);}done+=(size_t)n;}
    if(fsync(descriptor)){close(descriptor);unlink(path);fail("output sync",errno);}
    if(close(descriptor)){unlink(path);fail("output close",errno);}
}
int main(int argc,char **argv) {
    if(argc<3){fputs("usage: volume_tool inventory IMAGE | cat IMAGE /PATH | append OLD PACK NEW\n",stderr);return 2;}
    bool append=!strcmp(argv[1],"append");
    if((append&&argc!=5)||(!strcmp(argv[1],"cat")&&argc!=4)||(!strcmp(argv[1],"inventory")&&argc!=3))return 2;
    if(strcmp(argv[1],"inventory")&&strcmp(argv[1],"cat")&&!append)return 2;
    load_image(argv[2]);writable=append;FATFS filesystem;
    require(f_mount(&filesystem,"0:",1),"existing filesystem mount");
    if(!strcmp(argv[1],"inventory"))inventory(&filesystem);
    else if(!strcmp(argv[1],"cat"))cat_file(argv[3]);
    else append_file(argv[3]);
    require(f_mount(NULL,"0:",0),"filesystem unmount");
    if(append)save_new(argv[4]);free(image);return 0;
}
