#ifndef __DOWNLOAD_H__
#define __DOWNLOAD_H__

#include <notstd/delay.h>
#include <auror/config.h>
#include <auror/status.h>

int download_lastsync(config_s* conf, delay_t* lastsync);
void* download_database(const char* dbtmpname, status_s* status, unsigned idstatus, configRepository_s* repo, config_s* conf);




#endif
