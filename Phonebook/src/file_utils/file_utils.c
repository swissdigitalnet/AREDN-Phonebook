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

// Recursive helper function to create directories like 'mkdir -p'
static int create_directory_recursive(const char *path) {
    char *path_copy = strdup(path);
    if (!path_copy) {
        LOG_ERROR("Failed to duplicate path for recursive directory creation. Error: %s", strerror(errno));
        return 1;
    }

    // Remove trailing slashes
    size_t len = strlen(path_copy);
    while (len > 1 && path_copy[len - 1] == '/') {
        path_copy[len - 1] = '\0';
        len--;
    }

    // Check if directory already exists
    struct stat st = {0};
    if (stat(path_copy, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            LOG_DEBUG("Directory '%s' already exists.", path_copy);
            free(path_copy);
            return 0;
        } else {
            LOG_ERROR("Path '%s' exists but is not a directory.", path_copy);
            free(path_copy);
            return 1;
        }
    }

    // Get parent directory
    char *parent_copy = strdup(path_copy);
    if (!parent_copy) {
        LOG_ERROR("Failed to duplicate path for parent directory. Error: %s", strerror(errno));
        free(path_copy);
        return 1;
    }

    char *parent_dir = dirname(parent_copy);

    // Skip if we've reached root
    if (strcmp(parent_dir, path_copy) != 0 && strcmp(parent_dir, "/") != 0 && strcmp(parent_dir, ".") != 0) {
        // Recursively create parent directory
        if (create_directory_recursive(parent_dir) != 0) {
            free(path_copy);
            free(parent_copy);
            return 1;
        }
    }

    free(parent_copy);

    // Create this directory
    LOG_INFO("Creating directory '%s'.", path_copy);
    if (mkdir(path_copy, 0755) == -1) {
        if (errno != EEXIST) {  // Ignore if directory was created by another thread
            LOG_ERROR("Failed to create directory '%s'. Error: %s", path_copy, strerror(errno));
            free(path_copy);
            return 1;
        }
    }

    LOG_INFO("Successfully created directory '%s'.", path_copy);
    free(path_copy);
    return 0;
}

int file_utils_ensure_directory_exists(const char *path) {
    if (!path || strlen(path) == 0) {
        LOG_ERROR("Invalid path provided for directory creation.");
        return 1;
    }

    // Always treat path as a directory to create.
    // Callers who need to ensure a file's parent directory should
    // call dirname() themselves before passing the path here.
    return create_directory_recursive(path);
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

char* trim_whitespace(char *str) {
    if (!str || *str == '\0') {
        return str;
    }

    char *start = str;

    // Trim leading space
    while(isspace((unsigned char)*start)) start++;

    if(*start == '\0') {
        *str = '\0';
        return str;
    }

    // Trim trailing space
    char *end = start + strlen(start) - 1;
    while(end > start && isspace((unsigned char)*end)) end--;

    // Write new null terminator character
    end[1] = '\0';

    // Shift trimmed string to beginning of buffer (in-place)
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }

    return str;
}

void json_write_escaped(FILE *fp, const char *str) {
    if (!str) {
        return;
    }
    for (const char *p = str; *p; p++) {
        switch (*p) {
            case '"':  fputs("\\\"", fp); break;
            case '\\': fputs("\\\\", fp); break;
            case '\n': fputs("\\n", fp);  break;
            case '\r': fputs("\\r", fp);  break;
            case '\t': fputs("\\t", fp);  break;
            default:
                if ((unsigned char)*p < 0x20) {
                    fprintf(fp, "\\u%04x", (unsigned char)*p);
                } else {
                    fputc(*p, fp);
                }
                break;
        }
    }
}