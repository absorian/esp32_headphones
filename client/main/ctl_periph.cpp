#include "ctl_periph.h"
#include "common_util.h"

#include <impl/concurrency.h>
#include <driver/gpio.h>

#define BTN_PIN GPIO_NUM_27
#define BTN_PRESS_TIME 100
#define BTN_LONG_PRESS_TIME 1000

ESP_EVENT_DEFINE_BASE(CTL_PERIPH);


static void btn_isr_handler(void *par) {
    static time_t debounce_time = 0;

    time_t cur_time = thread_millis();
    if (cur_time - debounce_time > BTN_LONG_PRESS_TIME) {
        // action for long press
    } else if (cur_time - debounce_time > BTN_PRESS_TIME) {
        event_bridge::post_isr(APPLICATION, event_bridge::CTL_SWITCH_SVC, CTL_PERIPH);
    }
    debounce_time = cur_time;
}

void ctl_periph::init() {
    gpio_config_t btn_conf = {};
    btn_conf.intr_type = GPIO_INTR_ANYEDGE;
    btn_conf.mode = GPIO_MODE_INPUT;
    btn_conf.pin_bit_mask = 1ULL << BTN_PIN;
    btn_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    btn_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&btn_conf);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(BTN_PIN, btn_isr_handler, nullptr);
}
