// log_manager.h
#ifndef LOG_MANAGER_H
#define LOG_MANAGER_H

void log_init(const char* app_name);
void log_shutdown(void);
void log_message(int level, const char* app_name_in, const char* module_name_in, const char *format, ...);

#endif // LOG_MANAGER_H