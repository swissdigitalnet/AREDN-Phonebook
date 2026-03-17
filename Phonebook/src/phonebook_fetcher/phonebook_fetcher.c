#define MODULE_NAME "FETCHER" // Define MODULE_NAME at the top of the file

#include "phonebook_fetcher.h"
#include "../common.h" // This includes necessary system headers and core types
#include "../config_loader/config_loader.h" // For g_pb_interval_seconds, g_phonebook_servers_list, g_num_phonebook_servers
#include "../user_manager/user_manager.h"
#include "../file_utils/file_utils.h"
#include "../csv_processor/csv_processor.h"
#include "../passive_safety/passive_safety.h" // For heartbeat tracking
#include "../software_health/software_health.h" // For health monitoring

// Note: Global extern declarations moved to common.h
// extern int g_pb_interval_seconds; // Declared in common.h
// extern ConfigurableServer g_phonebook_servers_list[MAX_PB_SERVERS]; // Declared in common.h
// extern int g_num_phonebook_servers; // Declared in common.h


int ensure_phonebook_directory_exists(const char *path) {
    return file_utils_ensure_directory_exists(path);
}

int publish_phonebook_xml(const char *source_filepath) {
    pthread_mutex_lock(&phonebook_file_mutex);

    char public_path_copy[MAX_CONFIG_PATH_LEN]; // MAX_CONFIG_PATH_LEN from common.h
    strncpy(public_path_copy, PB_XML_PUBLIC_PATH, sizeof(public_path_copy) - 1); // PB_XML_PUBLIC_PATH from common.h
    public_path_copy[sizeof(public_path_copy) - 1] = '\0';
    char *public_dir = dirname(public_path_copy); // dirname from common.h

    if (file_utils_ensure_directory_exists(public_dir) != 0) {
        LOG_ERROR("Critical: Failed to ensure public directory '%s' for publish. Exiting publish.", public_dir);
        pthread_mutex_unlock(&phonebook_file_mutex);
        return 1;
    }

    // Passive Safety: Use safe file operation with automatic rollback and verification
    int result = safe_phonebook_file_operation(source_filepath, PB_XML_PUBLIC_PATH);

    if (result == 0) {
        LOG_INFO("Phonebook XML safely published at %s.", PB_XML_PUBLIC_PATH);

        // Only signal status updater on successful publish
        pthread_mutex_lock(&updater_trigger_mutex);
        pthread_cond_signal(&updater_trigger_cond);
        pthread_mutex_unlock(&updater_trigger_mutex);
        LOG_INFO("Signaled Status Updater for new phonebook.");

        // Clean up source temp file after successful publish
        if (access(source_filepath, F_OK) == 0) {
            if (remove(source_filepath) != 0) {
                LOG_WARN("Failed to delete temporary XML file '%s' after successful publish. Error: %s",
                        source_filepath, strerror(errno));
            } else {
                LOG_DEBUG("Deleted temporary XML file '%s' after successful publish.", source_filepath);
            }
        }
        pthread_mutex_unlock(&phonebook_file_mutex);
        return 0;
    } else {
        LOG_ERROR("Safe file operation failed for XML publish");
        // On failure, preserve the temp XML for inspection rather than deleting it
        LOG_INFO("Preserving temporary XML file '%s' for inspection after failed publish.", source_filepath);
        pthread_mutex_unlock(&phonebook_file_mutex);
        return 1;
    }
}

static bool initial_population_done = false;

void *phonebook_fetcher_thread(void *arg) {
    (void)arg;
    LOG_INFO("Phonebook fetcher started. Checking for existing phonebook data.");

    // Register this thread for health monitoring
    int thread_index = health_register_thread(pthread_self(), "phonebook_fetcher");
    if (thread_index < 0) {
        LOG_WARN("Failed to register phonebook fetcher thread for health monitoring");
        // Continue anyway - health monitoring is not critical for operation
    }

    // Emergency boot sequence: Load existing phonebook immediately if available
    if (access(PB_CSV_PATH, F_OK) == 0) {
        LOG_INFO("Found existing phonebook CSV at '%s'. Loading immediately for service availability.", PB_CSV_PATH);
        populate_registered_users_from_csv(PB_CSV_PATH);
        LOG_INFO("Emergency boot: SIP user database loaded from persistent storage. Directory entries: %d.", num_directory_entries);
        initial_population_done = true;

        // Load existing hash if available
        char existing_hash[HASH_LENGTH + 1] = "";
        FILE *hash_fp = fopen(PB_LAST_GOOD_CSV_HASH_PATH, "r");
        if (hash_fp) {
            if (fgets(existing_hash, sizeof(existing_hash), hash_fp) != NULL) {
                existing_hash[strcspn(existing_hash, "\r\n")] = '\0';
            }
            fclose(hash_fp);
        }

        // Update health metrics: emergency boot with existing data
        extern service_metrics_t g_service_metrics;
        extern pthread_mutex_t g_health_mutex;
        pthread_mutex_lock(&g_health_mutex);
        g_service_metrics.phonebook_last_updated = time(NULL);  // Use current time for boot
        strncpy(g_service_metrics.phonebook_fetch_status, "BOOT",
                sizeof(g_service_metrics.phonebook_fetch_status) - 1);
        if (existing_hash[0] != '\0') {
            strncpy(g_service_metrics.phonebook_csv_hash, existing_hash,
                    sizeof(g_service_metrics.phonebook_csv_hash) - 1);
        }
        g_service_metrics.phonebook_entries_loaded = num_directory_entries;
        pthread_mutex_unlock(&g_health_mutex);

        LOG_INFO("Health metrics updated: emergency boot with %d entries", num_directory_entries);

        // Convert to XML for web interface
        char existing_xml_temp_path[MAX_CONFIG_PATH_LEN];
        if (csv_processor_convert_csv_to_xml_and_get_path(existing_xml_temp_path, sizeof(existing_xml_temp_path)) == 0) {
            publish_phonebook_xml(existing_xml_temp_path);
            LOG_INFO("Emergency boot: XML phonebook published from existing data.");
        }
    } else {
        LOG_INFO("No existing phonebook found. Service will be available after first successful fetch.");
    }

    LOG_INFO("Entering main phonebook fetch loop.");
    while (g_keep_running) { // Check shutdown flag for graceful termination
        // Check for cooperative restart request from passive safety monitor
        extern volatile sig_atomic_t g_fetcher_restart_requested;
        if (g_fetcher_restart_requested) {
            LOG_INFO("Cooperative restart requested - exiting gracefully to allow restart");
            g_fetcher_restart_requested = 0; // Reset flag for next instance
            break;
        }

        // Passive Safety: Update heartbeat for thread recovery monitoring
        g_fetcher_last_heartbeat = time(NULL);

        // Health Monitoring: Update heartbeat
        if (thread_index >= 0) {
            health_update_heartbeat(thread_index);
        }

        LOG_DEBUG("Starting new fetcher cycle.");
        char new_csv_hash[HASH_LENGTH + 1]; // HASH_LENGTH from common.h
        char last_good_csv_hash[HASH_LENGTH + 1];

        if (csv_processor_download_csv() != 0) {
            LOG_ERROR("CSV download failed. Skipping this cycle.");

            // Update health metrics: mark fetch as failed
            extern service_metrics_t g_service_metrics;
            extern pthread_mutex_t g_health_mutex;
            pthread_mutex_lock(&g_health_mutex);
            strncpy(g_service_metrics.phonebook_fetch_status, "FAILED",
                    sizeof(g_service_metrics.phonebook_fetch_status) - 1);
            pthread_mutex_unlock(&g_health_mutex);

            goto end_fetcher_cycle;
        }

        // Calculate hash of downloaded temp file (in RAM)
        if (csv_processor_calculate_file_conceptual_hash(PB_CSV_TEMP_PATH, new_csv_hash, sizeof(new_csv_hash)) != 0) {
            LOG_ERROR("Failed to calculate hash for downloaded CSV. Skipping this cycle.");
            remove(PB_CSV_TEMP_PATH); // Clean up temp file
            goto end_fetcher_cycle;
        }

        // VALIDATE CSV BEFORE ANY DESTRUCTIVE OPERATIONS
        int valid_row_count = 0;
        if (csv_processor_validate_csv(PB_CSV_TEMP_PATH, &valid_row_count) != 0) {
            LOG_ERROR("Downloaded CSV failed validation. Refusing to accept invalid phonebook data.");
            remove(PB_CSV_TEMP_PATH); // Clean up invalid temp file
            goto end_fetcher_cycle;
        }
        LOG_INFO("CSV validation passed: %d valid entries in downloaded file", valid_row_count);

        // Read existing hash from flash (only if we have persistent data)
        FILE *hash_fp = fopen(PB_LAST_GOOD_CSV_HASH_PATH, "r");
        if (hash_fp) {
            if (fgets(last_good_csv_hash, sizeof(last_good_csv_hash), hash_fp) != NULL) {
                last_good_csv_hash[strcspn(last_good_csv_hash, "\r\n")] = '\0';
                LOG_DEBUG("Last good CSV hash: %s", last_good_csv_hash);
            } else {
                LOG_INFO("Could not read last good CSV hash. Assuming change.");
                last_good_csv_hash[0] = '\0';
            }
            fclose(hash_fp);
        } else {
            LOG_INFO("No last good CSV hash file found. Assuming change for first run.");
            last_good_csv_hash[0] = '\0';
        }
        LOG_DEBUG("New CSV hash: %s", new_csv_hash);

        // Flash-friendly comparison: Only write to flash if data actually changed
        if (strcmp(new_csv_hash, last_good_csv_hash) == 0 && initial_population_done) {
            LOG_DEBUG("Downloaded CSV is identical to flash copy. No flash write needed - preserving flash lifespan.");
            remove(PB_CSV_TEMP_PATH); // Clean up unchanged temp file
            goto end_fetcher_cycle;
        }

        // CRITICAL: Copy validated temp to persistent BEFORE any destructive operations
        // This ensures persistent storage has good data before we nuke the user table
        if (!initial_population_done) {
            LOG_INFO("Initial population required. Persisting validated CSV to storage.");
        } else {
            LOG_INFO("CSV content changed. Updating persistent storage with validated data (flash write).");
        }

        // Ensure the persistent storage directory exists before copying
        char csv_dir_copy[MAX_CONFIG_PATH_LEN];
        strncpy(csv_dir_copy, PB_CSV_PATH, sizeof(csv_dir_copy) - 1);
        csv_dir_copy[sizeof(csv_dir_copy) - 1] = '\0';
        char *csv_dir = dirname(csv_dir_copy);
        if (file_utils_ensure_directory_exists(csv_dir) != 0) {
            LOG_ERROR("Failed to create directory '%s' for persistent CSV storage", csv_dir);
            remove(PB_CSV_TEMP_PATH); // Clean up temp file
            goto end_fetcher_cycle;
        }

        // Cross-filesystem move: copy validated temp file to flash
        if (file_utils_copy_file(PB_CSV_TEMP_PATH, PB_CSV_PATH) != 0) {
            LOG_ERROR("Failed to copy validated temp CSV to persistent storage");
            remove(PB_CSV_TEMP_PATH); // Clean up temp file
            goto end_fetcher_cycle;
        }
        LOG_INFO("Validated CSV successfully copied to persistent storage.");
        remove(PB_CSV_TEMP_PATH); // Clean up temp file after successful copy

        // Now populate users from PERSISTENT CSV (this will clear the table, but persistent is validated)
        LOG_DEBUG("Populating SIP users from validated persistent CSV.");
        populate_registered_users_from_csv(PB_CSV_PATH);
        LOG_DEBUG("SIP user database populated from CSV. Total directory entries: %d.", num_directory_entries);
        initial_population_done = true;

        // Generate XML from the persistent CSV path for publishing
        LOG_DEBUG("Initiating XML conversion...");
        char fetched_xml_temp_path[MAX_CONFIG_PATH_LEN];
        if (csv_processor_convert_csv_to_xml_and_get_path(fetched_xml_temp_path, sizeof(fetched_xml_temp_path)) != 0) {
            LOG_ERROR("XML conversion failed. But persistent storage is intact - service remains available.");
            goto end_fetcher_cycle;
        }
        LOG_INFO("XML conversion successful.");

        // Try to publish the XML
        if (publish_phonebook_xml(fetched_xml_temp_path) != 0) {
            LOG_ERROR("XML publish failed. But persistent storage is intact - service remains available.");
            goto end_fetcher_cycle;
        }

        // Only update hash in flash if we haven't already written this hash
        if (strcmp(new_csv_hash, last_good_csv_hash) != 0) {
            FILE *hash_fp_write = fopen(PB_LAST_GOOD_CSV_HASH_PATH, "w");
            if (hash_fp_write) {
                fprintf(hash_fp_write, "%s\n", new_csv_hash);
                fclose(hash_fp_write);
                LOG_INFO("Flash write: Updated CSV hash to '%s' (flash wear minimized).", new_csv_hash);
            } else {
                LOG_ERROR("Failed to write new CSV hash to '%s'. Error: %s", PB_LAST_GOOD_CSV_HASH_PATH, strerror(errno));
            }
        } else {
            LOG_DEBUG("Hash unchanged, skipping flash write for hash file.");
        }

        // Update health metrics: successful fetch
        extern service_metrics_t g_service_metrics;
        extern pthread_mutex_t g_health_mutex;
        pthread_mutex_lock(&g_health_mutex);
        g_service_metrics.phonebook_last_updated = time(NULL);
        strncpy(g_service_metrics.phonebook_fetch_status, "SUCCESS",
                sizeof(g_service_metrics.phonebook_fetch_status) - 1);
        strncpy(g_service_metrics.phonebook_csv_hash, new_csv_hash,
                sizeof(g_service_metrics.phonebook_csv_hash) - 1);
        g_service_metrics.phonebook_entries_loaded = num_directory_entries;
        pthread_mutex_unlock(&g_health_mutex);

        LOG_INFO("Health metrics updated: phonebook fetch SUCCESS, %d entries, hash %s",
                 num_directory_entries, new_csv_hash);
        LOG_INFO("Finished fetcher cycle.");

        end_fetcher_cycle:;
        LOG_INFO("Sleeping %d seconds...", g_pb_interval_seconds);

        pthread_mutex_lock(&fetcher_wake_mutex);
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += g_pb_interval_seconds;

        int wait_status = pthread_cond_timedwait(&fetcher_wake_cond, &fetcher_wake_mutex, &ts);
        pthread_mutex_unlock(&fetcher_wake_mutex);

        // Check all wake conditions
        if (!g_keep_running) {
            break;
        }

        extern volatile sig_atomic_t g_fetcher_restart_requested;
        if (g_fetcher_restart_requested) {
            LOG_INFO("Cooperative restart requested during sleep - exiting gracefully");
            g_fetcher_restart_requested = 0;
            LOG_INFO("Phonebook fetcher thread exiting for restart.");
            return NULL;
        }

        if (phonebook_reload_requested) {
            phonebook_reload_requested = 0;
            LOG_INFO("Webhook reload requested - interrupting sleep to fetch phonebook immediately");
        }

        if (wait_status == ETIMEDOUT) {
            LOG_DEBUG("Fetcher woke on schedule (every %d seconds).", g_pb_interval_seconds);
        } else if (wait_status == 0) {
            LOG_DEBUG("Fetcher woke by signal (webhook or shutdown).");
        }
    }
    LOG_INFO("Phonebook fetcher thread exiting.");
    return NULL;
}