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

void controller_t::remote_set_md(const uint8_t *d) {
    auto* data = (data_block_t *) d;
    if (data->cmd != NO_CMD) on_remote_cmd_receive(static_cast<cmd_t>(data->cmd));

    mutex.lock();
    if (data->stt == state) {
        mutex.unlock();
        return;
    }
    state = remote_state = static_cast<state_t>(data->stt);
    mutex.unlock();

    on_remote_state_change(remote_state);
}

void controller_t::remote_get_md(uint8_t *d) {
    auto* data = (data_block_t *) d;
    mutex.lock();
    data->stt = remote_state;
    data->cmd = cmd;
    cmd = NO_CMD;
    mutex.unlock();
}
