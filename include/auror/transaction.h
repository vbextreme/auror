#ifndef __TRANSACTION_H__
#define __TRANSACTION_H__

#include <auror/config.h>

void transaction_begin(config_s* conf);
void transaction_end(void);

#endif
