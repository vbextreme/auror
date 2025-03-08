#ifndef __STATUS_H__
#define __STATUS_H__

#include <notstd/core.h>
#include <notstd/threads.h>
#include <notstd/delay.h>

#include <auror/config.h>

typedef enum { STATUS_TYPE_NEW, STATUS_TYPE_DOWNLOAD, STATUS_TYPE_WORKING, STATUS_TYPE_INSTALL, STATUS_TYPE_COMPLETE } status_e;

typedef struct status{
	config_s* conf;
	mutex_t   lock;
	pthread_t curlock;
	int*      available;
	unsigned  line;
	char      desc[80];
	delay_t   lastsync;
	double    speed;
	unsigned  total;
	unsigned  completed;
}status_s;

status_s* status_ctor(status_s* status, config_s* conf, unsigned maxjobs);
status_s* status_dtor(status_s* status);
unsigned status_new_id(status_s* status);
void status_refresh(status_s* status, unsigned id, double value, status_e state);
void status_speed(status_s* status, double mib);
void status_completed(status_s* status, unsigned id);
void status_description(status_s* status, const char* desc);

#endif
