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

A write command is the byte 0x02 followed by a 16-bit address, MSB first.  The data to be written to that address follows immediately.  There is no limit to the length of the write, except that it may not go beyond the end of the RAM.  The read is terminated by stopping the SCK and raising CS.

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

# How it works

To meet the tight timing as much is done with PIO and DMA as possible.  There are separate PIO programs handling data in and data out.  The basic flow is:
- Both PIOs wait for CS to go low
- The read PIO reads the first byte and sends it up to core1, which does the appropriate setup for the command while the address is being received
- The read PIO reads all but the last 2 bits of the address.  This is handled on core1 or DMA directly, depending on the command.
- The read PIO reads the last 2 bits of the address.  This is handled on core1 or ignored, depending on the command.  Simultaneously the write PIO branches depending on the value of the last 2 bits so that it can discard the right amount of data from the first 32-bit word for unaligned READ comands.
- Data is then transferred in or out by the appropriate PIO.

All processing on the CPU is handled by core1, interrupts are not used so there is no overhead going into and out of interrupt contexts, instead the core is always waiting for what it is expecting to happen next.  The core1 program is loaded into a dedicated scratch RAM bank so there is no contention with general RAM access, to ensure that the execution timings are very consistent.

Core1 is always used to detect CS going high, terminating the transaction.  It then aborts the DMA transfer and resets the PIOs ready for the next command.

## READ

The hardest timing is for the READ instruction, where the data must be available immediately the next cycle after the end of the address.  To acheive this:
- The memory representing the SRAM is at a fixed location in the RP2040s address space (`0x20030000`), and the `0x2003` is prepended to the received address by the read PIO.
- Once first 14 bits of the address are read, the address is padded with 2 0s to make it complete, and DMA'd to the read address trigger register of the transmit DMA channel, to be sent to the write PIO.
- The DMA will send 32-bit words at a time to the write PIO.
- The write PIO reads the last 2 bits of the address using the `jmp pin` instruction, selecting a branch that will discard an appropriate amount of data from the beginning of the data transferred to it.
- Once this is done the write PIO sends the data a bit at a time until the transfer is aborted.

## WRITE

Relatively speaking, this is simple:
- The first 14 and last 2 bits of the address are combined on core1, which then triggers a DMA channel to read the data from the read PIO into memory.
- The read PIO transfers receives the data a byte at a time until the transfer is aborted.

## FAST READ

A FAST READ command has dummy cycles to allow the address to be processed by the RAM before data needs to be sent.  Because everything is optimized for standard READ commands, FAST READ is implemented as a bit of a hack:
- On receiving the command, core1 patches the write PIO program to jump to an additional delay loop after the first 14 bits of the address, instead of branching on the last 2 bits.
- The DMA and write PIO autopull are reconfigured to transfer a byte at a time (because the DMA start address might not be aligned)
- The DMA is started (using the combined address) ready for the transfer to begin.
- After the transfer is aborted, the write PIO and DMA are reset to the values required for a READ command.

# Limitations / Bugs

Currently the time that CS must be high between operations is uncharacterised, but it is likely to be around 50 SYS clocks.

Aborting operations before the data transfer starts is not supported.
