#include <stdio.h>
#include <string.h> // For memset, memcpy
#include <stdlib.h> // For exit, atoi (if used)
#include <stdbool.h>
#include <time.h>
#include <signal.h>

#include "udpbd_c.h" // Contains UDPBDServer struct and prototypes, BlockDevice too

// BUFLEN needs to be defined, it was in main.cpp
#define BUFLEN 2048

// Struct and array for block size statistics
struct BlockSizeStat {
    uint32_t block_size;
    uint64_t count;
};
struct BlockSizeStat block_size_stats_data[32];
int num_block_size_stats = 0;

// Global variables for statistics
uint64_t total_bytes_transferred = 0;
double total_request_handling_time = 0.0;

// Platform-specific socket code was in main.cpp, needs to be here or in udpbd_c.h
// udpbd_c.h already includes winsock2.h or sys/socket.h etc.

// Macros from main.cpp for platform abstraction (SENDTO, SETSOCKOPT)
#if defined(_WIN32)
    #define SENDTO_IMPL(s, buf, len, flags, addr, addrlen) sendto(s, (const char*)(buf), len, flags, addr, addrlen)
    #define SETSOCKOPT_IMPL(s, lvl, opt, val, vlen) setsockopt(s, lvl, opt, (char*)(val), vlen)
    #define CLOSE_SOCKET(s) closesocket(s)
    #define SOCK_ERRNO WSAGetLastError()
#else // POSIX
    #include <errno.h> // For errno
    #define SENDTO_IMPL(s, buf, len, flags, addr, addrlen) sendto(s, buf, len, flags, addr, addrlen)
    #define SETSOCKOPT_IMPL(s, lvl, opt, val, vlen) setsockopt(s, lvl, opt, val, vlen)
    #define CLOSE_SOCKET(s) close(s)
    #define SOCK_ERRNO errno
#endif

// Forward declarations for static helper functions
static void UDPBDServer_set_block_shift(UDPBDServer *srv, uint32_t shift);
static void handle_sigusr1(int sig);
static void UDPBDServer_set_block_shift_sectors(UDPBDServer *srv, uint32_t sectors);
static void update_block_size_stats(uint32_t block_size, uint32_t num_blocks);

// Error reporting helper
static int report_error(const char* context, const char* message) {
    fprintf(stderr, "UDPBDServer Error: %s: %s (errno: %d)\n", context, message, SOCK_ERRNO);
    return -1; // Generic error code
}
static int report_error_simple(const char* message) {
    fprintf(stderr, "UDPBDServer Error: %s\n", message);
    return -1;
}


int UDPBDServer_init(UDPBDServer *srv, const char *sFileName) {
    if (!srv || !sFileName) return -1;

    // Initialize BlockDevice
    if (BlockDevice_init(&srv->_bd, sFileName) != 0) {
        // Error already printed by BlockDevice_init
        return report_error_simple("BlockDevice initialization failed");
    }

    srv->_total_read = 0;
    srv->_total_write = 0;
    srv->_write_size_left = 0; // Initialize
    srv->_block_shift = 0; // Initialize to ensure set_block_shift runs

    // Set initial block shift (default from original CUDPBDServer constructor)
    UDPBDServer_set_block_shift(srv, 5); // 128b blocks default

    struct sockaddr_in si_me;

#if defined(_WIN32)
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
        return report_error("WSAStartup", "Failed");
    }
#endif

    // Create a UDP socket
#if defined(_WIN32)
    if ((srv->s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == INVALID_SOCKET) {
        BlockDevice_destroy(&srv->_bd); // Clean up block device
        #if defined(_WIN32)
        WSACleanup();
        #endif
        return report_error("socket", "Failed to create socket");
    }
#else
    if ((srv->s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
        BlockDevice_destroy(&srv->_bd);
        return report_error("socket", "Failed to create socket");
    }
#endif

    // Bind socket to port
    memset((char *) &si_me, 0, sizeof(si_me));
    si_me.sin_family = AF_INET;
    si_me.sin_port = htons(UDPBD_PORT);
    si_me.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(srv->s, (struct sockaddr*)&si_me, sizeof(si_me)) == -1) {
        CLOSE_SOCKET(srv->s);
        BlockDevice_destroy(&srv->_bd);
#if defined(_WIN32)
        WSACleanup();
#endif
        return report_error("bind", "Failed to bind socket");
    }

    // Enable broadcasts
    int broadcastEnable = 1;
    if (SETSOCKOPT_IMPL(srv->s, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable)) == -1) {
        CLOSE_SOCKET(srv->s);
        BlockDevice_destroy(&srv->_bd);
#if defined(_WIN32)
        WSACleanup();
#endif
        return report_error("setsockopt SO_BROADCAST", "Failed");
    }

    signal(SIGUSR1, handle_sigusr1); // Register signal handler
    return 0; // Success
}

void UDPBDServer_destroy(UDPBDServer *srv) {
    if (!srv) return;

    BlockDevice_destroy(&srv->_bd); // Clean up the block device

    if (srv->s != -1 // For POSIX
#if defined(_WIN32)
        && srv->s != INVALID_SOCKET
#endif
    ) {
        CLOSE_SOCKET(srv->s);
    }

#if defined(_WIN32)
    WSACleanup();
#endif
    // No other dynamic memory directly in UDPBDServer struct for now
}

int UDPBDServer_run(UDPBDServer *srv) {
    if (!srv) return -1;

    struct sockaddr_in si_other;
    socklen_t slen = sizeof(si_other);
    int recv_len;
    char buf[BUFLEN]; // Buffer for incoming data

    printf("Server running on port %d (0x%x)\n", UDPBD_PORT, UDPBD_PORT);

    while (1) {
        struct timespec start_time, end_time;
        // Receive command
        recv_len = recvfrom(srv->s, buf, BUFLEN, 0, (struct sockaddr *) &si_other, &slen);
#if defined(_WIN32)
        if (recv_len == SOCKET_ERROR) {
#else
        if (recv_len == -1) {
#endif
            report_error("recvfrom", "Error receiving data");
            // Potentially break or return depending on error (e.g. WSAECONNRESET)
            // For now, continue might be problematic if error is persistent
            // Original code would throw, effectively stopping.
            return -1;
        }
        if (recv_len < (int)sizeof(struct SUDPBDv2_Header)) {
            fprintf(stderr, "Received packet too small (%d bytes)\n", recv_len);
            continue; // Ignore malformed/short packet
        }
        clock_gettime(CLOCK_MONOTONIC, &start_time);

        struct SUDPBDv2_Header *hdr = (struct SUDPBDv2_Header *)buf;
        int cmd_result = 0;

        // Process command
        switch (hdr->cmd) {
            case UDPBD_CMD_INFO:
                cmd_result = UDPBDServer_handle_cmd_info(srv, &si_other, slen, (struct SUDPBDv2_InfoRequest *)buf);
                break;
            case UDPBD_CMD_READ:
                cmd_result = UDPBDServer_handle_cmd_read(srv, &si_other, slen, (struct SUDPBDv2_RWRequest *)buf);
                break;
            case UDPBD_CMD_WRITE:
                cmd_result = UDPBDServer_handle_cmd_write(srv, &si_other, slen, (struct SUDPBDv2_RWRequest *)buf);
                break;
            case UDPBD_CMD_WRITE_RDMA:
                cmd_result = UDPBDServer_handle_cmd_write_rdma(srv, &si_other, slen, (struct SUDPBDv2_RDMA *)buf);
                break;
            default:
                fprintf(stderr, "Invalid cmd: 0x%x\n", hdr->cmd);
                // No specific error return, just ignore unknown command
        };

        if (cmd_result != 0) {
            // Error already printed by handler
            // Decide if server should stop on handler error. Original threw, stopping that request path.
            // For now, log and continue serving other requests.
            fprintf(stderr, "Error processing command 0x%x\n", hdr->cmd);
        }
        clock_gettime(CLOCK_MONOTONIC, &end_time);
        double elapsed_time = (end_time.tv_sec - start_time.tv_sec) + (end_time.tv_nsec - start_time.tv_nsec) / 1e9;
        total_request_handling_time += elapsed_time;
    }
    return 0; // Should not be reached in normal operation of this server
}


// --- Static Helper Functions ---

static void update_block_size_stats(uint32_t block_size, uint32_t num_blocks) {
    for (int i = 0; i < num_block_size_stats; ++i) {
        if (block_size_stats_data[i].block_size == block_size) {
            block_size_stats_data[i].count += num_blocks;
            return;
        }
    }

    if (num_block_size_stats < 32) {
        block_size_stats_data[num_block_size_stats].block_size = block_size;
        block_size_stats_data[num_block_size_stats].count = num_blocks;
        num_block_size_stats++;
    } else {
        fprintf(stderr, "Warning: Maximum number of unique block sizes reached. Cannot track new block size: %u\n", block_size);
    }
}

static void handle_sigusr1(int sig) {
    (void)sig; // Unused parameter

    double transfer_speed = 0.0;
    if (total_request_handling_time > 0.000001) {
        transfer_speed = (double)total_bytes_transferred / total_request_handling_time;
    }

    printf("Total bytes transferred: %llu B\n", (unsigned long long)total_bytes_transferred);
    printf("Total request handling time: %f s\n", total_request_handling_time);
    printf("Average transfer speed: %f B/s\n", transfer_speed);

    printf("Block size frequencies:\n");
    for (int i = 0; i < num_block_size_stats; ++i) {
        printf("  - Size: %u B, Count: %llu\n",
               block_size_stats_data[i].block_size,
               (unsigned long long)block_size_stats_data[i].count);
    }
    printf("--- End of Statistics ---\n");
    fflush(stdout);
}

static void UDPBDServer_set_block_shift(UDPBDServer *srv, uint32_t shift) {
    if (shift != srv->_block_shift) {
        srv->_block_shift       = shift;
        srv->_block_size        = 1 << (srv->_block_shift + 2);
        // RDMA_MAX_PAYLOAD is from udpbd.h
        if (srv->_block_size == 0) { // Prevent division by zero
             fprintf(stderr, "Error: Calculated block size is zero for shift %u. Clamping to 1.\n", shift);
             srv->_block_size = 1; // Should not happen with valid shifts
        }
        srv->_blocks_per_packet = RDMA_MAX_PAYLOAD / srv->_block_size;
        if (BlockDevice_get_sector_size(&srv->_bd) == 0 || srv->_block_size == 0) {
            fprintf(stderr, "Error: Sector size or block size is zero, cannot calculate blocks_per_sector.\n");
            srv->_blocks_per_sector = 0; // Or handle error appropriately
        } else {
            srv->_blocks_per_sector = BlockDevice_get_sector_size(&srv->_bd) / srv->_block_size;
        }
    }
}

static void UDPBDServer_set_block_shift_sectors(UDPBDServer *srv, uint32_t sectors) {
    uint32_t device_sector_size = BlockDevice_get_sector_size(&srv->_bd);
    if (device_sector_size == 0) {
        fprintf(stderr, "Error: Device sector size is 0. Cannot set block shift by sectors.\n");
        return;
    }
    uint32_t size = sectors * device_sector_size;

    static const struct BlockConfig {
        int shift_val;
        uint32_t effective_payload; // product of block_size * blocks_per_rdma_packet
    } block_configs[] = {
        {3, 32U * 45},  // 32-byte blocks
        {4, 64U * 22},  // 64-byte blocks
        {5, 128U * 11}, // 128-byte blocks
        {6, 256U * 5},  // 256-byte blocks
        {7, 512U * 2}   // 512-byte blocks
    };
    int num_configs = sizeof(block_configs) / sizeof(block_configs[0]);

    uint32_t packetsMIN = (uint32_t)-1;
    uint32_t chosen_shift = 3; // Default to shift 3 (smallest block size) as a fallback.

    uint32_t packet_counts[sizeof(block_configs) / sizeof(block_configs[0])]; // C99 VLA, or use num_configs

    // First pass: Calculate packet counts for all configurations and find the true packetsMIN.
    for (int i = 0; i < num_configs; ++i) {
        if (block_configs[i].effective_payload == 0) {
            packet_counts[i] = (uint32_t)-1; // Mark as unusable
        } else {
            packet_counts[i] = (size + block_configs[i].effective_payload - 1) / block_configs[i].effective_payload;
        }

        if (packet_counts[i] < packetsMIN) {
            packetsMIN = packet_counts[i];
        }
    }

    // Handle the error case where no valid configuration could be found
    // (Note: If size == 0, packetsMIN will be 0, so this condition won't be met, which is fine.)
    if (packetsMIN == (uint32_t)-1 && size > 0) {
        fprintf(stderr, "Error: Could not determine a valid block configuration for size %u\n", size);
        UDPBDServer_set_block_shift(srv, srv->_block_shift); // Re-set with current (likely last valid) shift
        return;
    }

    // Second pass: Iterate downwards from the largest block size to find the preferred shift.
    // The chosen_shift is already defaulted to 3. This loop will update it if a larger block size achieves packetsMIN.
    for (int i = num_configs - 1; i >= 0; --i) {
        if (block_configs[i].effective_payload > 0 && packet_counts[i] == packetsMIN) {
            chosen_shift = block_configs[i].shift_val;
            break; // Found the largest block size that achieves packetsMIN
        }
    }

    UDPBDServer_set_block_shift(srv, chosen_shift);
}


// --- Command Handlers ---

int UDPBDServer_handle_cmd_info(UDPBDServer *srv, struct sockaddr_in *si_other, socklen_t slen, struct SUDPBDv2_InfoRequest *request) {
    struct SUDPBDv2_InfoReply reply;
    char client_ip_str[INET_ADDRSTRLEN];

#if defined(_WIN32)
    char *temp_str = inet_ntoa(si_other->sin_addr);
    if (temp_str != NULL) {
        strncpy(client_ip_str, temp_str, INET_ADDRSTRLEN);
        client_ip_str[INET_ADDRSTRLEN - 1] = '\0';
    } else {
        strcpy(client_ip_str, "UNKNOWN_IP");
    }
#else
    if (inet_ntop(AF_INET, &(si_other->sin_addr), client_ip_str, INET_ADDRSTRLEN) == NULL) {
        perror("inet_ntop failed");
        strcpy(client_ip_str, "UNKNOWN_IP");
    }
#endif

    reply.hdr.cmd      = UDPBD_CMD_INFO_REPLY;
    reply.hdr.cmdid    = request->hdr.cmdid;
    reply.hdr.cmdpkt   = 1;
    reply.sector_size  = BlockDevice_get_sector_size(&srv->_bd);
    reply.sector_count = BlockDevice_get_sector_count(&srv->_bd);

    if (SENDTO_IMPL(srv->s, &reply, sizeof(reply), 0, (struct sockaddr*) si_other, slen) == -1) {
        return report_error("sendto (INFO_REPLY)", "Failed to send reply");
    }
    return 0;
}

int UDPBDServer_handle_cmd_read(UDPBDServer *srv, struct sockaddr_in *si_other, socklen_t slen, struct SUDPBDv2_RWRequest *request) {
    struct SUDPBDv2_RDMA reply;

    if (request->sector_count == 0) {
        printf("Read request for 0 sectors, doing nothing.\n");
        return 0;
    }

    UDPBDServer_set_block_shift_sectors(srv, request->sector_count);

    reply.hdr.cmd        = UDPBD_CMD_READ_RDMA;
    reply.hdr.cmdid      = request->hdr.cmdid;
    reply.hdr.cmdpkt     = 1;
    reply.bt.block_shift = srv->_block_shift;

    uint32_t total_sectors_to_read = request->sector_count;
    uint32_t device_sector_size = BlockDevice_get_sector_size(&srv->_bd);

    if (srv->_block_size == 0 || device_sector_size == 0) {
         return report_error_simple("Block size or device sector size is zero, cannot proceed with read.");
    }
    if (device_sector_size % srv->_block_size != 0) {
        return report_error_simple("Device sector size is not a multiple of block size.");
    }
    uint32_t blocks_per_device_sector = device_sector_size / srv->_block_size;
    uint32_t blocks_left = total_sectors_to_read * blocks_per_device_sector;

    srv->_total_read += (uint64_t)blocks_left * srv->_block_size;
    total_bytes_transferred += (uint64_t)blocks_left * srv->_block_size;
    update_block_size_stats(srv->_block_size, blocks_left);

    BlockDevice_seek(&srv->_bd, request->sector_nr);

    while (blocks_left > 0) {
        reply.bt.block_count = (blocks_left > srv->_blocks_per_packet) ? srv->_blocks_per_packet : blocks_left;

        size_t current_read_size = (size_t)reply.bt.block_count * srv->_block_size;

        if (current_read_size > RDMA_MAX_PAYLOAD) {
             return report_error_simple("Calculated read size exceeds RDMA_MAX_PAYLOAD");
        }

        ssize_t bytes_read = BlockDevice_read(&srv->_bd, reply.data, current_read_size);
        if (bytes_read != (ssize_t)current_read_size) {
            fprintf(stderr, "BlockDevice_read error during CMD_READ. Expected %zu, got %zd\n", current_read_size, bytes_read);
            return -1;
        }

        size_t packet_size = sizeof(struct SUDPBDv2_Header) + sizeof(union block_type) + current_read_size;

        if (SENDTO_IMPL(srv->s, &reply, packet_size, 0, (struct sockaddr*) si_other, slen) == -1) {
            return report_error("sendto (READ_RDMA)", "Failed to send data packet");
        }

        blocks_left -= reply.bt.block_count;
        reply.hdr.cmdpkt++;
    }
    return 0;
}

int UDPBDServer_handle_cmd_write(UDPBDServer *srv, struct sockaddr_in *si_other, socklen_t slen, struct SUDPBDv2_RWRequest *request) {
    (void)si_other;
    (void)slen;

    if (BlockDevice_is_readonly(&srv->_bd)) {
        fprintf(stderr, "Error: Write command received but block device is read-only.\n");
        // Consider sending NACK based on protocol design if client expects one for WRITE setup.
        // Original doesn't send reply here, failure is deferred to WRITE_RDMA.
        return -1; // Indicate error locally.
    }

    if (request->sector_count == 0) {
        printf("Write request for 0 sectors. Preparing for 0 byte write.\n");
        srv->_write_size_left = 0;
        // Optionally send WRITE_DONE immediately if that's desired protocol behavior.
        // For now, strictly follow original flow: client must send (empty) RDMA or server times out.
        // To make it robust, if sector_count is 0, could send WRITE_DONE(result=0)
        struct SUDPBDv2_WriteDone reply;
        reply.hdr.cmd    = UDPBD_CMD_WRITE_DONE;
        reply.hdr.cmdid  = request->hdr.cmdid;
        reply.hdr.cmdpkt = 1;
        reply.result     = 0; // Success for 0 bytes
        if (SENDTO_IMPL(srv->s, &reply, sizeof(reply), 0, (struct sockaddr*) si_other, slen) == -1) {
            return report_error("sendto (WRITE_DONE for 0 sectors)", "Failed to send completion reply");
        }
        return 0; // Successfully handled 0-sector write.
    }

    BlockDevice_seek(&srv->_bd, request->sector_nr);

    uint32_t device_sector_size = BlockDevice_get_sector_size(&srv->_bd);
    if (device_sector_size == 0) {
        return report_error_simple("Device sector size is 0, cannot prepare for write.");
    }
    srv->_write_size_left = request->sector_count * device_sector_size;

    srv->_total_write += srv->_write_size_left;
    return 0;
}

int UDPBDServer_handle_cmd_write_rdma(UDPBDServer *srv, struct sockaddr_in *si_other, socklen_t slen, struct SUDPBDv2_RDMA *request) {
    uint32_t current_block_size = 1 << (request->bt.block_shift + 2);
    size_t data_size = (size_t)request->bt.block_count * current_block_size;

    update_block_size_stats(current_block_size, request->bt.block_count);

    if (BlockDevice_is_readonly(&srv->_bd)) {
        fprintf(stderr, "Error: Write RDMA command received but block device is read-only.\n");
        struct SUDPBDv2_WriteDone reply;
        reply.hdr.cmd    = UDPBD_CMD_WRITE_DONE;
        reply.hdr.cmdid  = request->hdr.cmdid;
        reply.hdr.cmdpkt = request->hdr.cmdpkt;
        reply.result     = -1; // Generic error, e.g. EACCES
        SENDTO_IMPL(srv->s, &reply, sizeof(reply), 0, (struct sockaddr*) si_other, slen);
        return -1;
    }

    if (data_size == 0 && request->bt.block_count == 0) { // Client signals end of zero-sector write
        if (srv->_write_size_left == 0) {
             // This might be the "empty" RDMA packet client sends after a 0-sector write cmd
            struct SUDPBDv2_WriteDone reply;
            reply.hdr.cmd    = UDPBD_CMD_WRITE_DONE;
            reply.hdr.cmdid  = request->hdr.cmdid;
            reply.hdr.cmdpkt = 1; // Final reply
            reply.result     = 0; // Success for 0 bytes written
            if (SENDTO_IMPL(srv->s, &reply, sizeof(reply), 0, (struct sockaddr*) si_other, slen) == -1) {
                return report_error("sendto (WRITE_DONE for 0-data RDMA)", "Failed to send completion reply");
            }
            return 0;
        } else {
            fprintf(stderr, "Warning: Received 0-data RDMA, but server expected %u more bytes.\n", srv->_write_size_left);
            // Proceed to write 0 bytes, _write_size_left will remain, potentially leading to timeout or error later.
        }
    }

    if (data_size > RDMA_MAX_PAYLOAD) {
        fprintf(stderr, "Error: WRITE_RDMA data_size (%zu) exceeds RDMA_MAX_PAYLOAD (%u).\n", data_size, (unsigned int)RDMA_MAX_PAYLOAD);
        // Don't send reply here as packet is malformed. Client should time out.
        return -1;
    }

    // Check if client is trying to write more than initially announced by UDPBD_CMD_WRITE
    // This check was missing in the original logic, but it's good practice.
    if (data_size > srv->_write_size_left) {
        fprintf(stderr, "Error: Received more data (%zu bytes) for WRITE_RDMA than expected (%u bytes left for cmdId %d).\n",
                data_size, srv->_write_size_left, request->hdr.cmdid);
        // Original server would write it. For stricter handling, one might cap or error.
        // For now, let's write it but log verbosely. The _write_size_left will go to 0 or negative.
        // If strict adherence to _write_size_left is desired:
        // data_size = srv->_write_size_left; // Cap write to remaining expected.
        // if (data_size == 0) { /* ... handle if nothing left to write ... */ }
    }


    ssize_t bytes_written = BlockDevice_write(&srv->_bd, request->data, data_size);

    if (bytes_written != (ssize_t)data_size) {
        fprintf(stderr, "BlockDevice_write error during CMD_WRITE_RDMA. Expected %zu, got %zd\n", data_size, bytes_written);
        struct SUDPBDv2_WriteDone reply;
        reply.hdr.cmd    = UDPBD_CMD_WRITE_DONE;
        reply.hdr.cmdid  = request->hdr.cmdid;
        reply.hdr.cmdpkt = request->hdr.cmdpkt;
        reply.result     = (bytes_written == -1) ? SOCK_ERRNO : -100; // Map errno or use generic write error
        SENDTO_IMPL(srv->s, &reply, sizeof(reply), 0, (struct sockaddr*) si_other, slen);
        return -1;
    }
    total_bytes_transferred += bytes_written;

    if (srv->_write_size_left >= data_size) {
        srv->_write_size_left -= data_size;
    } else {
        fprintf(stderr, "Warning: WRITE_RDMA data_size %zu exceeded remaining _write_size_left %u. Setting remaining to 0.\n", data_size, srv->_write_size_left);
        srv->_write_size_left = 0;
    }

    if (srv->_write_size_left == 0) {
        struct SUDPBDv2_WriteDone reply;
        reply.hdr.cmd    = UDPBD_CMD_WRITE_DONE;
        reply.hdr.cmdid  = request->hdr.cmdid;
        reply.hdr.cmdpkt = 1; // Final reply packet for this WRITE sequence.
        reply.result     = 0; // Success

        if (SENDTO_IMPL(srv->s, &reply, sizeof(reply), 0, (struct sockaddr*) si_other, slen) == -1) {
            return report_error("sendto (WRITE_DONE)", "Failed to send completion reply");
        }
    }
    return 0;
}
