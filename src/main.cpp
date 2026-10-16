#include <zephyr/kernel.h>

int main() {

    printk("main thread started.\n");

    while (true) {
        printk("main thread wakeup.\n");
        k_msleep(500);
    }
}
