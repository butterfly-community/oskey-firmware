// SPDX-License-Identifier: Apache-2.0

#include "mqtt.h"

#ifdef CONFIG_OSKEY_MQTT

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/conn_mgr_monitor.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/posix/netdb.h>
#include <zephyr/posix/poll.h>
#include <zephyr/posix/sys/socket.h>

LOG_MODULE_REGISTER(mqtt);

#define MQTT_BUFFER_SIZE         128
#define MQTT_CLIENT_ID_SIZE      19
#define MQTT_CONNECT_TIMEOUT_MS  2000
#define MQTT_NETWORK_POLL_MS     1000
#define MQTT_RECONNECT_DELAY_SEC 5
#define MQTT_THREAD_STACK_SIZE   4096

static uint8_t rx_buffer[MQTT_BUFFER_SIZE];
static uint8_t tx_buffer[MQTT_BUFFER_SIZE];
static struct mqtt_client client;
static struct sockaddr_storage broker;
static char client_id[MQTT_CLIENT_ID_SIZE];
static bool connected;

static K_SEM_DEFINE(network_connected, 0, 1);
static K_THREAD_STACK_DEFINE(mqtt_thread_stack, MQTT_THREAD_STACK_SIZE);
static struct k_thread mqtt_thread;
static struct net_mgmt_event_callback l4_event_cb;
static atomic_t network_ready;
static bool mqtt_thread_started;

static int init_client_id(void)
{
	struct net_if *iface = net_if_get_default();

	if (iface == NULL) {
		return -ENODEV;
	}

	const struct net_linkaddr *addr = net_if_get_link_addr(iface);

	if (addr == NULL || addr->len < 6) {
		return -ENODEV;
	}

	snprintk(client_id, sizeof(client_id), "oskey-%02x%02x%02x%02x%02x%02x", addr->addr[0],
		 addr->addr[1], addr->addr[2], addr->addr[3], addr->addr[4], addr->addr[5]);
	return 0;
}

static int init_broker(void)
{
	const struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
	};
	struct addrinfo *result = NULL;
	int ret = getaddrinfo(CONFIG_OSKEY_MQTT_BROKER_HOSTNAME, CONFIG_OSKEY_MQTT_BROKER_PORT,
			      &hints, &result);

	if (ret != 0) {
		LOG_ERR("Failed to resolve MQTT broker: %s", gai_strerror(ret));
		return -EHOSTUNREACH;
	}

	if (result == NULL) {
		return -ENOENT;
	}

	struct sockaddr_in *broker4 = (struct sockaddr_in *)&broker;
	const struct sockaddr_in *resolved = (const struct sockaddr_in *)result->ai_addr;

	*broker4 = *resolved;
	freeaddrinfo(result);
	return 0;
}

static int discard_publish_payload(struct mqtt_client *mqtt_client, size_t length)
{
	uint8_t buffer[64];

	while (length > 0) {
		int ret = mqtt_read_publish_payload_blocking(mqtt_client, buffer,
							     MIN(length, sizeof(buffer)));

		if (ret <= 0) {
			return ret < 0 ? ret : -EIO;
		}
		length -= ret;
	}

	return 0;
}

static void handle_publish(struct mqtt_client *mqtt_client,
			   const struct mqtt_publish_param *publish)
{
	const struct mqtt_utf8 *topic = &publish->message.topic.topic;

	LOG_INF("MQTT PUBLISH on '%.*s': %u bytes", (int)topic->size, (const char *)topic->utf8,
		publish->message.payload.len);

	int ret = discard_publish_payload(mqtt_client, publish->message.payload.len);

	if (ret < 0) {
		LOG_ERR("Failed to read MQTT payload: %d", ret);
		return;
	}

	if (publish->message.topic.qos == MQTT_QOS_1_AT_LEAST_ONCE) {
		const struct mqtt_puback_param ack = {
			.message_id = publish->message_id,
		};

		ret = mqtt_publish_qos1_ack(mqtt_client, &ack);
	} else if (publish->message.topic.qos == MQTT_QOS_2_EXACTLY_ONCE) {
		const struct mqtt_pubrec_param receive = {
			.message_id = publish->message_id,
		};

		ret = mqtt_publish_qos2_receive(mqtt_client, &receive);
	}

	if (ret < 0) {
		LOG_ERR("Failed to acknowledge MQTT PUBLISH: %d", ret);
	}
}

static void mqtt_event_handler(struct mqtt_client *mqtt_client, const struct mqtt_evt *event)
{
	switch (event->type) {
	case MQTT_EVT_CONNACK:
		connected = event->result == 0;
		if (connected) {
			LOG_INF("Connected to MQTT broker as %s", client_id);
		} else {
			LOG_ERR("MQTT connection rejected: %d", event->result);
		}
		break;
	case MQTT_EVT_DISCONNECT:
		connected = false;
		LOG_INF("MQTT disconnected: %d", event->result);
		break;
	case MQTT_EVT_PUBLISH:
		handle_publish(mqtt_client, &event->param.publish);
		break;
	case MQTT_EVT_PUBREL: {
		const struct mqtt_pubcomp_param complete = {
			.message_id = event->param.pubrel.message_id,
		};
		int ret = mqtt_publish_qos2_complete(mqtt_client, &complete);

		if (ret < 0) {
			LOG_ERR("Failed to complete MQTT QoS 2 exchange: %d", ret);
		}
		break;
	}
	default:
		break;
	}
}

static int init_client(void)
{
	int ret;

	if (client_id[0] == '\0') {
		ret = init_client_id();
		if (ret < 0) {
			return ret;
		}
	}

	ret = init_broker();

	if (ret < 0) {
		return ret;
	}

	mqtt_client_init(&client);
	client.broker = &broker;
	client.evt_cb = mqtt_event_handler;
	client.client_id.utf8 = (uint8_t *)client_id;
	client.client_id.size = strlen(client_id);
	client.user_name = NULL;
	client.password = NULL;
	client.rx_buf = rx_buffer;
	client.rx_buf_size = sizeof(rx_buffer);
	client.tx_buf = tx_buffer;
	client.tx_buf_size = sizeof(tx_buffer);
	client.transport.type = MQTT_TRANSPORT_NON_SECURE;
#ifdef CONFIG_MQTT_VERSION_5_0
	client.protocol_version = MQTT_VERSION_5_0;
#else
	client.protocol_version = MQTT_VERSION_3_1_1;
#endif
	return 0;
}

static int poll_client(int timeout)
{
	struct pollfd fd = {
		.fd = client.transport.tcp.sock,
		.events = POLLIN,
	};
	int ret = poll(&fd, 1, timeout);

	if (ret < 0) {
		return -errno;
	}

	if (ret == 0) {
		return 0;
	}

	if ((fd.revents & POLLIN) != 0) {
		ret = mqtt_input(&client);
		if (ret < 0) {
			return ret;
		}
	}

	if ((fd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
		return -ENOTCONN;
	}

	return 0;
}

static void disconnect_client(void)
{
	if (connected && mqtt_disconnect(&client, NULL) == 0) {
		return;
	}

	(void)mqtt_abort(&client);
	connected = false;
}

static int run_client(void)
{
	int ret = init_client();

	if (ret < 0) {
		return ret;
	}

	connected = false;
	ret = mqtt_connect(&client);
	if (ret < 0) {
		return ret;
	}

	ret = poll_client(MQTT_CONNECT_TIMEOUT_MS);
	if (ret < 0 || !connected) {
		if (ret == 0) {
			ret = -ETIMEDOUT;
		}
		goto out;
	}

	while (connected && atomic_get(&network_ready)) {
		int timeout = mqtt_keepalive_time_left(&client);

		if (timeout < 0 || timeout > MQTT_NETWORK_POLL_MS) {
			timeout = MQTT_NETWORK_POLL_MS;
		}

		ret = poll_client(timeout);
		if (ret < 0) {
			break;
		}

		ret = mqtt_live(&client);
		if (ret < 0 && ret != -EAGAIN) {
			break;
		}
		ret = 0;
	}

out:
	disconnect_client();
	return ret;
}

static void l4_event_handler(struct net_mgmt_event_callback *cb, uint64_t event,
			     struct net_if *iface)
{
	ARG_UNUSED(cb);
	ARG_UNUSED(iface);

	if (event == NET_EVENT_L4_CONNECTED) {
		atomic_set(&network_ready, 1);
		k_sem_give(&network_connected);
	} else if (event == NET_EVENT_L4_DISCONNECTED) {
		atomic_clear(&network_ready);
	}
}

static void mqtt_thread_handler(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		k_sem_take(&network_connected, K_FOREVER);

		while (atomic_get(&network_ready)) {
			int ret = run_client();

			if (ret < 0 && atomic_get(&network_ready)) {
				LOG_WRN("MQTT connection failed: %d", ret);
				k_sleep(K_SECONDS(MQTT_RECONNECT_DELAY_SEC));
			}
		}
	}
}

int mqtt_start(void)
{
	if (mqtt_thread_started) {
		return -EALREADY;
	}

	net_mgmt_init_event_callback(&l4_event_cb, l4_event_handler,
				     NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED);
	net_mgmt_add_event_callback(&l4_event_cb);
	conn_mgr_mon_resend_status();

	k_thread_create(&mqtt_thread, mqtt_thread_stack, K_THREAD_STACK_SIZEOF(mqtt_thread_stack),
			mqtt_thread_handler, NULL, NULL, NULL, K_PRIO_PREEMPT(8), 0, K_NO_WAIT);
	k_thread_name_set(&mqtt_thread, "mqtt");
	mqtt_thread_started = true;
	return 0;
}

#else

int mqtt_start(void)
{
	return 0;
}

#endif /* CONFIG_OSKEY_MQTT */
