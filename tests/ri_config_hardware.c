/* Read-only RI snapshots. No writes to RI or RDRAM configuration registers. */
#include <inttypes.h>
#include <libdragon.h>
#include <stdint.h>
#include <stdio.h>

int main(void) {
    /* Keep these volatile reads ahead of all display and console setup. */
    const uint32_t entry_config = *(volatile const uint32_t *)0xA4700004;
    const uint32_t entry_select = *(volatile const uint32_t *)0xA470000C;
    const int memory_bytes = get_memory_size();

    console_init();
    console_set_debug(false);
    console_set_render_mode(RENDER_MANUAL);
    const uint32_t console_config = *(volatile const uint32_t *)0xA4700004;
    const uint32_t console_select = *(volatile const uint32_t *)0xA470000C;

    printf("READ-ONLY RI ENTRY PROBE\n\n");
    printf("RAM: %d bytes (%d MiB)\n\n", memory_bytes, memory_bytes / (1024 * 1024));
    printf("Before console initialization:\n");
    printf("RI_CONFIG A4700004 = 0x%08" PRIX32 "\n", entry_config);
    printf("RI_SELECT A470000C = 0x%08" PRIX32 "\n\n", entry_select);
    printf("After console initialization:\n");
    printf("RI_CONFIG A4700004 = 0x%08" PRIX32 "\n", console_config);
    printf("RI_SELECT A470000C = 0x%08" PRIX32 "\n\n", console_select);
    printf("Known RAC settings:\n");
    printf("0x00000040: automatic current\n");
    printf("0x00000020: manual midpoint\n\n");
    printf("No RI configuration writes.\n");
    printf("No cart, FS, audio or USB setup.\n");
    printf("Persistent report; power off to exit.\n");
    console_render();
    for (;;) wait_ms(1000);
}
