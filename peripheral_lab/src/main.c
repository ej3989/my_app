/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#define LAB_BUTTON_NODE DT_ALIAS(lab_button)
#define LAB_I2C_NODE DT_ALIAS(lab_i2c)
#define LAB_SPI_NODE DT_ALIAS(lab_spi)

#define DEBOUNCE_MS 30
#define TIMER_PERIOD_MS 1000
#define SPI_MAX_BYTES 16

struct button_monitor {
    const struct gpio_dt_spec *button;
    struct gpio_callback callback;
    struct k_work_delayable debounce_work;
    int last_state;
    atomic_t edge_count;
    atomic_t press_count;
    atomic_t release_count;
};

struct lab_context {
    struct button_monitor button;
    const struct device *i2c;
    const struct device *spi;
    struct k_timer heartbeat_timer;
    atomic_t tick_count;
};

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(LAB_BUTTON_NODE, gpios);
static struct lab_context lab = {
        .button =
                {
                        .button = &button,
                        .last_state = -1,
                },
        .i2c = DEVICE_DT_GET(LAB_I2C_NODE),
        .spi = DEVICE_DT_GET(LAB_SPI_NODE),
};

static void heartbeat_timer_entry(struct k_timer *timer) {
    struct lab_context *ctx = CONTAINER_OF(timer, struct lab_context, heartbeat_timer);

    atomic_inc(&ctx->tick_count);
}

static int button_monitor_read(const struct button_monitor *monitor) {
    return gpio_pin_get_dt(monitor->button);
}

static void button_debounce_entry(struct k_work *work) {
    struct k_work_delayable *delayable = k_work_delayable_from_work(work);
    struct button_monitor *monitor = CONTAINER_OF(delayable, struct button_monitor, debounce_work);
    int state = button_monitor_read(monitor);

    if (state < 0) {
        printf("button read failed: %d\n", state);
        return;
    }

    if (state == monitor->last_state) {
        return;
    }

    monitor->last_state = state;
    if (state != 0) {
        atomic_inc(&monitor->press_count);
        printf("button pressed\n");
    } else {
        atomic_inc(&monitor->release_count);
        printf("button released\n");
    }
}

static void button_gpio_callback(const struct device *dev, struct gpio_callback *callback,
                                 uint32_t pins) {
    struct button_monitor *monitor = CONTAINER_OF(callback, struct button_monitor, callback);

    ARG_UNUSED(dev);
    ARG_UNUSED(pins);

    atomic_inc(&monitor->edge_count);
    (void)k_work_reschedule(&monitor->debounce_work, K_MSEC(DEBOUNCE_MS));
}

static int button_monitor_start(struct button_monitor *monitor) {
    int ret;

    if (!gpio_is_ready_dt(monitor->button)) {
        printf("button GPIO device is not ready\n");
        return -ENODEV;
    }

    ret = gpio_pin_configure_dt(monitor->button, GPIO_INPUT);
    if (ret < 0) {
        printf("button configure failed: %d\n", ret);
        return ret;
    }

    monitor->last_state = button_monitor_read(monitor);
    if (monitor->last_state < 0) {
        printf("initial button read failed: %d\n", monitor->last_state);
        return monitor->last_state;
    }

    k_work_init_delayable(&monitor->debounce_work, button_debounce_entry);
    gpio_init_callback(&monitor->callback, button_gpio_callback, BIT(monitor->button->pin));

    ret = gpio_add_callback(monitor->button->port, &monitor->callback);
    if (ret < 0) {
        printf("button callback add failed: %d\n", ret);
        return ret;
    }

    ret = gpio_pin_interrupt_configure_dt(monitor->button, GPIO_INT_EDGE_BOTH);
    if (ret < 0) {
        printf("button interrupt configure failed: %d\n", ret);
        return ret;
    }

    return 0;
}

static int parse_hex_byte(const char *text, uint8_t *out) {
    char *end = NULL;
    long value = strtol(text, &end, 16);

    if (*text == '\0' || *end != '\0' || value < 0 || value > UINT8_MAX) {
        return -EINVAL;
    }

    *out = (uint8_t)value;
    return 0;
}

static int cmd_status(const struct shell *shell, size_t argc, char **argv) {
    int state;

    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    state = button_monitor_read(&lab.button);
    if (state < 0) {
        shell_error(shell, "button read failed: %d", state);
        return state;
    }

    shell_print(shell, "tick_count = %ld", atomic_get(&lab.tick_count));
    shell_print(shell, "button = %s", state ? "pressed" : "released");
    shell_print(shell, "gpio_edges = %ld", atomic_get(&lab.button.edge_count));
    shell_print(shell, "button_presses = %ld", atomic_get(&lab.button.press_count));
    shell_print(shell, "button_releases = %ld", atomic_get(&lab.button.release_count));
    shell_print(shell, "i2c = %s", lab.i2c->name);
    shell_print(shell, "spi = %s", lab.spi->name);

    return 0;
}

static int cmd_i2c_scan(const struct shell *shell, size_t argc, char **argv) {
    uint8_t dummy;
    int found = 0;

    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    if (!device_is_ready(lab.i2c)) {
        shell_error(shell, "I2C device is not ready");
        return -ENODEV;
    }

    shell_print(shell, "Scanning %s", lab.i2c->name);

    for (uint8_t addr = 0x03; addr <= 0x77; addr++) {
        int ret = i2c_write_read(lab.i2c, addr, NULL, 0, &dummy, 1);

        if (ret == 0) {
            shell_print(shell, "found: 0x%02x", addr);
            found++;
        }
    }

    if (found == 0) {
        shell_print(shell, "no I2C devices found");
    }

    return 0;
}

static int cmd_spi_xfer(const struct shell *shell, size_t argc, char **argv) {
    uint8_t tx_buf[SPI_MAX_BYTES];
    uint8_t rx_buf[SPI_MAX_BYTES] = {0};
    struct spi_buf tx_spi_buf = {
            .buf = tx_buf,
            .len = argc - 1,
    };
    struct spi_buf rx_spi_buf = {
            .buf = rx_buf,
            .len = argc - 1,
    };
    struct spi_buf_set tx = {
            .buffers = &tx_spi_buf,
            .count = 1,
    };
    struct spi_buf_set rx = {
            .buffers = &rx_spi_buf,
            .count = 1,
    };
    struct spi_config config = {
            .frequency = 1000000,
            .operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
            .slave = 0,
    };
    int ret;

    if (argc < 2) {
        shell_error(shell, "usage: lab spi_xfer <hex-byte> [hex-byte...]");
        return -EINVAL;
    }

    if (argc - 1 > SPI_MAX_BYTES) {
        shell_error(shell, "max %d bytes", SPI_MAX_BYTES);
        return -EINVAL;
    }

    if (!device_is_ready(lab.spi)) {
        shell_error(shell, "SPI device is not ready");
        return -ENODEV;
    }

    for (size_t i = 1; i < argc; i++) {
        ret = parse_hex_byte(argv[i], &tx_buf[i - 1]);
        if (ret < 0) {
            shell_error(shell, "invalid hex byte: %s", argv[i]);
            return ret;
        }
    }

    ret = spi_transceive(lab.spi, &config, &tx, &rx);
    if (ret < 0) {
        shell_error(shell, "SPI transfer failed: %d", ret);
        return ret;
    }

    shell_fprintf(shell, SHELL_NORMAL, "rx:");
    for (size_t i = 0; i < argc - 1; i++) {
        shell_fprintf(shell, SHELL_NORMAL, " %02x", rx_buf[i]);
    }
    shell_fprintf(shell, SHELL_NORMAL, "\n");

    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
        lab_cmds, SHELL_CMD(status, NULL, "Show timer, button, I2C, and SPI status", cmd_status),
        SHELL_CMD(i2c_scan, NULL, "Scan the I2C bus", cmd_i2c_scan),
        SHELL_CMD(spi_xfer, NULL, "Transfer hex bytes over SPI", cmd_spi_xfer),
        SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(lab, &lab_cmds, "Peripheral lab commands", NULL);

int main(void) {
    int ret;

    printf("Peripheral Lab started\n");
    printf("Commands: lab status, lab i2c_scan, lab spi_xfer aa 55\n");

    ret = button_monitor_start(&lab.button);
    if (ret < 0) {
        printf("button monitor start failed: %d\n", ret);
        return 0;
    }

    if (!device_is_ready(lab.i2c)) {
        printf("I2C device is not ready: %s\n", lab.i2c->name);
    }

    if (!device_is_ready(lab.spi)) {
        printf("SPI device is not ready: %s\n", lab.spi->name);
    }

    k_timer_init(&lab.heartbeat_timer, heartbeat_timer_entry, NULL);
    k_timer_start(&lab.heartbeat_timer, K_MSEC(TIMER_PERIOD_MS), K_MSEC(TIMER_PERIOD_MS));

    return 0;
}
