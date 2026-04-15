#ifndef PROXY_PROXY_H
#define PROXY_PROXY_H

#include <stddef.h>

#include "state.h"

void *ctx_malloc(size_t size);
void connection_init(int port);
void proxy_init(const char *addr, int port);
int relay(int src, int dst, int sender);

#endif
