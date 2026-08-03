#ifndef OSKEY_TRANSPORT_H
#define OSKEY_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

#include "bindings.h"

int app_transport_send(struct TransportRoute route, const uint8_t *data, size_t len);

#endif
