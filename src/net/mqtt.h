#ifndef MQTT_H
#define MQTT_H

#include "wrapper.h"

#define SERVER_ADDR            "bemfa.com"
#define SERVER_PORT            9501
#define SERVER_PORT_STR        "9501"
#define APP_CONNECT_TIMEOUT_MS 2000
#define APP_SLEEP_MSECS        500
#define APP_CONNECT_TRIES      10
#define APP_MQTT_BUFFER_SIZE   128
#define MQTT_CLIENTID          ""

int mqtt_start();

#endif
