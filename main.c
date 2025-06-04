#include <stdio.h>  // For printf, fprintf, stderr
#include <stdlib.h> // For EXIT_SUCCESS, EXIT_FAILURE

// Include the main header for our C UDPBD server code
#include "udpbd_c.h"

// main.cpp had specific includes for platform definitions,
// many of which are now handled in udpbd_c.h or the specific .c files.
// For example, socket headers, fcntl.h, unistd.h, string.h are used by
// the implementation files. main.c itself should be simpler.

// Original main.cpp also had these defines, which might be needed for lseek64 behavior
// if not already covered by system headers or defines in other .c files.
// However, block_device.c now manages its own lseek64 definitions.
/*
#if defined(__MINGW32__)
#define open _open
#define read _read
#define write _write
#define close _close
// #define lseek64 _lseeki64 // Handled in block_device.c if needed
#endif

#if defined(__APPLE__) || defined(__FreeBSD__)
// #define lseek64 lseek // Handled in block_device.c
#endif

#if defined(__APPLE__)
#define _DARWIN_USE_64_BIT_INODE 1 // This might be relevant globally or for fcntl.h
#endif
*/
// For _DARWIN_USE_64_BIT_INODE, if it affects system headers, it should be defined
// before including them, or as a compiler flag. For now, assume it's handled if necessary
// by the includes in udpbd_c.h or block_device.c, or that default behavior is sufficient.


int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage:\n");
        fprintf(stderr, "  %s <file>\n", argv[0]);
        return EXIT_FAILURE; // Or return -1 as in original
    }

    const char *fileName = argv[1];
    UDPBDServer server; // Allocate server state on the stack

    // Initialize the server (which also initializes its block device)
    // UDPBDServer_init will print specific errors.
    if (UDPBDServer_init(&server, fileName) != 0) {
        fprintf(stderr, "Failed to initialize UDPBD server.\n");
        // UDPBDServer_init should have called BlockDevice_destroy on failure if BD was partially init'd.
        // If socket was partially init'd, it should also be cleaned up by UDPBDServer_init.
        return -2; // Match original error code for initialization failure
    }

    // Run the server. This function will block until a critical error or shutdown.
    // UDPBDServer_run will print specific errors if it exits due to one.
    int run_status = UDPBDServer_run(&server);
    if (run_status != 0) {
        fprintf(stderr, "UDPBD server exited with an error.\n");
    }

    // Clean up server resources (which also cleans up its block device)
    UDPBDServer_destroy(&server);

    if (run_status != 0) {
        return -2; // Or a different code if desired for runtime errors vs init errors
    }

    return EXIT_SUCCESS; // Or return 0 as in original
}
