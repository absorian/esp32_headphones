//
// Created by ism on 01.06.2023.
//

#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <cstdint>
#include "impl.h"

class controller_t {
    const char *TAG = "Controller";
public:
    // up to 4 states
    enum state_t {
        DISCONNECT = 0, STALL, SPK_ONLY, FULL
    };
    // up to 8 commands (can be expanded)
    enum cmd_t {
        NO_CMD = 0, PLAY_PAUSE, NEXT, PREV
    };

    controller_t() = default;

    void set_state(state_t s);

    void set_cmd(cmd_t c);

    // There should be metadata size and some format as constants
    void remote_set_md(const uint8_t *data);

    void remote_get_md(uint8_t *data);

    static size_t md_size() {
        return 1;
    }

protected:
    virtual void on_remote_cmd_receive(cmd_t cmd) = 0;

    virtual void on_remote_state_change(state_t state) = 0;

    mutex_t mutex;
private:

    state_t state{};
    state_t remote_state{};
    cmd_t cmd{};
};

#endif //CONTROLLER_H
