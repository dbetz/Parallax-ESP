#include <esp8266.h>
#include "roffs.h"

#ifdef SPIFFS

#include "spiffs.h"

static u8_t spiffs_work_buf[SPIFFS_CFG_LOG_PAGE_SZ(x)*2];
static u8_t spiffs_fds[4*32];
static spiffs fs;

int ICACHE_FLASH_ATTR roffs_mount(uint32_t flashAddress)
{
    spiffs_config cfg;
    s32_t spiffs_hal_read(u32_t addr, u32_t size, u8_t *dst);
    s32_t spiffs_hal_write(u32_t addr, u32_t size, u8_t *src);
    s32_t spiffs_hal_erase(u32_t addr, u32_t size);
    cfg.hal_read_f = spiffs_hal_read;
    cfg.hal_write_f = spiffs_hal_write;
    cfg.hal_erase_f = spiffs_hal_erase;
    int res = SPIFFS_mount(&fs,
                           &cfg,
                           spiffs_work_buf,
                           spiffs_fds, sizeof(spiffs_fds),
                           NULL, 0,
                           NULL);
    if (res != SPIFFS_OK) {
        os_printf("Formatting flash filesystem\n");
        res = SPIFFS_format(&fs);
        if (res == SPIFFS_OK) {
            res = SPIFFS_mount(&fs,
                              &cfg,
                              spiffs_work_buf,
                              spiffs_fds, sizeof(spiffs_fds),
                              NULL, 0,
                              NULL);
        }
    }

    return res == SPIFFS_OK ? 0 : -1;
}

ROFFS_FILE ICACHE_FLASH_ATTR *roffs_open(const char *fileName)
{
    ROFFS_FILE *file;
    spiffs_file fd;
    if ((fd = SPIFFS_open(&fs, fileName, SPIFFS_O_RDONLY, 0)) < 0)
        return NULL;
    if (!(file = (ROFFS_FILE *)os_malloc(sizeof(ROFFS_FILE)))) {
        SPIFFS_close(&fs, fd);
        return NULL;
    }
    file->fd = fd;
    return file;
}

int ICACHE_FLASH_ATTR roffs_close(ROFFS_FILE *file)
{
    SPIFFS_close(&fs, file->fd);
    os_free(file);
    return 0;
}

int ICACHE_FLASH_ATTR roffs_file_size(ROFFS_FILE *file)
{
    spiffs_stat stat;
    if (SPIFFS_fstat(&fs, file->fd, &stat) != SPIFFS_OK)
        return -1;
    return stat.size;
}

int ICACHE_FLASH_ATTR roffs_file_flags(ROFFS_FILE *file)
{
    return 0; // no place to store this metadata
}

int ICACHE_FLASH_ATTR roffs_read(ROFFS_FILE *file, char *buf, int len)
{
    return SPIFFS_read(&fs, file->fd, buf, len);
}

ROFFS_FILE ICACHE_FLASH_ATTR *roffs_create(const char *fileName, int size)
{
    ROFFS_FILE *file;
    spiffs_file fd;
    if ((fd = SPIFFS_open(&fs, fileName, SPIFFS_O_CREAT | SPIFFS_O_WRONLY, 0)) < 0)
        return NULL;
    if (!(file = (ROFFS_FILE *)os_malloc(sizeof(ROFFS_FILE)))) {
        SPIFFS_close(&fs, fd);
        return NULL;
    }
    file->fd = fd;
    return file;
}

int ICACHE_FLASH_ATTR roffs_write(ROFFS_FILE *file, char *buf, int len)
{
    return SPIFFS_write(&fs, file->fd, buf, len);
}

#endif

