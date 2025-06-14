#include "file_utils.h"
#include "../common.h" // For logging macros
#include <sys/stat.h> // For mkdir
#include <string.h>   // For strdup, basename
#include <libgen.h>   // For dirname
#include <errno.h>    // For strerror
#include <time.h>     // For timestamp in debug copies
#include <unistd.h>   // For fsync, fileno, access

#define MODULE_NAME "UTILS"

int file_utils_copy_file(const char *src, const char *dst) {
    FILE *fsrc = NULL, *fdst = NULL;
    char buf[4096];
    size_t bytes;
    int ret = 0;

    fsrc = fopen(src, "rb");
    if (!fsrc) {
        LOG_ERROR("Failed to open source file for copy '%s'. Error: %s", src, strerror(errno));
        return 1;
    }

    fdst = fopen(dst, "wb");
    if (!fdst) {
        LOG_ERROR("Failed to open destination file for copy '%s'. Error: %s", dst, strerror(errno));
        fclose(fsrc);
        return 1;
    }

    while ((bytes = fread(buf, 1, sizeof(buf), fsrc)) > 0) {
        if (fwrite(buf, 1, bytes, fdst) != bytes) {
            LOG_ERROR("Error writing to destination file '%s' during copy. Error: %s", dst, strerror(errno));
            ret = 1;
            break;
        }
    }

    if (ferror(fsrc)) {
        LOG_ERROR("Error reading from source file '%s' during copy. Error: %s", src, strerror(errno));
        ret = 1;
    }

    fflush(fdst);
    fsync(fileno(fdst));
    fclose(fdst);
    fclose(fsrc);

    return ret;
}

int file_utils_ensure_directory_exists(const char *path) {
    char *path_copy = strdup(path);
    if (!path_copy) {
        LOG_ERROR("Failed to duplicate path for directory creation. Error: %s", strerror(errno));
        return 1;
    }

    char *directory_to_create = dirname(path_copy);

    struct stat st = {0};
    if (stat(directory_to_create, &st) == -1) {
        if (errno == ENOENT) {
            LOG_INFO("Directory '%s' does not exist. Attempting to create.", directory_to_create);
            if (mkdir(directory_to_create, 0755) == -1) {
                LOG_ERROR("Failed to create directory '%s'. Error: %s", directory_to_create, strerror(errno));
                free(path_copy);
                return 1;
            }
            LOG_INFO("Successfully created directory '%s'.", directory_to_create);
        } else {
            LOG_ERROR("Error checking directory '%s'. Error: %s", directory_to_create, strerror(errno));
            free(path_copy);
            return 1;
        }
    } else if (!S_ISDIR(st.st_mode)) {
        LOG_ERROR("Path '%s' exists but is not a directory.", directory_to_create);
        free(path_copy);
        return 1;
    } else {
        LOG_DEBUG("Directory '%s' already exists.", directory_to_create);
    }
    free(path_copy);
    return 0;
}

int file_utils_publish_file_to_destination(const char *source_path, const char *destination_path) {
    LOG_INFO("Copying from '%s' to '%s'.", source_path, destination_path);
    if (file_utils_copy_file(source_path, destination_path) == 0) {
        LOG_INFO("Copied '%s' to '%s'. Temporary source file NOT deleted for debugging.", source_path, destination_path);
        return 0;
    } else {
        LOG_ERROR("Failed to copy file from '%s' to '%s'. Error: %s", source_path, destination_path, strerror(errno));
        return 1;
    }
}

// Removed entire #ifdef DEBUG_BUILD ... #endif block for file_utils_make_debug_copy
// as requested, removing all logic for debug files.