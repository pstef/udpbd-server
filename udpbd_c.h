#ifndef UDPBD_C_H
#define UDPBD_C_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/types.h>

// Platform-specific definitions for loff_t and socket types/headers
#if defined(_WIN32)
    #include <winsock2.h>
    #include <ws2tcpip.h> // For socklen_t, sockaddr_in etc.
    #ifndef loff_t
        #define loff_t __int64
    #endif
    // SOCKET type is defined in winsock2.h
#else // POSIX systems
    #include <sys/socket.h> // For struct sockaddr_in, socklen_t
    #include <netinet/in.h> // For struct sockaddr_in on some systems
    #include <arpa/inet.h>  // For inet_ntop, inet_ntoa
    #include <unistd.h>     // For lseek, read, write, close

    #if defined(__APPLE__) || defined(__FreeBSD__)
        #ifndef loff_t // Ensure it's not already defined (e.g. by sys/types.h)
            #define loff_t off_t
        #endif
    #else
        // For other POSIX systems, prefer off_t if loff_t is not standard.
        // lseek64 might expect loff_t or off_t depending on _FILE_OFFSET_BITS.
        // Let's use off_t as a general type for file offsets on POSIX.
        #ifndef loff_t
            #define loff_t off_t
        #endif
    #endif
#endif

#include "udpbd.h" // For SUDPBDv2_Header etc.

// Forward declaration not strictly needed due to order, but good practice.
struct UDPBDServer_s;

/*
 * Structure for BlockDevice, equivalent to CBlockDevice
 */
typedef struct {
    int _fp; // File descriptor
    bool _read_only;
    loff_t _fsize;
} BlockDevice;

/*
 * Structure for UDPBDServer, equivalent to CUDPBDServer
 */
typedef struct UDPBDServer_s { // Define struct UDPBDServer_s
    BlockDevice _bd; // Composition: UDPBDServer contains a BlockDevice instance
    uint32_t _block_shift;
    uint32_t _block_size;
    uint32_t _blocks_per_packet;
    uint32_t _blocks_per_sector;

#if defined(_WIN32)
    SOCKET s; // Windows uses SOCKET type for socket descriptors
#else
    int s;    // POSIX uses int for socket descriptors
#endif

    uint64_t _total_read;
    uint64_t _total_write;
    uint32_t _write_size_left; // Used to track remaining data for a write operation
} UDPBDServer; // Typedef it to UDPBDServer

// --- Function Prototypes for BlockDevice ---

// Initializes the block device. Returns 0 on success, -1 on error.
int BlockDevice_init(BlockDevice *bd, const char *sFileName);

// Cleans up resources used by the block device.
void BlockDevice_destroy(BlockDevice *bd);

// Seeks to the specified sector.
void BlockDevice_seek(BlockDevice *bd, uint32_t sector);

// Reads data from the block device. Returns bytes read, or -1 on error.
ssize_t BlockDevice_read(BlockDevice *bd, void *data, size_t size);

// Writes data to the block device. Returns bytes written, or -1 on error.
ssize_t BlockDevice_write(BlockDevice *bd, const void *data, size_t size);

// Gets the sector size of the block device.
uint32_t BlockDevice_get_sector_size(BlockDevice *bd);

// Gets the total number of sectors in the block device.
uint32_t BlockDevice_get_sector_count(BlockDevice *bd);

// Checks if the block device is opened in read-only mode.
bool BlockDevice_is_readonly(BlockDevice *bd);

// --- Function Prototypes for UDPBDServer ---

// Initializes the UDPBD server. Requires a filename for the block device.
// Returns 0 on success, negative value on error.
int UDPBDServer_init(UDPBDServer *srv, const char *sFileName);

// Cleans up resources used by the UDPBD server.
void UDPBDServer_destroy(UDPBDServer *srv);

// Starts the server and enters the main processing loop.
// Returns 0 on normal exit (e.g. shutdown signal), negative value on error.
int UDPBDServer_run(UDPBDServer *srv);

// --- Command Handler Prototypes for UDPBDServer ---
// These functions handle specific commands received by the server.
// They take a pointer to the server state, client address information, and the request data.
// Return 0 on success, -1 on error.

int UDPBDServer_handle_cmd_info(UDPBDServer *srv, struct sockaddr_in *si_other, socklen_t slen, struct SUDPBDv2_InfoRequest *request);
int UDPBDServer_handle_cmd_read(UDPBDServer *srv, struct sockaddr_in *si_other, socklen_t slen, struct SUDPBDv2_RWRequest *request);
int UDPBDServer_handle_cmd_write(UDPBDServer *srv, struct sockaddr_in *si_other, socklen_t slen, struct SUDPBDv2_RWRequest *request);
int UDPBDServer_handle_cmd_write_rdma(UDPBDServer *srv, struct sockaddr_in *si_other, socklen_t slen, struct SUDPBDv2_RDMA *request);

#endif // UDPBD_C_H
