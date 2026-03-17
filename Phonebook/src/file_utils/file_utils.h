#ifndef FILE_UTILS_H
#define FILE_UTILS_H

#include "../common.h" 

// General file copying utility
int file_utils_copy_file(const char *src, const char *dst);

// Utility to ensure a directory exists 
int file_utils_ensure_directory_exists(const char *path);

// Utility to publish a file to a destination 
int file_utils_publish_file_to_destination(const char *source_path, const char *destination_path);

// Shared string utility: trim leading/trailing whitespace in-place
// Returns pointer to trimmed string (may point into original buffer)
char* trim_whitespace(char *str);

// Write a JSON-escaped string to a FILE (escapes \ " and control chars)
void json_write_escaped(FILE *fp, const char *str);

#endif