#include "mqtt.h"

#ifdef CONFIG_MQTT_LIB

#include <zephyr/logging/log.h>

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/random/random.h>

#include <string.h>
#include <errno.h>

LOG_MODULE_REGISTER(MAIN);

#define APP_BMEM
#define APP_DMEM

/* Buffers for MQTT client. */
static APP_BMEM uint8_t rx_buffer[APP_MQTT_BUFFER_SIZE];
static APP_BMEM uint8_t tx_buffer[APP_MQTT_BUFFER_SIZE];

/* The mqtt client struct */
static APP_BMEM struct mqtt_client client_ctx;

/* MQTT Broker details. */
static APP_BMEM struct sockaddr_storage broker;

static APP_BMEM struct pollfd fds[1];
static APP_BMEM int nfds;
static APP_BMEM bool connected;

/* Whether to include full topic in the publish message, or alias only (MQTT 5). */
static APP_BMEM bool include_topic;
static APP_BMEM bool aliases_enabled;

#define APP_TOPIC_ALIAS 1

static void prepare_fds(struct mqtt_client *client)
{
	if (client->transport.type == MQTT_TRANSPORT_NON_SECURE) {
		fds[0].fd = client->transport.tcp.sock;
	}

	fds[0].events = POLLIN;
	nfds = 1;
}

static int wait(int timeout)
{
	int ret = 0;

	if (nfds > 0) {
		ret = poll(fds, nfds, timeout);
		if (ret < 0) {
			LOG_ERR("poll error: %d", errno);
		}
	}

	return ret;
}

static void on_mqtt_publish(struct mqtt_client *const client, const struct mqtt_evt *evt)
{
	int rc;
	uint8_t payload[128];

	rc = mqtt_read_publish_payload(client, payload, 128);
	if (rc < 0) {
		LOG_ERR("Failed to read received MQTT payload [%d]", rc);
		return;
	}
	payload[rc] = '\0';

	LOG_INF("MQTT payload received!");
	LOG_INF("topic: '%s', payload: %s", evt->param.publish.message.topic.topic.utf8, payload);
}

void mqtt_evt_handler(struct mqtt_client *const client, const struct mqtt_evt *evt)
{
	int err;

	switch (evt->type) {
	case MQTT_EVT_CONNACK:
		if (evt->result != 0) {
			LOG_ERR("MQTT connect failed %d", evt->result);
			break;
		}
		connected = true;
		LOG_INF("MQTT client connected!");

#if defined(CONFIG_MQTT_VERSION_5_0)
		if (evt->param.connack.prop.rx.has_topic_alias_maximum &&
		    evt->param.connack.prop.topic_alias_maximum > 0) {
			LOG_INF("Topic aliases allowed by the broker, max %u.",
				evt->param.connack.prop.topic_alias_maximum);
			aliases_enabled = true;
		} else {
			LOG_INF("Topic aliases disallowed by the broker.");
		}
#endif

		break;

	case MQTT_EVT_DISCONNECT:
		LOG_INF("MQTT client disconnected %d", evt->result);
		connected = false;
		// clear_fds
		nfds = 0;

		break;

	case MQTT_EVT_PUBACK:
		if (evt->result != 0) {
			LOG_ERR("MQTT PUBACK error %d", evt->result);
			break;
		}

		LOG_INF("PUBACK packet id: %u", evt->param.puback.message_id);

		break;

	case MQTT_EVT_PUBREC:
		if (evt->result != 0) {
			LOG_ERR("MQTT PUBREC error %d", evt->result);
			break;
		}

		LOG_INF("PUBREC packet id: %u", evt->param.pubrec.message_id);

		const struct mqtt_pubrel_param rel_param = {.message_id =
								    evt->param.pubrec.message_id};

		err = mqtt_publish_qos2_release(client, &rel_param);
		if (err != 0) {
			LOG_ERR("Failed to send MQTT PUBREL: %d", err);
		}

		break;

	case MQTT_EVT_PUBREL:
		if (evt->result != 0) {
			LOG_ERR("MQTT PUBREL error [%d]", evt->result);
			break;
		}

		LOG_INF("PUBREL packet ID: %u", evt->param.pubrel.message_id);

		const struct mqtt_pubcomp_param rec_param = {.message_id =
								     evt->param.pubrel.message_id};

		mqtt_publish_qos2_complete(client, &rec_param);
		break;

	case MQTT_EVT_PUBCOMP:
		if (evt->result != 0) {
			LOG_ERR("MQTT PUBCOMP error %d", evt->result);
			break;
		}

		LOG_INF("PUBCOMP packet id: %u", evt->param.pubcomp.message_id);

		break;

	case MQTT_EVT_PINGRESP:
		LOG_INF("PINGRESP packet");
		break;

	case MQTT_EVT_PUBLISH:
		const struct mqtt_publish_param *p = &evt->param.publish;

		if (p->message.topic.qos == MQTT_QOS_1_AT_LEAST_ONCE) {
			const struct mqtt_puback_param ack_param = {.message_id = p->message_id};
			mqtt_publish_qos1_ack(client, &ack_param);
		} else if (p->message.topic.qos == MQTT_QOS_2_EXACTLY_ONCE) {
			const struct mqtt_pubrec_param rec_param = {.message_id = p->message_id};
			mqtt_publish_qos2_receive(client, &rec_param);
		}
		on_mqtt_publish(client, evt);
		break;

	default:
		break;
	}
}

static char *get_mqtt_payload(enum mqtt_qos qos)
{
	static APP_DMEM char payload[] = "DOORS:OPEN_QoSx";

	payload[strlen(payload) - 1] = '0' + qos;

	return payload;
}

static char *get_mqtt_topic(void)
{
	return "Topic";
}

int app_mqtt_subscribe(struct mqtt_client *client)
{
	int rc;
	struct mqtt_topic sub_topics[] = {
		{.topic = {.utf8 = "Test", .size = strlen(sub_topics->topic.utf8)}, .qos = 0}};
	const struct mqtt_subscription_list sub_list = {
		.list = sub_topics, .list_count = ARRAY_SIZE(sub_topics), .message_id = 5841u};

	LOG_INF("Subscribing to %d topic(s)", sub_list.list_count);

	rc = mqtt_subscribe(client, &sub_list);
	if (rc != 0) {
		LOG_ERR("MQTT Subscribe failed [%d]", rc);
	}

	return rc;
}

static int publish(struct mqtt_client *client, enum mqtt_qos qos)
{
	struct mqtt_publish_param param = {0};

	/**
	 * Always true for MQTT 3.1.1.
	 * True only on first publish message for MQTT 5.0 if broker allows aliases.
	 */
	if (include_topic) {
		param.message.topic.topic.utf8 = (uint8_t *)get_mqtt_topic();
		param.message.topic.topic.size = strlen(param.message.topic.topic.utf8);
	}

	param.message.topic.qos = qos;
	param.message.payload.data = get_mqtt_payload(qos);
	param.message.payload.len = strlen(param.message.payload.data);
	param.message_id = sys_rand16_get();
	param.dup_flag = 0U;
	param.retain_flag = 0U;

#if defined(CONFIG_MQTT_VERSION_5_0)
	if (aliases_enabled) {
		param.prop.topic_alias = APP_TOPIC_ALIAS;
		include_topic = false;
	}
#endif

	return mqtt_publish(client, &param);
}

#define RC_STR(rc) ((rc) == 0 ? "OK" : "ERROR")

#define PRINT_RESULT(func, rc) LOG_INF("%s: %d <%s>", (func), rc, RC_STR(rc))

static void broker_init(void)
{
	int rc;

	const struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM};

	struct addrinfo *result;

	uint8_t broker_ip[NET_IPV4_ADDR_LEN];

	rc = getaddrinfo(SERVER_ADDR, SERVER_PORT_STR, &hints, &result);

	if (rc != 0) {
		LOG_ERR("Failed to resolve broker hostname [%s]", gai_strerror(rc));
	}
	if (result == NULL) {
		LOG_ERR("Broker address not found");
	}

	struct sockaddr_in *broker4 = (struct sockaddr_in *)&broker;
	broker4->sin_addr.s_addr = ((struct sockaddr_in *)result->ai_addr)->sin_addr.s_addr;
	broker4->sin_family = AF_INET;
	broker4->sin_port = ((struct sockaddr_in *)result->ai_addr)->sin_port;

	freeaddrinfo(result);

	inet_ntop(AF_INET, &broker4->sin_addr.s_addr, broker_ip, sizeof(broker_ip));
	LOG_INF("Broker address: %s:%s", broker_ip, SERVER_PORT_STR);
}

static void client_init(struct mqtt_client *client)
{
	mqtt_client_init(client);

	broker_init();

	/* MQTT client configuration */
	client->broker = &broker;
	client->evt_cb = mqtt_evt_handler;
	client->client_id.utf8 = (uint8_t *)MQTT_CLIENTID;
	client->client_id.size = strlen(MQTT_CLIENTID);
	client->password = NULL;
	client->user_name = NULL;
#if defined(CONFIG_MQTT_VERSION_5_0)
	client->protocol_version = MQTT_VERSION_5_0;
#else
	client->protocol_version = MQTT_VERSION_3_1_1;
#endif
	client->rx_buf = rx_buffer;
	client->rx_buf_size = sizeof(rx_buffer);
	client->tx_buf = tx_buffer;
	client->tx_buf_size = sizeof(tx_buffer);
	client->transport.type = MQTT_TRANSPORT_NON_SECURE;
}

/* In this routine we block until the connected variable is 1 */
static int try_to_connect(struct mqtt_client *client)
{
	int rc, i = 0;

	while (i++ < APP_CONNECT_TRIES && !connected) {

		client_init(client);

		rc = mqtt_connect(client);

		if (rc != 0) {
			PRINT_RESULT("mqtt_connect", rc);
			k_sleep(K_MSEC(APP_SLEEP_MSECS));
			continue;
		}

		prepare_fds(client);

		if (wait(APP_CONNECT_TIMEOUT_MS)) {
			mqtt_input(client);
		}

		if (!connected) {
			mqtt_abort(client);
		}
	}

	if (connected) {
		return 0;
	}

	return -EINVAL;
}

static int process_mqtt_and_sleep(struct mqtt_client *client, int timeout)
{
	int64_t remaining = timeout;
	int64_t start_time = k_uptime_get();
	int rc;

	while (remaining > 0 && connected) {
		if (wait(remaining)) {
			rc = mqtt_input(client);
			if (rc != 0) {
				PRINT_RESULT("mqtt_input", rc);
				return rc;
			}
		}

		rc = mqtt_live(client);

		if (rc != 0 && rc != -EAGAIN) {
			PRINT_RESULT("mqtt_live", rc);
			return rc;
		} else if (rc == 0) {
			rc = mqtt_input(client);
			if (rc != 0) {
				PRINT_RESULT("mqtt_input", rc);
				return rc;
			}
		}

		remaining = timeout + start_time - k_uptime_get();
	}

	return 0;
}

static int publisher(void)
{
	int rc, r = 0;

	include_topic = true;
	aliases_enabled = false;

	LOG_INF("attempting to connect: ");
	rc = try_to_connect(&client_ctx);
	PRINT_RESULT("try_to_connect", rc);

	if (rc != 0) {
		LOG_ERR("Failed to connect to MQTT broker");
		return rc;
	}

	LOG_INF("Connected to MQTT broker");

	if (connected) {
		LOG_INF("MQTT client connected, publishing messages...");
	} else {
		LOG_ERR("MQTT client not connected, exiting.");
		return -EINVAL;
	}

	rc = mqtt_ping(&client_ctx);
	PRINT_RESULT("mqtt_ping", rc);

	rc = app_mqtt_subscribe(&client_ctx);
	PRINT_RESULT("mqtt_subscribe", rc);

	rc = process_mqtt_and_sleep(&client_ctx, APP_SLEEP_MSECS);
	rc = publish(&client_ctx, MQTT_QOS_0_AT_MOST_ONCE);
	PRINT_RESULT("mqtt_publish", rc);

	while (connected) {
		rc = process_mqtt_and_sleep(&client_ctx, 1000);
		if (rc != 0) {
			break;
		}
	}

	return r;
}

static int start_app(void)
{
	int r = publisher();

	return r;
}

int mqtt_start(void)
{
	// wait_for_network();

	start_app();

	return 0;
}

#else

int mqtt_start()
{
	printk("MQTT not support!\n");
	return 0;
}

#endif /* CONFIG_MQTT_LIB */
