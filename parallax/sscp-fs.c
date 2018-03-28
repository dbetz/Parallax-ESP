#include <esp8266.h>
#include "sscp.h"
#include "roffs.h"
#include "proploader.h"

// FINFO,n
void ICACHE_FLASH_ATTR fs_do_finfo(int argc, char *argv[])
{
    char fileName[128];
    int fileSize;
    
    if (argc != 2) {
        sscp_sendResponse("E,%d", SSCP_ERROR_WRONG_ARGUMENT_COUNT);
        return;
    }
    
    if (roffs_fileinfo(atoi(argv[1]), fileName, &fileSize) == 0) {
        sscp_sendResponse("S,%s,%d", fileName, fileSize);
        return;
    }

    sscp_sendResponse("N,,0");
}

// FCOUNT
void ICACHE_FLASH_ATTR fs_do_fcount(int argc, char *argv[])
{
    int count;
    
    if (argc != 1) {
        sscp_sendResponse("E,%d", SSCP_ERROR_WRONG_ARGUMENT_COUNT);
        return;
    }
    
    if (roffs_filecount(&count) == 0) {
        sscp_sendResponse("S,%d", count);
        return;
    }

    sscp_sendResponse("N,0");
}

// FRUN,name
void ICACHE_FLASH_ATTR fs_do_frun(int argc, char *argv[])
{
    int sts;
    
    if (argc != 2) {
        sscp_sendResponse("E,%d", SSCP_ERROR_WRONG_ARGUMENT_COUNT);
        return;
    }
    
    if ((sts = loadFile(argv[1])) == lsOK) {
        sscp_sendResponse("S,0");
        return;
    }

    sscp_sendResponse("N,%d", sts);
}

