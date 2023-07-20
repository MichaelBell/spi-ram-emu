#include <pico/stdlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hardware/pio.h>
#include <hardware/dma.h>
#include <pico/multicore.h>
#include "hardware/structs/bus_ctrl.h"

extern "C" {
    #include "logic.h"

    #include "pio_spi.h"
}

#include "sram.pio.h"

#define SPI_MOSI 18
#define SPI_SCK 19  // Must be MOSI + 1
#define SPI_CS 20
#define SPI_MISO 21

// We define the SMs and DMA channels to avoid memory accesses
// looking them up which saves precious cycles processing the SPI commands.
#define pio_read_sm 1
#define pio_read pio1
#define pio_write_sm 1
#define pio_write pio0
#define pio_write_offset 0  // This must be 0
#define rx_channel 0
#define tx_channel 1
#define tx_channel2 2

int pio_read_offset;

//uint8_t emu_ram[65536];
// TODO: This is a massive hack - sort out the memory map
uint8_t* emu_ram = (uint8_t*)0x20030000;

void init_sram_pio()
{
    pio_read_offset = pio_add_program(pio_read, &sram_read_program);
    pio_sm_claim(pio_read, pio_read_sm);
    pio_add_program_at_offset(pio_write, &sram_write_program, pio_write_offset);
    pio_sm_claim(pio_write, pio_write_sm);

    sram_read_program_init(pio_read, pio_read_sm, pio_read_offset, SPI_MOSI);
    sram_write_program_init(pio_write, pio_write_sm, pio_write_offset, SPI_MOSI, SPI_MISO);
}

void __not_in_flash_func(reset_rx_channel)()
{
    dma_channel_config c = dma_channel_get_default_config(rx_channel);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_dreq(&c, pio_get_dreq(pio_read, pio_read_sm, false));

    dma_channel_configure(
        rx_channel,          // Channel to be configured
        &c,            // The configuration we just created
        NULL,           // The initial write address
        &pio_read->rxf[pio_read_sm],           // The initial read address
        65536, // Number of transfers; in this case each is 1 byte.
        false           // Start immediately.
    );
}

void __not_in_flash_func(reset_tx_channel)()
{
    dma_channel_config c = dma_channel_get_default_config(tx_channel);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_bswap(&c, true);
    channel_config_set_dreq(&c, pio_get_dreq(pio_write, pio_write_sm, true));

    dma_channel_configure(
        tx_channel,          // Channel to be configured
        &c,            // The configuration we just created
        &pio_write->txf[pio_write_sm],           // The initial write address
        NULL,           // The initial read address
        65536, // Number of transfers; in this case each is 1 byte.
        false           // Start immediately.
    );

    c = dma_channel_get_default_config(tx_channel2);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(pio_read, pio_read_sm, false));

    dma_channel_configure(
        tx_channel2,          // Channel to be configured
        &c,            // The configuration we just created
        &dma_hw->ch[tx_channel].al3_read_addr_trig,           // The initial write address
        &pio_read->rxf[pio_read_sm],           // The initial read address
        1, // Number of transfers; in this case each is 1 byte.
        false           // Start immediately.
    );
}

void __scratch_x("core1_main") core1_main()
{
    init_sram_pio();

    dma_channel_claim(rx_channel);
    dma_channel_claim(tx_channel);
    dma_channel_claim(tx_channel2);

    reset_rx_channel();
    reset_tx_channel();

    while (true) {
        uint32_t cmd = pio_sm_get_blocking(pio_read, pio_read_sm);
        if (cmd == 0x3) {
            // Read - this works by transferring the address direct from the Read PIO SM
            // direct to the read address of the transmit DMA channel.
            dma_channel_start(tx_channel2);

            while (gpio_get(SPI_CS) == 0);
            dma_channel_abort(tx_channel);
        }
        else if (cmd == 0xB) {
            // Fast read
            // Need to patch the write program to do extra delay cycles
            pio_write->instr_mem[sram_write_offset_addr_loop_end] = pio_encode_jmp(sram_write_offset_fast_read);

            // And change the write size to 8
            hw_clear_bits(&dma_hw->ch[tx_channel].al1_ctrl, DMA_CH0_CTRL_TRIG_DATA_SIZE_BITS);
            hw_set_bits(&pio_write->sm[pio_write_sm].shiftctrl, 8 << PIO_SM0_SHIFTCTRL_PULL_THRESH_LSB);

            // Transfer the address manually
            uint32_t addr = pio_sm_get_blocking(pio_read, pio_read_sm);
            addr |= pio_sm_get_blocking(pio_read, pio_read_sm);
            dma_hw->ch[tx_channel].al3_read_addr_trig = addr;

            while (gpio_get(SPI_CS) == 0);
            dma_channel_abort(tx_channel);

            // Unpatch the write program
            pio_write->instr_mem[sram_write_offset_addr_loop_end] = pio_encode_jmp_pin(sram_write_offset_addr_two);

            // And change the write size back to 32
            hw_set_bits(&dma_hw->ch[tx_channel].al1_ctrl, 2 << DMA_CH10_CTRL_TRIG_DATA_SIZE_LSB);
            hw_clear_bits(&pio_write->sm[pio_write_sm].shiftctrl, PIO_SM0_SHIFTCTRL_PULL_THRESH_BITS);
        }
        else if (cmd == 0x2) {
            // Write
            //addr += pio_sm_get_blocking(pio, pio_read_sm) << 8;
            uint32_t addr = pio_sm_get_blocking(pio_read, pio_read_sm);
            addr |= pio_sm_get_blocking(pio_read, pio_read_sm);
            dma_hw->ch[rx_channel].al2_write_addr_trig = addr;

            while (gpio_get(SPI_CS) == 0);
            while (!pio_sm_is_rx_fifo_empty(pio_read, pio_read_sm));
            dma_channel_abort(rx_channel);
        }
        else {
            // Ignore unknown command
            while (gpio_get(SPI_CS) == 0);
        }
        pio_sm_set_enabled(pio_write, pio_write_sm, false);
        pio_sm_clear_fifos(pio_write, pio_write_sm);
        pio_sm_restart(pio_write, pio_write_sm);
        pio_sm_exec(pio_write, pio_write_sm, pio_encode_jmp(pio_write_offset));
        pio_sm_set_enabled(pio_write, pio_write_sm, true);
        pio_sm_set_enabled(pio_read, pio_read_sm, false);
        pio_sm_clear_fifos(pio_read, pio_read_sm);
        pio_sm_restart(pio_read, pio_read_sm);
        pio_sm_exec(pio_read, pio_read_sm, pio_encode_jmp(pio_read_offset));
        pio_sm_set_enabled(pio_read, pio_read_sm, true);            
    }
}

uint32_t logic_buf[1024];

int main() {
    stdio_init_all();

    // Init the RAM to known values
    for (int i = 0; i < 65536; ++i) emu_ram[i] = i;

    sleep_ms(5000);

    pio_spi_inst_t spi = {
        .pio = pio1,
        .sm = pio_claim_unused_sm(pio1, true),
        .cs_pin = SPI_CS
    };

    gpio_init(SPI_CS);
    gpio_put(SPI_CS, true);
    gpio_set_dir(SPI_CS, true);

    uint pio_spi_offset = pio_add_program(spi.pio, &spi_cpha0_program);

    hw_set_bits(&bus_ctrl_hw->priority, BUSCTRL_BUS_PRIORITY_DMA_R_BITS | BUSCTRL_BUS_PRIORITY_DMA_W_BITS);
    multicore_launch_core1(core1_main);
    sleep_ms(2);

    pio_spi_setup(&spi);

    constexpr int BUF_LEN = 8 + 4;
    uint8_t out_buf[BUF_LEN], in_buf[BUF_LEN];

    int logic_sm = pio_claim_unused_sm(pio1, true);
    //logic_analyser_init(pio1, logic_sm, SPI_MOSI, 4, 1);

    int speed_incr = 1;
    for (int divider = 12; divider > 1; divider -= speed_incr) 
    {
        if (divider <= 3) divider = 4;

        pio_spi_init(spi.pio, spi.sm, pio_spi_offset, 8, divider, false, false, SPI_SCK, SPI_MOSI, SPI_MISO);
        printf("\nTesting at %.03fMHz\n", 125.f/(2 * divider));
        for (int runs = 0; runs < 2000; ++runs) {
            int addr = (rand() % (65536 - BUF_LEN)) & ~3;

#if 1
            // Read 8 bytes from addr
            //logic_analyser_arm(pio1, logic_sm, 11, logic_buf, 128, SPI_CS, false);
            out_buf[0] = 0x3;
            out_buf[1] = addr >> 8;
            out_buf[2] = addr & 0xff;
            memset(&out_buf[3], 0, 9);
            gpio_put(SPI_CS, false);
            pio_spi_write8_read8_blocking(&spi, out_buf, in_buf, BUF_LEN);
            gpio_put(SPI_CS, true);

#if 0
            printf("Read from addr %04x: ", addr);
            bool ok = true;
            uint8_t* data_buf = &in_buf[3];
            for (int i = 0; i < 8; ++i) {
                printf("%02hhx ", data_buf[i]);
                if (data_buf[i] != emu_ram[addr + i]) ok = false;
            }
            printf("%s", ok ? "OK\n" : "FAIL!\n");
#else
            bool ok = true;
            uint8_t* data_buf = &in_buf[3];
            for (int i = 0; i < 9; ++i) {
                if (data_buf[i] != emu_ram[addr + i]) ok = false;
            }
#endif
            //print_capture_buf(logic_buf, 18, 4, 128*8);
            //sleep_ms(1000);
            //sleep_us(1);
            if (!ok) {
                printf("Read from addr %04x: ", addr);
                uint8_t* data_buf = &in_buf[3];
                for (int i = 0; i < 8; ++i) {
                    printf("%02hhx ", data_buf[i]);
                }
                printf("FAIL!\nExpected:            ");
                for (int i = 0; i < 8; ++i) {
                    printf("%02hhx ", emu_ram[addr + i]);
                }
                printf("\n");
                //print_capture_buf(logic_buf, 18, 4, 128*8);
                divider += 2;
                speed_incr = 1;
                break;
            }
#endif 

#if 1
            // Fast read 8 bytes from addr
            addr = rand() % (65536 - BUF_LEN);
            out_buf[0] = 0xB;
            out_buf[1] = addr >> 8;
            out_buf[2] = addr & 0xff;
            memset(&out_buf[3], 0, 9);
            gpio_put(SPI_CS, false);
            pio_spi_write8_read8_blocking(&spi, out_buf, in_buf, BUF_LEN);
            gpio_put(SPI_CS, true);

#if 0
            printf("Fast read from addr %04x: ", addr);
            bool ok = true;
            uint8_t* data_buf = &in_buf[4];
            for (int i = 0; i < 8; ++i) {
                printf("%02hhx ", data_buf[i]);
                if (data_buf[i] != emu_ram[addr + i]) ok = false;
            }
            printf("%s", ok ? "OK\n" : "FAIL!\n");
#else
            ok = true;
            data_buf = &in_buf[4];
            for (int i = 0; i < 8; ++i) {
                if (data_buf[i] != emu_ram[addr + i]) ok = false;
            }
#endif
            //print_capture_buf(logic_buf, 18, 4, 128*8);
            //sleep_ms(1000);
            //sleep_us(1);
            if (!ok) {
                printf("Fast read from addr %04x: ", addr);
                uint8_t* data_buf = &in_buf[4];
                for (int i = 0; i < 8; ++i) {
                    printf("%02hhx ", data_buf[i]);
                }
                printf("FAIL!\nExpected:                 ");
                for (int i = 0; i < 8; ++i) {
                    printf("%02hhx ", emu_ram[addr + i]);
                }
                printf("\n");
                divider += 2;
                speed_incr = 1;
                break;
            }
#endif

            // Write 8 bytes to addr
            addr = rand() % (65536 - BUF_LEN);
            out_buf[0] = 0x2;
            out_buf[1] = addr >> 8;
            out_buf[2] = addr & 0xff;
            for (int i = 0; i < 8; ++i) {
                out_buf[i+3] = rand();
            }
            gpio_put(SPI_CS, false);
            pio_spi_write8_read8_blocking(&spi, out_buf, in_buf, BUF_LEN);
            gpio_put(SPI_CS, true);

#if 0
            printf("Write to addr %04x: ", addr);
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
            for (int i = 0; i < 9; ++i) {
                if (data_buf[i] != emu_ram[addr + i]) ok = false;
            }            
#endif

            //sleep_us(1);
            if (!ok) {
                printf("Write to addr %x: ", addr);
                uint8_t* data_buf = &out_buf[3];
                for (int i = 0; i < 8; ++i) {
                    printf("%02hhx ", emu_ram[addr + i]);
                }
                printf("FAIL!\nExpected:             ");
                for (int i = 0; i < 8; ++i) {
                    printf("%02hhx ", data_buf[i]);
                }
                printf("\n");
                //divider++;
                speed_incr = 0;
                break;
            }
        }
        if (divider > 50) divider = 50;
    }
}