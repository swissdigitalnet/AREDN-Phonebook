#ifndef CSV_PROCESSOR_H
#define CSV_PROCESSOR_H

#include "../common.h" 

// Function to download CSV from URL to PB_CSV_PATH
int csv_processor_download_csv(void);

// Function to convert CSV to XML and get path to temp XML file
int csv_processor_convert_csv_to_xml_and_get_path(char *output_path, size_t output_path_len);

// Function to calculate a conceptual hash of a file
int csv_processor_calculate_file_conceptual_hash(const char *filepath, char *output_hash_str, size_t hash_str_len);

// Function to validate CSV file has minimum viable content
// Returns 0 if valid, 1 if invalid
int csv_processor_validate_csv(const char *filepath, int *row_count);

#endif