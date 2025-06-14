#ifndef FILE_UTILS_H
#define FILE_UTILS_H

#include "../common.h" 

// General file copying utility
int file_utils_copy_file(const char *src, const char *dst);

// Utility to ensure a directory exists 
int file_utils_ensure_directory_exists(const char *path);

// Utility to publish a file to a destination 
int file_utils_publish_file_to_destination(const char *source_path, const char *destination_path);

// Removed file_utils_make_debug_copy as it's no longer used
// int file_utils_make_debug_copy(const char *source_path, const char *debug_dir_base, const char *prefix);

#endif