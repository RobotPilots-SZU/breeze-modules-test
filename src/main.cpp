#include "breeze/referee/messages.h"
#include <zephyr/kernel.h>
#include "breeze/msgpipe/pub.hpp"
#include <breeze/referee/msgpipe.h>

using namespace breeze;

int main() {

    printk("main thread started.\n");
    referee::game_state_info msg {};
    msgpipe::publish(msg,K_TIMEOUT_ABS_MS(1));

    while (true) {
        printk("main thread wakeup.\n");
        k_msleep(500);
    }
}
