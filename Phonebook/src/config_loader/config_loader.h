// config_loader.h
#ifndef CONFIG_LOADER_H
#define CONFIG_LOADER_H

#include "common.h" // Include common.h to get definitions like ConfigurableServer, MAX_PB_SERVERS

// These global variables are DECLARED here (extern) and DEFINED in config_loader.c
extern int g_pb_interval_seconds;
extern int g_status_update_interval_seconds;
extern int g_uac_test_interval_seconds;
extern ConfigurableServer g_phonebook_servers_list[MAX_PB_SERVERS];
extern int g_num_phonebook_servers;

/**
 * @brief Loads configuration parameters from a specified file.
 *
 * This function reads key-value pairs from the configuration file.
 * It parses PB_INTERVAL_SECONDS, STATUS_UPDATE_INTERVAL_SECONDS,
 * and multiple PHONEBOOK_SERVER entries.
 * Default values are used if the file is not found or if specific
 * parameters are missing/malformed.
 *
 * @param config_filepath The path to the configuration file (e.g., "/etc/sipserver.conf").
 * @return 0 on successful loading (even if some defaults are used), 1 if a critical
 * error prevents any configuration from being loaded (e.g., memory allocation error).
 */
int load_configuration(const char *config_filepath);

#endif // CONFIG_LOADER_H
