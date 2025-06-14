#ifndef PHONEBOOK_FETCHER_H
#define PHONEBOOK_FETCHER_H

#include "../common.h" 
#include "../file_utils/file_utils.h" 

// Thread function
void *phonebook_fetcher_thread(void *arg);

// Utility function to ensure directory exists (now in file_utils)
int ensure_phonebook_directory_exists(const char *path);

// Function to publish XML (called from status_updater)
int publish_phonebook_xml(const char *source_filepath);

#endif