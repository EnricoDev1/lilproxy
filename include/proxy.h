#ifndef PROXY_PROXY_H
#define PROXY_PROXY_H

#include <stddef.h>

#include "state.h"
#include "types.h"

proxyContext *ctx_malloc();
appConfig *cfg_init();
void connection_init();
void proxy_init();
int relay(int src, int dst, int sender);

#endif
