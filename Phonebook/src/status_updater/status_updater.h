// src/status_updater/status_updater.h
#ifndef STATUS_UPDATER_H
#define STATUS_UPDATER_H

#include "../common.h"

// The main thread function for the status updater task
void *status_updater_thread(void *arg);

#endif // STATUS_UPDATER_H