#include <pico/stdlib.h>
#include <stdio.h>
#include <stdlib.h>

#include <hardware/spi.h>
#include <hardware/dma.h>
#include <pico/multicore.h>
#include "hardware/structs/bus_ctrl.h"

#include "sram.pio.h"

#define SPI_MOSI 2
#define SPI_SCK 3  // Must be MOSI + 1
#define SPI_CS 4
#define SPI_MISO 5

PIO pio = pio0;
int pio_read_sm;
int pio_read_offset;
int pio_write_sm;
int pio_write_offset;

int rx_channel;
int tx_channel, tx_channel2;

uint8_t emu_ram[65536];

void init_sram_pio()
{
    pio_read_offset = pio_add_program(pio, &sram_read_program);
    pio_read_sm = pio_claim_unused_sm(pio, true);
    pio_write_offset = pio_add_program(pio, &sram_write_program);
    pio_write_sm = pio_claim_unused_sm(pio, true);

    sram_read_program_init(pio, pio_read_sm, pio_read_offset, SPI_MOSI);
    sram_write_program_init(pio, pio_write_sm, pio_write_offset, SPI_MOSI, SPI_MISO);
}

void __not_in_flash_func(reset_rx_channel)()
{
    dma_channel_config c = dma_channel_get_default_config(rx_channel);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_dreq(&c, pio_get_dreq(pio, pio_read_sm, false));

    dma_channel_configure(
        rx_channel,          // Channel to be configured
        &c,            // The configuration we just created
        NULL,           // The initial write address
        &pio->rxf[pio_read_sm],           // The initial read address
        65536, // Number of transfers; in this case each is 1 byte.
        false           // Start immediately.
    );
}

void __not_in_flash_func(reset_tx_channel)()
{
    dma_channel_config c = dma_channel_get_default_config(tx_channel);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(pio, pio_write_sm, true));

    dma_channel_configure(
        tx_channel,          // Channel to be configured
        &c,            // The configuration we just created
        &pio->txf[pio_write_sm],           // The initial write address
        NULL,           // The initial read address
        65536, // Number of transfers; in this case each is 1 byte.
        false           // Start immediately.
    );
}

void __scratch_x("core1_main") core1_main()
{
    init_sram_pio();

    rx_channel = 0; dma_channel_claim(0);
    tx_channel = 1; dma_channel_claim(1);

    reset_rx_channel();
    reset_tx_channel();

    while (true) {
        uint32_t cmd = pio_sm_get_blocking(pio, pio_read_sm);
        uintptr_t addr = (uintptr_t)emu_ram;
        if (cmd == 0x3) {
            // Read
            addr += pio_sm_get_blocking(pio, pio_read_sm) << 8;
            addr += pio_sm_get_blocking(pio, pio_read_sm);
            //printf("R%04x\n", addr - (uintptr_t)emu_ram);

            dma_hw->ch[1].al3_read_addr_trig = addr;

            while (gpio_get(SPI_CS) == 0);
            dma_channel_abort(1);
            pio_sm_set_enabled(pio, pio_write_sm, false);
            pio_sm_clear_fifos(pio, pio_write_sm);
            pio_sm_restart(pio, pio_write_sm);
            pio_sm_exec(pio, pio_write_sm, pio_encode_jmp(pio_write_offset));
            pio_sm_set_enabled(pio, pio_write_sm, true);
        }
        else if (cmd == 0x2) {
            // Write
            addr += pio_sm_get_blocking(pio, pio_read_sm) << 8;
            addr += pio_sm_get_blocking(pio, pio_read_sm);
            dma_hw->ch[0].al2_write_addr_trig = addr;

            while (gpio_get(SPI_CS) == 0);
            while (!pio_sm_is_rx_fifo_empty(pio, pio_read_sm));
            dma_channel_abort(0);
        }
        else {
            // Ignore unknown command
            while (gpio_get(SPI_CS) == 0);
        }
        pio_sm_set_enabled(pio, pio_read_sm, false);
        pio_sm_clear_fifos(pio, pio_read_sm);
        pio_sm_restart(pio, pio_read_sm);
        pio_sm_exec(pio, pio_read_sm, pio_encode_jmp(pio_read_offset));
        pio_sm_set_enabled(pio, pio_read_sm, true);            
    }
}

int main() {
    stdio_init_all();

    // Init the RAM to known values
    for (int i = 0; i < 65536; ++i) emu_ram[i] = i;

    sleep_ms(5000);

    gpio_init(21);
    gpio_put(21, true);
    gpio_set_dir(21, true);

    gpio_set_function(18, GPIO_FUNC_SPI);
    gpio_set_function(19, GPIO_FUNC_SPI);
    gpio_set_function(20, GPIO_FUNC_SPI);

    hw_set_bits(&bus_ctrl_hw->priority, BUSCTRL_BUS_PRIORITY_PROC1_BITS);
    multicore_launch_core1(core1_main);
    sleep_ms(2);

    constexpr int BUF_LEN = 8 + 3;
    uint8_t out_buf[BUF_LEN], in_buf[BUF_LEN];

    int speed_incr = 1;
    for (int speed = 1; speed < 150; speed += speed_incr) {
        spi_init(spi0, speed * 100 * 1000);
        printf("\nTesting at %d00kHz\n", speed);
        for (int runs = 0; runs < 1000; ++runs) {
            int addr = rand() % (65536 - BUF_LEN);

            // Read 8 bytes from addr
            out_buf[0] = 0x3;
            out_buf[1] = addr >> 8;
            out_buf[2] = addr & 0xff;
            gpio_put(21, false);
            spi_write_read_blocking(spi0, out_buf, in_buf, BUF_LEN);
            gpio_put(21, true);

#if 0
            printf("Read from addr %x: ", addr);
            bool ok = true;
            uint8_t* data_buf = &in_buf[3];
            for (int i = 0; i < 8; ++i) {
                printf("%02hhx ", data_buf[i]);
                if (data_buf[i] != emu_ram[addr + i]) ok = false;
            }
            printf("%s", ok ? "OK\r" : "FAIL!\n");
#else
            bool ok = true;
            uint8_t* data_buf = &in_buf[3];
            for (int i = 0; i < 8; ++i) {
                if (data_buf[i] != emu_ram[addr + i]) ok = false;
            }
#endif
            
            //sleep_ms(1000);
            //sleep_us(1);
            if (!ok) {
                printf("Read from addr %x: FAIL!\n", addr);
                speed--;
                speed_incr = 0;
                break;
            }

            // Write 8 bytes to addr
            addr = rand() % (65536 - BUF_LEN);
            out_buf[0] = 0x2;
            out_buf[1] = addr >> 8;
            out_buf[2] = addr & 0xff;
            for (int i = 0; i < 8; ++i) {
                out_buf[i+3] = rand();
            }
            gpio_put(21, false);
            spi_write_read_blocking(spi0, out_buf, in_buf, BUF_LEN);
            gpio_put(21, true);

#if 0
            printf("Write to addr %x: ", addr);
            ok = true;
            data_buf = &out_buf[3];
            for (int i = 0; i < 8; ++i) {
                printf("%02hhx ", emu_ram[addr + i]);
                if (data_buf[i] != emu_ram[addr + i]) ok = false;
            }
            printf("%s", ok ? "OK\r" : "FAIL!\n");
#else
            ok = true;
            data_buf = &out_buf[3];
            for (int i = 0; i < 8; ++i) {
                if (data_buf[i] != emu_ram[addr + i]) ok = false;
            }            
#endif

            //sleep_us(1);
            if (!ok) {
                printf("Write to addr %x: FAIL!\n", addr);
                speed--;
                speed_incr = 0;
                break;
            }
        }
        if (speed < 1) speed = 1;
    }
}