#include "bluetooth/bluetooth.h"

#include <zephyr/logging/log.h>

#include "message.h"

#ifdef CONFIG_OSKEY_BLUETOOTH

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/services/nus.h>

#define OSKEY_BT_PASSKEY 123456

LOG_MODULE_REGISTER(oskey_bt);

K_MUTEX_DEFINE(oskey_bt_conn_lock);
static struct bt_conn *active_conn;

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static const struct bt_data sd[] = {
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NUS_SRV_VAL),
};

static void set_active_conn(struct bt_conn *conn)
{
	struct bt_conn *old_conn;

	k_mutex_lock(&oskey_bt_conn_lock, K_FOREVER);
	old_conn = active_conn;
	active_conn = conn == NULL ? NULL : bt_conn_ref(conn);
	k_mutex_unlock(&oskey_bt_conn_lock);

	if (old_conn != NULL) {
		bt_conn_unref(old_conn);
	}
}

static struct bt_conn *get_active_conn(void)
{
	struct bt_conn *conn;

	k_mutex_lock(&oskey_bt_conn_lock, K_FOREVER);
	conn = active_conn == NULL ? NULL : bt_conn_ref(active_conn);
	k_mutex_unlock(&oskey_bt_conn_lock);

	return conn;
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (err) {
		LOG_ERR("Failed to connect to %s %u %s", addr, err, bt_hci_err_to_str(err));
		return;
	}

	LOG_INF("Connected %s", addr);

	set_active_conn(conn);

	err = bt_conn_set_security(conn, BT_SECURITY_L4);
	if (err) {
		LOG_ERR("Failed to set security (%d)", err);
		(void)bt_conn_disconnect(conn, BT_HCI_ERR_AUTH_FAIL);
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Disconnected from %s, reason 0x%02x %s", addr, reason, bt_hci_err_to_str(reason));

	set_active_conn(NULL);
	/* Protocol and signing state is not cleared; partial requests or responses may be lost. */
}

static void start_advertising(void)
{
	/* Some BlueZ/Realtek adapters require scanning before reconnecting to private addresses. */
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));

	if (err) {
		LOG_ERR("Failed to start advertising: %d", err);
	} else {
		LOG_INF("Advertising started");
	}
}

static void identity_resolved(struct bt_conn *conn, const bt_addr_le_t *rpa,
			      const bt_addr_le_t *identity)
{
	char addr_identity[BT_ADDR_LE_STR_LEN];
	char addr_rpa[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(identity, addr_identity, sizeof(addr_identity));
	bt_addr_le_to_str(rpa, addr_rpa, sizeof(addr_rpa));

	LOG_INF("Identity resolved %s -> %s", addr_rpa, addr_identity);
}

static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (!err) {
		LOG_INF("Security changed: %s level %u", addr, level);
	} else {
		LOG_ERR("Security failed: %s level %u err %s(%d)", addr, level,
			bt_security_err_to_str(err), err);
		(void)bt_conn_disconnect(conn, BT_HCI_ERR_AUTH_FAIL);
	}
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.recycled = start_advertising,
	.identity_resolved = identity_resolved,
	.security_changed = security_changed,
};

static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Passkey for %s: %06u", addr, passkey);
}

static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Pairing cancelled: %s", addr);
}

#ifdef CONFIG_BT_APP_PASSKEY
static uint32_t auth_app_passkey(struct bt_conn *conn)
{
	ARG_UNUSED(conn);

	return OSKEY_BT_PASSKEY;
}
#endif

static struct bt_conn_auth_cb auth_callbacks = {
	.passkey_display = auth_passkey_display,
	.cancel = auth_cancel,
#ifdef CONFIG_BT_APP_PASSKEY
	.app_passkey = auth_app_passkey,
#endif
};

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	LOG_INF("Pairing complete%s", bonded ? " and bonded" : "");
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	LOG_ERR("Pairing Failed (%d). Disconnecting.", reason);
	bt_conn_disconnect(conn, BT_HCI_ERR_AUTH_FAIL);
}

static struct bt_conn_auth_info_cb auth_info_callbacks = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed,
};

static void notifications_changed(bool enabled, void *ctx)
{
	ARG_UNUSED(ctx);

	LOG_INF("Notifications %s", enabled ? "enabled" : "disabled");
}

static void nus_received(struct bt_conn *conn, const void *data, uint16_t len, void *ctx)
{
	ARG_UNUSED(ctx);

	if (bt_conn_get_security(conn) < BT_SECURITY_L4) {
		LOG_WRN("Ignoring %u bytes received before L4 security", len);
		return;
	}

	if (app_message_submit(AppMessageSource_Bluetooth, AppMessageAction_External, 0, data, len,
			       NULL, 0)) {
		LOG_DBG("Received %u bytes", len);
	} else {
		LOG_ERR("Failed to queue %u Bluetooth bytes", len);
	}
}

static struct bt_nus_cb nus_callbacks = {
	.notif_enabled = notifications_changed,
	.received = nus_received,
};

int oskey_bt_init(void)
{
	int err = bt_nus_cb_register(&nus_callbacks, NULL);

	if (err) {
		LOG_ERR("Failed to register NUS callback: %d", err);
		return err;
	}

	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return err;
	}
	LOG_INF("Bluetooth initialized");

	return 0;
}

int oskey_bt_start(void)
{
	int err = bt_conn_auth_cb_register(&auth_callbacks);
	if (err) {
		return err;
	}

	err = bt_conn_auth_info_cb_register(&auth_info_callbacks);
	if (err) {
		return err;
	}

	start_advertising();

	return 0;
}

int oskey_bt_send(const uint8_t *data, size_t len)
{
	struct bt_conn *conn;
	size_t max_payload;
	int err = 0;

	if (data == NULL) {
		return -EINVAL;
	}
	if (len == 0) {
		return 0;
	}

	conn = get_active_conn();
	if (conn == NULL) {
		return -ENOTCONN;
	}

	if (bt_conn_get_security(conn) < BT_SECURITY_L4) {
		err = -EACCES;
		goto out;
	}

	max_payload = bt_gatt_get_mtu(conn);
	if (max_payload <= 3U) {
		err = -EMSGSIZE;
		goto out;
	}
	max_payload -= 3U;
	while (len > 0) {
		uint16_t chunk_len = (uint16_t)MIN(len, max_payload);

		err = bt_nus_send(conn, data, chunk_len);
		if (err) {
			break;
		}

		data += chunk_len;
		len -= chunk_len;
	}

out:
	bt_conn_unref(conn);
	return err;
}

#else

int oskey_bt_init(void)
{
	return 0;
}

int oskey_bt_start(void)
{
	return 0;
}

int oskey_bt_send(const uint8_t *data, size_t len)
{
	ARG_UNUSED(data);
	ARG_UNUSED(len);

	return -ENOTSUP;
}

#endif
