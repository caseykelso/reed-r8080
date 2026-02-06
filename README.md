# Reed R8080 Linux Interface

This repository contains a small C++ utility that talks to the REED R8080
sound level meter through its USB HID (Holtek Workshop Sample) interface.
The goal is to provide a convenient way to:

* list the hidraw devices that are currently attached (`--list`)
* inspect the R8080 HID report descriptor (`--descriptor`)
* stream live reports, show the raw payloads, and decode the dB readings
  into CSV-friendly time series (`--raw`, `--ascii`, `--bcd`, `--csv`)

Because the official Windows application communicates over hidraw the tool
focuses on being flexible: it can log undecoded payloads while you reverse
engineer the format, or it can perform simple ASCII / BCD decoding to print
approximate sound level readings in real time.

## Prerequisites

* Linux with `/dev/hidraw` support (tested on Ubuntu 22.04 / kernel 6.5).
* CMake ≥ 3.16 and a C++17 capable compiler (g++ 10+, clang 12+, …).
* Access to the hidraw node: either run the tool with `sudo` or install a
  udev rule (see the next section).

### Allow non-root access (udev rule)

1. Decide which group should own the device (e.g. `plugdev`). Make sure your
   user belongs to it (`groups | grep plugdev`). If not: `sudo usermod -aG plugdev $USER`
   and log out/in.
2. Create the rule:

   ```bash
   sudo tee /etc/udev/rules.d/99-r8080.rules <<'EOF'
   SUBSYSTEM=="hidraw", ATTRS{idVendor}=="04d9", ATTRS{idProduct}=="e000", MODE="0664", GROUP="plugdev"
   EOF
   ```

3. Reload udev and re-trigger the rule (or unplug/replug the meter):

   ```bash
   sudo udevadm control --reload
   sudo udevadm trigger --attr-match=idVendor=04d9 --attr-match=idProduct=e000
   ```

4. If the permissions still show `root root`, unplug and replug the meter (or
   reboot) so the new rule takes effect. You should then see something like:

   ```
   crw-rw-r-- 1 root plugdev 511, 1 ... /dev/hidraw1
   ```

After these steps `make list` / `make run` should work without `sudo`, and the
device will report the proper vid/pid instead of `0000:0000`.

## Building

```bash
cmake -S . -B build
cmake --build build

You can also use the provided `Makefile` wrapper:

```bash
make build             # configure + build via CMake
make run               # auto-detect 04d9:e000, run `--raw --csv samples.csv`
make list              # shortcut for reed-r8080 --list
make run ARGS="--descriptor"   # override args when needed
```
```

The resulting binary will be `build/reed-r8080`.

## Usage

List the available hidraw devices and confirm that the R8080 is visible:

```bash
sudo ./build/reed-r8080 --list
```

Inspect the report descriptor of the first matching `04d9:e000` device:

```bash
sudo ./build/reed-r8080 --descriptor
```

Stream live reports, print them in a human readable form and append all
decoded data to a CSV file:

```bash
sudo ./build/reed-r8080 --raw --csv r8080.csv
```

The tool tries two decoders:

1. **ASCII decoder (default):** useful when the payload already contains
   printable text such as ` 035.2 dBA`. The first number it finds will be
   printed and logged.
2. **BCD decoder:** when the device emits packed BCD digits. Enable via
   `--bcd` and tweak the layout:

   ```bash
   sudo ./build/reed-r8080 --bcd --bcd-offset 1 --bcd-nibbles 6 --bcd-decimals 1
   ```

   The example above decodes 3 bytes starting at offset 1 as `XX.X` dB.

### Feature reports / commands

Some meters only start streaming after receiving a command. You can send an
arbitrary HID feature report before streaming begins by supplying raw hex
bytes:

```bash
sudo ./build/reed-r8080 --feature "00 01 02 03"
```

The first byte should be the report ID (usually `00`). If you are unsure of
the protocol keep `--raw` enabled, capture the payloads, and compare them
with traces taken while the official Windows application runs inside a VM.

### Logging undecoded data

When reverse engineering the protocol, use the tool purely as a logger:

```bash
sudo ./build/reed-r8080 --raw --no-ascii --samples 200 > raw.log
```

The resulting log can be fed into Python, Wireshark, or a spreadsheet to
discover patterns between button presses and byte sequences.

## Limitations / next steps

* The program does not attempt to request historical logs from the meter.
* The default ASCII decoder only extracts the first numeric token; feel free
  to extend it once the exact byte layout is known.
* If decoding fails you still get a CSV with timestamps and raw payloads so
  you can iterate without plugging the meter into a Windows PC.

Pull requests with better knowledge of the R8080 protocol are very welcome!
