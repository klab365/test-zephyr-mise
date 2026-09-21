#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "app_events.h"
#include "hmi.h"

enum matrix_symbol {
    MATRIX_SYMBOL_OFF = 1,
    MATRIX_SYMBOL_SMILE,
    MATRIX_SYMBOL_HEART,
    MATRIX_SYMBOL_CHECK,
    MATRIX_SYMBOL_CROSS,
};

IPC_EVENT_DEFINE(LongPressEvent);

#define HMI_BUTTON_NODE DT_ALIAS(sw0)
#define HMI_MATRIX_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(nordic_nrf_led_matrix)
#define HMI_LONG_PRESS_TIMEOUT K_SECONDS(2)
#define HMI_DOUBLE_CLICK_TIMEOUT K_MSEC(350)
#define HMI_KNIGHT_RIDER_INTERVAL K_MSEC(120)

#define PIXEL_BIT(idx, val) (val ? BIT(idx) : 0)
#define PIXEL_MASK(...) FOR_EACH_IDX(PIXEL_BIT, (|), __VA_ARGS__)

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(HMI_BUTTON_NODE, gpios);
static const struct device *const matrix = DEVICE_DT_GET(HMI_MATRIX_NODE);
static struct gpio_callback button_cb;
static struct k_work_delayable long_press_work;
static struct k_work_delayable click_work;
static struct k_work_delayable knight_rider_work;
static bool click_pending;
static bool long_press_fired;
static bool knight_rider_active;
static bool matrix_on;
static uint8_t knight_rider_pos;
static int8_t knight_rider_dir = 1;

static const struct display_buffer_descriptor matrix_desc = {
    .buf_size = 5,
    .width = 5,
    .height = 5,
    .pitch = 8,
};

static const uint8_t symbol_off[5] = { 0 };
static const uint8_t symbol_smile[5] = {
    PIXEL_MASK(0, 1, 0, 1, 0),
    PIXEL_MASK(0, 1, 0, 1, 0),
    PIXEL_MASK(0, 0, 0, 0, 0),
    PIXEL_MASK(1, 0, 0, 0, 1),
    PIXEL_MASK(0, 1, 1, 1, 0),
};
static const uint8_t symbol_heart[5] = {
    PIXEL_MASK(0, 1, 0, 1, 0),
    PIXEL_MASK(1, 1, 1, 1, 1),
    PIXEL_MASK(1, 1, 1, 1, 1),
    PIXEL_MASK(0, 1, 1, 1, 0),
    PIXEL_MASK(0, 0, 1, 0, 0),
};
static const uint8_t symbol_check[5] = {
    PIXEL_MASK(0, 0, 0, 0, 0),
    PIXEL_MASK(0, 0, 0, 0, 1),
    PIXEL_MASK(0, 0, 0, 1, 0),
    PIXEL_MASK(1, 0, 1, 0, 0),
    PIXEL_MASK(0, 1, 1, 0, 0),
};
static const uint8_t symbol_cross[5] = {
    PIXEL_MASK(1, 0, 0, 0, 1),
    PIXEL_MASK(0, 1, 0, 1, 0),
    PIXEL_MASK(0, 0, 1, 0, 0),
    PIXEL_MASK(0, 1, 0, 1, 0),
    PIXEL_MASK(1, 0, 0, 0, 1),
};

static int write_matrix_pattern(const uint8_t pattern[5], bool on)
{
    int rc = display_write(matrix, 0, 0, &matrix_desc, pattern);
    if (rc != 0) {
        return rc;
    }

    return on ? display_blanking_off(matrix) : display_blanking_on(matrix);
}

static int write_matrix_symbol(enum matrix_symbol symbol)
{
    const uint8_t *pattern;
    bool on;

    switch (symbol) {
    case MATRIX_SYMBOL_OFF:
        pattern = symbol_off;
        on = false;
        break;
    case MATRIX_SYMBOL_SMILE:
        pattern = symbol_smile;
        on = true;
        break;
    case MATRIX_SYMBOL_HEART:
        pattern = symbol_heart;
        on = true;
        break;
    case MATRIX_SYMBOL_CHECK:
        pattern = symbol_check;
        on = true;
        break;
    case MATRIX_SYMBOL_CROSS:
        pattern = symbol_cross;
        on = true;
        break;
    default:
        return -EINVAL;
    }

    int rc = display_set_brightness(matrix, 0x7f);
    if (rc != 0) {
        return rc;
    }

    return write_matrix_pattern(pattern, on);
}

static int write_matrix(bool on)
{
    return write_matrix_symbol(on ? MATRIX_SYMBOL_SMILE : MATRIX_SYMBOL_OFF);
}

static void start_knight_rider(void)
{
    knight_rider_active = true;
    knight_rider_pos = 0U;
    knight_rider_dir = 1;
    (void) k_work_reschedule(&knight_rider_work, K_NO_WAIT);
}

static void click_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    click_pending = false;
}

static void knight_rider_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    if (!knight_rider_active) {
        return;
    }

    uint8_t frame[5] = { 0 };
    frame[1] = BIT(knight_rider_pos);
    frame[2] = BIT(knight_rider_pos);
    frame[3] = BIT(knight_rider_pos);

    int rc = write_matrix_pattern(frame, true);
    if (rc != 0) {
        printk("hmi actor: failed to update knight rider frame: %d\n", rc);
    }

    if (knight_rider_pos == 4U) {
        knight_rider_dir = -1;
    } else if (knight_rider_pos == 0U) {
        knight_rider_dir = 1;
    }

    knight_rider_pos += knight_rider_dir;
    (void) k_work_reschedule(&knight_rider_work, HMI_KNIGHT_RIDER_INTERVAL);
}

static void long_press_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    if (gpio_pin_get_dt(&button) <= 0) {
        return;
    }

    long_press_fired = true;
    click_pending = false;
    knight_rider_active = false;
    (void) k_work_cancel_delayable(&click_work);
    (void) k_work_cancel_delayable(&knight_rider_work);

    matrix_on = !matrix_on;
    int rc = write_matrix(matrix_on);
    if (rc != 0) {
        printk("hmi actor: failed to update matrix: %d\n", rc);
    }

    LongPressEvent_payload_t event = {};
    rc = ipc_publish(LongPressEvent, event);
    if (rc != 0) {
        printk("hmi actor: failed to publish long press: %d\n", rc);
    }

    printk("hmi actor: long press detected, matrix %s\n", matrix_on ? "on" : "off");
}

static void button_pressed(const struct device *dev, struct gpio_callback *cb,
                           uint32_t pins)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);

    int pressed = gpio_pin_get_dt(&button);
    if (pressed > 0) {
        long_press_fired = false;
        k_work_reschedule(&long_press_work, HMI_LONG_PRESS_TIMEOUT);
    } else if (pressed == 0) {
        (void) k_work_cancel_delayable(&long_press_work);
        if (long_press_fired) {
            return;
        }

        if (click_pending) {
            click_pending = false;
            (void) k_work_cancel_delayable(&click_work);
            start_knight_rider();
            printk("hmi actor: double click detected, knight rider started\n");
            return;
        }

        click_pending = true;
        (void) k_work_reschedule(&click_work, HMI_DOUBLE_CLICK_TIMEOUT);
    }
}

static int init_hardware(void)
{
    if (!gpio_is_ready_dt(&button)) {
        printk("hmi actor: button device not ready\n");
        return -ENODEV;
    }

    if (!device_is_ready(matrix)) {
        printk("hmi actor: LED matrix device not ready\n");
        return -ENODEV;
    }

    int rc = display_set_pixel_format(matrix, PIXEL_FORMAT_MONO01);
    if (rc != 0) {
        printk("hmi actor: failed to set matrix pixel format: %d\n", rc);
        return rc;
    }

    rc = display_set_brightness(matrix, 0x7f);
    if (rc != 0) {
        printk("hmi actor: failed to set matrix brightness: %d\n", rc);
        return rc;
    }

    rc = write_matrix(false);
    if (rc != 0) {
        printk("hmi actor: failed to clear matrix: %d\n", rc);
        return rc;
    }

    rc = gpio_pin_configure_dt(&button, GPIO_INPUT);
    if (rc != 0) {
        printk("hmi actor: failed to configure button: %d\n", rc);
        return rc;
    }

    k_work_init_delayable(&long_press_work, long_press_work_handler);
    k_work_init_delayable(&click_work, click_work_handler);
    k_work_init_delayable(&knight_rider_work, knight_rider_work_handler);

    gpio_init_callback(&button_cb, button_pressed, BIT(button.pin));
    rc = gpio_add_callback(button.port, &button_cb);
    if (rc != 0) {
        printk("hmi actor: failed to add button callback: %d\n", rc);
        return rc;
    }

    rc = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_BOTH);
    if (rc != 0) {
        printk("hmi actor: failed to configure button interrupt: %d\n", rc);
        return rc;
    }
    return 0;
}

IPC_ACTOR_DEFINE(hmi_actor, "hmi", 1024, K_PRIO_PREEMPT(7), 4,
                 IPC_MESSAGE_MAX(AppMgmtRequestEvent, LongPressEvent));

IPC_START_HOOK(hmi_actor, on_hmi_start)
{
    ARG_UNUSED(self);

    int rc = init_hardware();
    if (rc != 0) {
        return;
    }

    printk("hmi actor: started\n");
}

IPC_ACTOR_HANDLE(hmi_actor, LongPressEvent, on_long_press_event)
{
    ARG_UNUSED(self);
    ARG_UNUSED(msg);
    ARG_UNUSED(raw_msg);

    printk("hmi actor: received long press event\n");
}

IPC_ACTOR_HANDLE(hmi_actor, AppMgmtRequestEvent, on_app_mgmt_request)
{
    ARG_UNUSED(self);
    ARG_UNUSED(raw_msg);

    if (msg->command != APP_MGMT_CMD_SET_MATRIX_SYMBOL) {
        return;
    }

    knight_rider_active = false;
    (void)k_work_cancel_delayable(&knight_rider_work);

    int rc = write_matrix_symbol((enum matrix_symbol)msg->value);
    if (rc == 0) {
        matrix_on = (msg->value != MATRIX_SYMBOL_OFF);
    } else {
        printk("hmi actor: invalid matrix symbol: %u\n", msg->value);
    }

    app_mgmt_respond(msg->transaction, rc, msg->value, NULL, 0U);
}
