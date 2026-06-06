/*
 * Reference only: BLE code before it was wrapped in BluetoothRgbPeripheral.
 *
 * This file is not built by CMake. It is kept only for comparison with
 * src/main.cpp.
 */

static void on_ble_connected(bt_conn *conn, uint8_t err)
{
	if (err != 0) {
		printf("Bluetooth connection failed: 0x%02x\n", err);
		return;
	}

	printf("Bluetooth connected\n");
}

static void on_ble_disconnected(bt_conn *conn, uint8_t reason)
{
	printf("Bluetooth disconnected: 0x%02x\n", reason);
}

BT_CONN_CB_DEFINE(connection_callbacks) = {
	.connected = on_ble_connected,
	.disconnected = on_ble_disconnected,
};

static struct bt_uuid_128 rgb_service_uuid =
	BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x9f1d0000, 0x3d2f, 0x4f3a, 0x8b11, 0x123456789abc));

static struct bt_uuid_128 rgb_color_uuid =
	BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x9f1d0001, 0x3d2f, 0x4f3a, 0x8b11, 0x123456789abc));

static int parse_rgb_text(const char *text, uint8_t *red, uint8_t *green, uint8_t *blue)
{
	char *end = nullptr;
	long values[3];
	const char *cursor = text;

	for (size_t i = 0; i < ARRAY_SIZE(values); ++i) {
		while (*cursor == ' ' || *cursor == ',') {
			++cursor;
		}

		values[i] = strtol(cursor, &end, 10);
		if (end == cursor || values[i] < 0 || values[i] > 255) {
			return -EINVAL;
		}

		cursor = end;
	}

	while (*cursor == ' ' || *cursor == ',') {
		++cursor;
	}

	if (*cursor != '\0') {
		return -EINVAL;
	}

	*red = static_cast<uint8_t>(values[0]);
	*green = static_cast<uint8_t>(values[1]);
	*blue = static_cast<uint8_t>(values[2]);
	return 0;
}

static ssize_t read_rgb_characteristic(bt_conn *conn,
				       const bt_gatt_attr *attr,
				       void *buf,
				       uint16_t len,
				       uint16_t offset)
{
	char text[16];
	int written = snprintf(text,
			       sizeof(text),
			       "%u,%u,%u",
			       current_color.red,
			       current_color.green,
			       current_color.blue);

	return bt_gatt_attr_read(conn, attr, buf, len, offset, text, written);
}

static ssize_t write_rgb_characteristic(bt_conn *conn,
					const bt_gatt_attr *attr,
					const void *buf,
					uint16_t len,
					uint16_t offset,
					uint8_t flags)
{
	if (offset != 0) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	uint8_t red;
	uint8_t green;
	uint8_t blue;

	if (len == 3) {
		const auto *bytes = static_cast<const uint8_t *>(buf);
		red = bytes[0];
		green = bytes[1];
		blue = bytes[2];
	} else {
		if (len >= 16) {
			return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
		}

		char text[16];
		memcpy(text, buf, len);
		text[len] = '\0';

		if (parse_rgb_text(text, &red, &green, &blue) < 0) {
			return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
		}
	}

	int ret = blinker.set(red, green, blue);
	if (ret < 0) {
		return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
	}

	mode = Mode::Manual;
	printf("BLE RGB write: %u, %u, %u\n", red, green, blue);
	return len;
}

BT_GATT_SERVICE_DEFINE(rgb_service,
	BT_GATT_PRIMARY_SERVICE(&rgb_service_uuid),
	BT_GATT_CHARACTERISTIC(&rgb_color_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
			       read_rgb_characteristic,
			       write_rgb_characteristic,
			       nullptr),
);

static int start_bluetooth()
{
	int ret = bt_enable(nullptr);
	if (ret < 0) {
		printf("Failed to initialize Bluetooth: %d\n", ret);
		return ret;
	}

	printf("Bluetooth initialized\n");

	ret = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1,
			      advertising_data,
			      ARRAY_SIZE(advertising_data),
			      scan_response_data,
			      ARRAY_SIZE(scan_response_data));
	if (ret < 0) {
		printf("Failed to start Bluetooth advertising: %d\n", ret);
		return ret;
	}

	printf("Bluetooth advertising as \"%s\"\n", CONFIG_BT_DEVICE_NAME);
	return 0;
}
