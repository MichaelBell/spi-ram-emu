# Simulated SPI RAM on RP2040

An SPI RAM implementation for RP2040.

This project allows the RP2040 to act as if it were an serial SPI RAM, similar to a [23LC512](https://ww1.microchip.com/downloads/aemDocuments/documents/MPD/ProductDocuments/DataSheets/23A512-23LC512-512-Kbit-SPI-Serial-SRAM-with-SDI-and-SQI-Interface-20005155C.pdf).

The commands READ (0x03), WRITE (0x02) and FAST READ (0x0B) are implented.  The RAM operates in sequential mode, and operations must not go beyond the end of the RAM.

Only SPI mode is supported (no DSPI/QSPI).

The maximum clock rate supported depends on the system clock speed and the operation:

| Operation | Max speed | Max speed at 125MHz SYS clock |
| --------- | --------- | ----------------------------- |
| READ  | SYS clock / 10 | 12.5 MHz |
| READ (aligned) | SYS clock / 8 | 15.6 MHz |
| FAST READ | SYS clock / 8 | 15.6 MHz |
| WRITE | SYS clock / 6 | 20.8 MHz |

A READ is considered to be aligned if the start address is a multiple of 4.  There is no requirement for the length of an aligned read to be a multiple of 4 bytes.

# Command details

The SPI slave works in SPI mode 0 or 3 - data is transferred in both directions on the rising edge of SCK.  All and addresses are transferred MSB first.

## READ

A read command is the byte 0x03 followed by a 16-bit address, MSB first.  Data transfer begins immediately with no delay cycles.  There is no limit to the length of the read, except that it may not go beyond the end of the RAM.  The read is terminated by stopping the SCK and raising CS.

## FAST READ

A fast read command is the byte 0x0B followed by a 16-bit address, MSB first.  There are then 8 delay cycles (one dummy byte transfer) before data transfer begins.  Otherwise it is the same as a READ, except it might work at a faster clock rate.

## WRITE

A write command is they byte 0x02 followed by a 16-bit address, MSB first.  The data to be written to that address follows immediately.  There is no limit to the length of the write, except that it may not go beyond the end of the RAM.  The read is terminated by stopping the SCK and raising CS.

# Using in your own project

It is easiest to integrate by copying the 4 files beginning sram from this project into your project.  Alternatively, you could include this project as a submodule.

You will need to include the `sram.c` file in your source files, and add
```
pico_generate_pio_header(${NAME} ${CMAKE_CURRENT_LIST_DIR}/sram.pio)

set_target_properties(${NAME} PROPERTIES PICO_TARGET_LINKER_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/sram_memmap.ld)
pico_add_link_depend(${NAME} ${CMAKE_CURRENT_LIST_DIR}/sram_memmap.ld)
```
to your CMakeLists.txt to use a custom memory map that reserves the 64kB memory region for the RAM.

Configure the pins, and if necessary DMA channels and PIO SMs by editing `sram.h`.

Start the RAM by including `sram.h` and calling `setup_simulated_sram()`.  This sets up the PIOs and launches the handler on core1.

Note that in order to meet the strict timing requirements, the RAM simulation must have dedicated use of core1, and it uses most of the PIO instructions on both PIOs, though there are still a few instructions spare.

# Limitations / Bugs

Currently the time that CS must be high between operations is uncharacterised, but it is likely to be around 50 SYS clocks.

Aborting operations before the data transfer starts is not supported.