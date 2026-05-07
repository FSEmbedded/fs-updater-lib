#pragma once

#include <cstdint>

namespace fs {

/* fsheader structures */
struct fs_header_v0_0
{                            /* Size: 16 Bytes */
    char magic[4];           /* "FS" + two bytes operating system */
                             /* (e.g. "LX" for Linux) */
    uint32_t file_size_low;  /* Image size [31:0] */
    uint32_t file_size_high; /* Image size [63:32] */
    uint16_t flags;          /* See flags below */
    uint8_t padsize;         /* Number of padded bytes at end */
    uint8_t version;         /* Header version x.y:
                                 [7:4] major x, [3:0] minor y */
};

struct fs_header_v1_0
{                               /* Size: 64 bytes */
    struct fs_header_v0_0 info; /* Image info, see above */
    char type[16];              /* Image type, e.g. "U-BOOT" */
    union {
        char descr[32];   /* Description, null-terminated */
        uint8_t p8[32];   /* 8-bit parameters */
        uint16_t p16[16]; /* 16-bit parameters */
        uint32_t p32[8];  /* 32-bit parameters */
        uint64_t p64[4];  /* 64-bit parameters */
    } param;
};

} // namespace fs
