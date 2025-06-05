#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>  // For O_RDONLY, O_RDWR, open
// Define _FILE_OFFSET_BITS=64 before including sys/types.h or other headers
// to ensure off_t is 64-bit and lseek becomes lseek64 on Linux.
#if defined(__linux__)
    #define _FILE_OFFSET_BITS 64
#endif

#include <unistd.h> // For close, read, write, lseek (on POSIX)

#include "udpbd_c.h" // Contains BlockDevice struct definition and prototypes

// Platform-specific includes and defines for lseek64 and ioctl
#if defined(_WIN32)
    // Windows specific: _open, _read, _write, _close, _lseeki64 already mapped by MinGW usually
    // or need to be explicitly used. The original main.cpp uses open, read etc. via defines.
    // We will assume standard names and rely on toolchain/MinGW for mapping.
    // If direct _lseeki64 is needed, include appropriate headers.
    // For MSVC, io.h might be needed for _open, _close etc.
    // #include <io.h> // For _open, _close, _lseeki64 if not using MinGW defines
    #include <io.h> // For _O_BINARY
#elif defined(__APPLE__) || defined(__FreeBSD__)
    #include <sys/ioctl.h>
    #include <sys/disk.h> // For DKIOCGETBLOCKCOUNT, DIOCGMEDIASIZE
    // lseek64 is mapped to lseek for these platforms in original main.cpp
    #ifndef lseek64
        #define lseek64 lseek
    #endif
#else // Other POSIX (Linux)
    // lseek64 is generally available. If not, ensure _LARGEFILE64_SOURCE is defined.
    // For simplicity, assume it's available as lseek64, now handled by _FILE_OFFSET_BITS on Linux.
#endif

int BlockDevice_init(BlockDevice *bd, const char *sFileName) {
    if (!bd || !sFileName) {
        return -1; // Invalid arguments
    }

    bd->_read_only = false;
    bd->_fp = -1; // Initialize file descriptor

    // Open the selected file.
    // This file will be used as the Block Device
#if defined(_WIN32)
    // For Windows, _O_BINARY might be important if not default
    bd->_fp = open(sFileName, bd->_read_only ? O_RDONLY | _O_BINARY : O_RDWR | _O_BINARY);
#else
    bd->_fp = open(sFileName, bd->_read_only ? O_RDONLY : O_RDWR);
#endif

    if (bd->_fp < 0) {
        bd->_read_only = true; // Try opening as read-only
#if defined(_WIN32)
        bd->_fp = open(sFileName, bd->_read_only ? O_RDONLY | _O_BINARY : O_RDWR | _O_BINARY);
#else
        bd->_fp = open(sFileName, bd->_read_only ? O_RDONLY : O_RDWR);
#endif
        if (bd->_fp < 0) {
            // Original used: throw runtime_error(string("unable to open file ") + sFileName);
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), "Unable to open file: %s", sFileName);
            perror(error_msg); // perror adds system error message
            return -1;
        }
    }

    // Get the size of the file
    // On Linux with _FILE_OFFSET_BITS=64, lseek is lseek64.
    // On Apple/FreeBSD, lseek64 is defined to lseek.
    // On Windows, _lseeki64 should be used if loff_t is __int64,
    // but current loff_t definition for WIN32 is __int64 and open uses standard names
    // which MinGW might map. If lseek doesn't work for >2GB files on Win, _lseeki64 is needed.
    // For now, stick to lseek as per POSIX standard with _FILE_OFFSET_BITS.
    bd->_fsize = lseek(bd->_fp, 0, SEEK_END);
    if (bd->_fsize == (loff_t)-1) { // Error check for lseek
        perror("BlockDevice_init: lseek to SEEK_END failed");
        close(bd->_fp);
        bd->_fp = -1;
        return -1;
    }
    lseek(bd->_fp, 0, SEEK_SET); // Reset to start

#if defined(__APPLE__)
    if (bd->_fsize == 0) {
        uint64_t blockCount = 0; // Initialize to prevent uninitialized usage
        uint32_t blockSize = 0;  // Initialize
        if (ioctl(bd->_fp, DKIOCGETBLOCKCOUNT, &blockCount) == -1) {
            perror("BlockDevice_init: ioctl DKIOCGETBLOCKCOUNT failed");
            // Decide if this is fatal or if fsize 0 is acceptable
        }
        if (ioctl(bd->_fp, DKIOCGETBLOCKSIZE, &blockSize) == -1) {
            perror("BlockDevice_init: ioctl DKIOCGETBLOCKSIZE failed");
             // Decide if this is fatal
        }
        if (blockSize > 0) { // Avoid division by zero or multiplication by zero if ioctls failed
            bd->_fsize = blockCount * blockSize;
        } else if (blockCount > 0 && blockSize == 0) { // If block size is 0 but count > 0, this is odd.
             fprintf(stderr, "BlockDevice_init: Warning - DKIOCGETBLOCKSIZE returned 0, but blockCount is %llu\n", (unsigned long long)blockCount);
        }
    }
#elif defined(__FreeBSD__)
    if (bd->_fsize == 0) {
        uint64_t mediaSize = 0; // Initialize
        if (ioctl(bd->_fp, DIOCGMEDIASIZE, &mediaSize) == -1) {
            perror("BlockDevice_init: ioctl DIOCGMEDIASIZE failed");
            // Decide if this is fatal
        } else {
            bd->_fsize = mediaSize;
        }
    }
#endif

    printf("Opened '%s' as Block Device\n", sFileName);
    printf(" - %s\n", bd->_read_only ? "read-only" : "read/write");
    // Ensure _fsize is not negative before printing, though loff_t can be signed.
    // Size calculation should be careful about types to avoid overflow if _fsize is huge.
    if (bd->_fsize < 0) {
        fprintf(stderr, "Warning: File size is negative (%lld), this might indicate an error or very large file on 32-bit off_t systems.\n", (long long)bd->_fsize);
    }
    printf(" - size = %lldMB / %lldMiB\n", (long long)bd->_fsize / (1000*1000), (long long)bd->_fsize / (1024*1024));

#if defined(__linux__) || defined(__FreeBSD__) // Systems known to support posix_fadvise well
    if (bd->_fp >= 0) {
        // Advise for sequential access for the entire file.
        // offset = 0, len = 0 advises for the whole file.
        if (posix_fadvise(bd->_fp, 0, 0, POSIX_FADV_SEQUENTIAL) != 0) {
            // perror("BlockDevice_init: posix_fadvise POSIX_FADV_SEQUENTIAL failed");
            // Non-fatal error, so just print if desired, or ignore.
        }
    }
#endif

    return 0; // Success
}

void BlockDevice_destroy(BlockDevice *bd) {
    if (bd && bd->_fp >= 0) {
        close(bd->_fp);
        bd->_fp = -1; // Mark as closed
    }
    // No dynamic memory allocated directly by BlockDevice itself in this structure.
}

void BlockDevice_seek(BlockDevice *bd, uint32_t sector) {
    if (!bd || bd->_fp < 0) return;
    loff_t offset = (loff_t)sector * 512;
    //printf("seek %d * 512 = %ld\n", sector, offset); // Original comment
    lseek(bd->_fp, offset, SEEK_SET);
}

ssize_t BlockDevice_read(BlockDevice *bd, void *data, size_t size) {
    if (!bd || bd->_fp < 0 || !data) return -1; // Basic error check

#if defined(__linux__) || defined(__FreeBSD__)
    if (bd->_fp >= 0) {
        // Get current file offset. lseek with SEEK_CUR doesn't change the offset.
        loff_t current_offset = lseek(bd->_fp, 0, SEEK_CUR);
        if (current_offset != (loff_t)-1) {
            // Advise that we will need the data range [current_offset, current_offset + size -1].
            if (posix_fadvise(bd->_fp, current_offset, size, POSIX_FADV_WILLNEED) != 0) {
                // perror("BlockDevice_read: posix_fadvise POSIX_FADV_WILLNEED failed");
                // Non-fatal, ignore return value.
            }
        } else {
            // perror("BlockDevice_read: lseek for current_offset failed before posix_fadvise");
            // Failed to get current offset, so cannot advise accurately.
        }
    }
#endif

    ssize_t rv = read(bd->_fp, data, size);
    if (rv != (ssize_t)size) { // ssize_t vs size_t comparison
        // Original: printf("read error %ld != %ld\n", rv, size);
        fprintf(stderr, "BlockDevice_read: read error %zd != %zu\n", rv, size);
        if (rv == -1) {
            perror("BlockDevice_read failed");
        }
    }
    return rv;
}

ssize_t BlockDevice_write(BlockDevice *bd, const void *data, size_t size) {
    if (!bd || bd->_fp < 0 || !data) return -1; // Basic error check
    if (bd->_read_only) {
        fprintf(stderr, "BlockDevice_write: Attempted to write to a read-only device.\n");
        return -1; // Or some other error code like EACCES / EPERM
    }
    ssize_t rv = write(bd->_fp, data, size);
    //printf("write %ld\n", size); // Original comment
    if (rv != (ssize_t)size) {
        fprintf(stderr, "BlockDevice_write: write error %zd != %zu\n", rv, size);
         if (rv == -1) {
            perror("BlockDevice_write failed");
        }
    }
    return rv;
}

uint32_t BlockDevice_get_sector_size(BlockDevice *bd) {
    if (!bd) return 0; // Should not happen if used correctly
    return 512; // Fixed sector size as per original logic
}

uint32_t BlockDevice_get_sector_count(BlockDevice *bd) {
    if (!bd || bd->_fsize < 0 || BlockDevice_get_sector_size(bd) == 0) {
        return 0; // Avoid division by zero or negative size issues
    }
    return bd->_fsize / BlockDevice_get_sector_size(bd);
}

bool BlockDevice_is_readonly(BlockDevice *bd) {
    if (!bd) return true; // Default to safe side
    return bd->_read_only;
}
