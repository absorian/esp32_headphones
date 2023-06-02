//
// Created by ism on 01.06.2023.
//

#include "controller.h"

void controller_t::set_state(controller_t::state_t s) {
    mutex.lock();
    remote_state = s;
    mutex.unlock();
}

void controller_t::set_cmd(controller_t::cmd_t c) {
    mutex.lock();
    if (cmd != NO_CMD) {
        logi(TAG, "The previous command was not obtained");
    }
    cmd = c;
    mutex.unlock();
}

void controller_t::remote_set_md(const uint8_t *data) {
    auto c = static_cast<cmd_t>((*data & 0b00011100) >> 2);
    if (c != NO_CMD) on_remote_cmd_receive(c);

    auto s = static_cast<state_t>(*data & 0b00000011);
    mutex.lock();
    if (s == state) {
        mutex.unlock();
        return;
    }
    remote_state = s;
    state = s;
    mutex.unlock();
    on_remote_state_change(s);
}

void controller_t::remote_get_md(uint8_t *data) {
//        *data = 0;
    mutex.lock();
    *data = static_cast<uint8_t>(remote_state);
    *data = *data | (static_cast<uint8_t>(cmd) << 2);
    cmd = NO_CMD;
    mutex.unlock();
}
