ISP-55e0 - An ISP flashing tool for the WCH CH55x, CH57x and CH32Fx
===================================================================

This tool is meant to flash the WinChipHead CH55x / CH57x / CH32Fx
series, such as the CH551, CH552, CH554, CH559 or CH579, through USB
or a serial port. Serial backends are provided for Windows (Win32 COM API)
and POSIX systems (termios). USB uses libusb.

When set in ISP mode, the chip creates a 4348:55e0 USB device, hence
the name for that project.

A few similar tools exist for the CH55x series, but all appear
unmaintained and / or are without a proper license:

  - https://github.com/rgwan/librech551
  - https://github.com/NgoHungCuong/vnproch551
  - https://github.com/LoveMHz/vnproch55x     (descendant of vnproch551)
  - https://github.com/MarsTechHAN/ch552tool  (in python)
  - https://github.com/juliuswwj/wchprog      (in python)
  - https://github.com/SIGINT112/wchprog      (fork of wchprog, w/ serial support)

So the wheel has to be re-invented.

See also `wchisp` (https://github.com/ch32-rs/wchisp) which is a new
supported tool, written in rust, and with a license.

isp55e0 has been tested on:

|Chip        |Bootloader|USB|Serial|
|------------|----------|---|------|
|CH549       |2.4.0     |+  |      |
|CH551       |2.3.1     |+  |      |
|CH552       |2.3.1     |+  |      |
|CH552       |2.4.0     |+  |      |
|CH554       |2.4.0     |+  |      |
|CH559       |2.4.0     |+  |      |
|CH573       |2.8.0     |+  |      |
|CH579       |2.8.0     |+  |      |
|CH582       |2.4.0     |+  |+     |
|CH583       |2.4.0     |+  |+     |
|CH591       |2.3.0     |+  |+     |
|CH592       |2.3.0     |+  |+     |
|CH584       |2.3.0     |+  |+     |
|CH58X       |2.4.0     |+  |+     |
|CH585       |2.3.0     |+  |+     |
|CH587       |2.4.0     |+  |+     |
|CH32F103C8T6|2.3.1     |+  |      |
|CH32F103C8T6|2.5.0     |+  |      |
|CH32V103C8T6|2.6.0     |+  |      |
|CH32V203C8T6|2.7.0     |+  |      |
|CH32V203G8R6|2.6.0     |+  |      |
|CH32V307VCT6|2.9.0     |+  |      |
|CH32X035F8U6|2.6.0     |+  |      |


Build
-----

It needs the libusb development package. On Debian / Ubuntu, install
the "libusb-1.0-0-dev" package. In rpm world, it is usually called
"libusb1-devel".

Type:

>  make

Build it without libusb:

>  make WITHOUT_USB=1

### Windows / MinGW64

In an MSYS2 MinGW64 shell with GCC, GNU make and the MinGW64 libusb
package installed:

```bash
mingw32-make CC=gcc
# Serial-only build, without a libusb dependency:
mingw32-make CC=gcc WITHOUT_USB=1
# Hardware-independent UART framing tests:
mingw32-make CC=gcc test
# CLI validation (run against the serial-only executable):
mingw32-make CC=gcc WITHOUT_USB=1
python -I tests/test_cli.py ./isp55e0.exe
```

The compiler target selects `transfer_uart_windows.c` or
`transfer_uart_posix.c` automatically. USB-enabled builds also compile
`transfer_usb_libusb.c`. Build variants have separate object directories;
cleaning is not required when switching `WITHOUT_USB`.

A USB-enabled executable requires libusb and its runtime dependencies.
The USB device must use a driver supported by libusb. Serial-only builds
use the Windows COM API and do not need libusb.

### macOS

1. install `libusb`

   ```bash
   brew install libusb
   ```

2. build

   ```bash
   make CC=clang CPPFLAGS="-I/opt/homebrew/include" LDFLAGS="-L/opt/homebrew/lib"
   ```

UDEV configuration
------------------

By default, a linux system will only allow the root user to program
these chips. If you'd like to program these chips as a regular user, a
udev rule must be added. As root, issue the following command:

    cp 99-wch-isp.rules /etc/udev/rules.d/99-wch-isp.rules

Edit this file as desired.


Usage
-----

Help:
```
  ./isp55e0 --help

  ISP programmer for some WinChipHead MCUs
  Options:
    --port, -p          serial port (COM3 or /dev/ttyUSB0)
    --transport, -t     backend: uart or libusb (default: auto)
    --code-flash, -f    firmware to flash
    --code-verify, -c   verify existing firwmare
    --data-flash, -k    data to flash
    --data-verify, -l   verify existing data
    --data-dump, -m     dump the data flash to a file
    --debug, -d         turn debug traces on
    --speed, -s         UART baudrate, any positive value the adapter supports (default 115200)
    --user-config, -u   User-level Configuration, e.g. 4d
    --write-protection-config, -w Write-Protection Cfg., e.g. ffffffff
    --version, -v       version
    --help, -h          this help
```

### Selecting a transport

Without `--transport`, specifying `--port` selects UART; otherwise libusb
is selected. An explicit backend can be selected with `--transport uart`
or `--transport libusb`.

```bash
# Windows: query over a serial port (COM10 and higher are supported too)
./isp55e0.exe --port COM3
# Windows: flash over UART, switching from the initial 115200 baud
./isp55e0.exe --port COM10 --speed 921600 --code-flash fw.bin
# POSIX
./isp55e0 --port /dev/ttyUSB0 --speed 230400 --code-flash fw.bin
# USB
./isp55e0 --transport libusb --code-flash fw.bin
```

UART opens at 115200 baud, 8N1, no flow control. Windows disables DTR/RTS;
the POSIX backend does not explicitly change their line states. Drivers may
still change these signals when a port is opened or closed.
`--speed` accepts any positive integer; it sends the ISP baud-change
command before changing the host serial rate. A successful session
restores 115200 before reboot/close. On failure the port is closed, but
the MCU may need resetting to restore its initial baudrate. Whether a
given rate actually works is decided at runtime by the host serial
driver: Windows forwards the rate to the driver, Linux uses the BOTHER
extension for non-standard rates (common architectures), and macOS uses
plain numeric speeds.
`--speed` is only valid with UART.

### UART baud rates

The tool accepts any positive number for `--speed`, but the rate that
actually works is limited by **every link in the chain**: the MCU's ISP
bootloader, the USB-to-UART bridge (or other serial hardware), and its
driver. The effective maximum is the lowest of them all.

Examples:

| Component | Maximum UART baud |
|-----------|-------------------|
| CH32V20x ISP bootloader | 6000000 |
| CH592 ISP bootloader | 6000000 |
| CH9102 USB-to-UART bridge | 6000000 |
| CH340 USB-to-UART bridge | 2000000 |

So even though a CH32V20x or CH592 can be programmed at 6 Mbaud, going
through a classic CH340 bridge caps the link at 2 Mbaud; use a bridge in
the same class as the chip (for example a CH9102) to reach the chip's
full rate. Older chip families also top out lower than the figures above.

If a rate is refused by the device, or communication times out after the
switch, reset or power-cycle the board: the bootloader returns to its
default 115200 baud after reset.

### Transport implementation

- `transfer.h` / `transfer.c`: backend operations, selection and debug tracing.
- `transfer_uart.c`: shared UART framing and checksum validation.
- `transfer_uart_windows.c`: Windows COM configuration and timed I/O.
- `transfer_uart_posix.c`: termios configuration and poll-based I/O.
- `transfer_usb_libusb.c`: USB discovery, interface claim and bulk transfers.

ISP command structures and baud-change commands remain in the protocol
layer; backends exchange unframed ISP request/response payloads. Each
backend owns its private handle in `transport.context` and returns zero
or a negative errno value, with diagnostics in `transport.error`.

To add a WCH-driver backend later, implement `transfer_usb_wch.c` with a
`const struct transport_ops transfer_usb_wch_ops`, register it in the
backend table in `transfer.c`, and add its build flags/libraries to the
Makefile. It can then be selected by its backend name without changing
ISP commands. No WCH DLL/API implementation is included yet; the exact
supported driver/device API must be established first.

Query the device:

>  ./isp55e0

Flash some firmware, which will also read back to verify it:

>  ./isp55e0 -f fw.bin

Verify an existing firmware against a flashed firmware:

>  ./isp55e0 -c fw.bin

Note that if the verification fails, all subsequent verifications will
fail. The chip has to be power cycled to fix the issue.


On flashing iHex files
----------------------

iHex is not supported, but objcopy can be used to convert an iHex file
to a regular raw file:

>  objcopy -I ihex -O binary xxx.hex xxx.bin


Note on the CH32F103C8T6 BluePill clone
---------------------------------------

The CH32F103 chip has 2 USB ports. Some breakout boards have 1 USB
port, while some have both. The USB port used by the bootloader is
connected to PB6 (D-) and PB7 (D+), while the regular port is
connected to PA11 (D-) and PA12 (D+). When only one port is present,
some soldering will be needed to access the port. When the 2 ports are
present, the bottom port is used by the bootloader and the top port is
the regular one, compatible with the STM32F103 chip.


Note on the firmware encryption
-------------------------------

A drawback of these devices is that the firmware has to be sent
encrypted to the device. The host creates the key, which is sent
garbled and hidden amongst garbage bytes to the bootloader. The
bootloader extracts the key from predetermined offsets in the packet
and will use it later to decrypt the data before flashing or
comparing. As it's a simple fixed size xor key, it's rather easy to
figure it out, but this is just plain stupid. Since the last byte of
the key is the sum of the first byte plus the chip type, it is not
possible to create a fake key full of zeroes.

There are plenty of other manufacturers who don't do that stupid shit.

For some reason, the data flash is different. Writing to it has to be
encrypted, but reading from it is in clear.


Caveats
-------

Setting options is not implemented.
The program will make no attempt to recover from an error and will
just exit.
