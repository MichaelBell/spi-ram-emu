#include <pico/stdlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hardware/pio.h>

extern "C" {
    #include "logic.h"
    #include "sram.h"

    #include "pio_spi.h"
}

uint32_t logic_buf[1024];

int main() {
    stdio_init_all();

    uint8_t* emu_ram = setup_simulated_sram();

    // Init the RAM to known values
    for (int i = 0; i < 65536; ++i) emu_ram[i] = i;

    sleep_ms(5000);

    pio_spi_inst_t spi = {
        .pio = pio1,
        .sm = pio_claim_unused_sm(pio1, true),
        .cs_pin = SIM_SRAM_SPI_CS
    };

    gpio_init(SIM_SRAM_SPI_CS);
    gpio_put(SIM_SRAM_SPI_CS, true);
    gpio_set_dir(SIM_SRAM_SPI_CS, true);

    uint pio_spi_offset = pio_add_program(spi.pio, &spi_cpha0_program);

    pio_spi_setup(&spi);

    constexpr int BUF_LEN = 8 + 4;
    uint8_t out_buf[BUF_LEN], in_buf[BUF_LEN];

    //int logic_sm = pio_claim_unused_sm(pio1, true);
    //logic_analyser_init(pio1, logic_sm, SIM_SRAM_SPI_MOSI, 4, 1);

    int speed_incr = 1;
    for (int divider = 12; divider > 1; divider -= speed_incr) 
    {
        if (divider <= 3) divider = 4;

        pio_spi_init(spi.pio, spi.sm, pio_spi_offset, 8, divider, false, false, SIM_SRAM_SPI_SCK, SIM_SRAM_SPI_MOSI, SIM_SRAM_SPI_MISO);
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
            gpio_put(SIM_SRAM_SPI_CS, false);
            pio_spi_write8_read8_blocking(&spi, out_buf, in_buf, BUF_LEN);
            gpio_put(SIM_SRAM_SPI_CS, true);

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
            gpio_put(SIM_SRAM_SPI_CS, false);
            pio_spi_write8_read8_blocking(&spi, out_buf, in_buf, BUF_LEN);
            gpio_put(SIM_SRAM_SPI_CS, true);

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
            gpio_put(SIM_SRAM_SPI_CS, false);
            pio_spi_write8_read8_blocking(&spi, out_buf, in_buf, BUF_LEN);
            gpio_put(SIM_SRAM_SPI_CS, true);

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