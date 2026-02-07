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
make vendor-download   # fetch the official Windows utility into downloads/
make vendor-run        # launch the vendor utility via Wine (auto-inits win32 prefix)
make run ARGS="--descriptor"   # override args when needed
```
```

The resulting binary will be `build/reed-r8080`.

## Using the vendor Windows software (for protocol tracing)

You may need the official R8080 Windows application to discover which HID
commands enable streaming. Convenience targets download and run it:

1. Install the prerequisites (Ubuntu/Debian example):

   ```bash
   sudo apt install wine unzip curl
   ```

   (If you plan to sniff USB traffic with Wireshark/tshark, also install
   `wireshark` and accept the prompt to allow non-root captures.)

2. Download and extract the vendor installer:

   ```bash
   make vendor-download
   ```

   This drops the vendor ZIP plus `R8080.exe` into `downloads/` (ignored by
   git).

3. Run the vendor program under Wine:

   ```bash
   make vendor-run
   ```

   The Makefile defaults to `WINEPREFIX=$HOME/.wine-r8080` and
   `WINEARCH=win32`, and it will automatically run `wineboot -i` the first time
   to initialize that 32-bit prefix. Override these environment variables if you
   already maintain your own Wine prefixes.

### Making the meter visible to the vendor app

Wine does not automatically expose Linux HID devices to Windows programs. If
the vendor UI launches but claims no meter is attached, use one of the
approaches below:

1. **Pass the USB device through to Wine (experimental):**
   * Install libusb for both architectures: `sudo apt install libusb-1.0-0 libusb-1.0-0:i386`.
   * Run Wine as root so it can detach the kernel driver and claim the device:
     ```bash
     sudo -E WINEPREFIX=$HOME/.wine-r8080 WINEARCH=win32 wine downloads/vendor/.../R8080.exe
     ```
     (Use `sudo -E` to preserve your prefix path. You may need to replug the
     meter afterwards to give it back to Linux.)
   * For troubleshooting, enable USB logging: `WINEDEBUG=+usb ...`.

2. **Use a Windows VM (recommended for USB capture):**
   * Launch a Windows virtual machine (VirtualBox/VMware/QEMU) and attach the
     Holtek device (vid:pid 04d9:e000) to the guest.
   * Install the vendor software inside the VM and verify it can talk to the
     meter.
   * Use USBPcap/Wireshark inside the guest to capture the HID traffic.

## Capturing USB traffic while the vendor app runs

To reverse engineer the initialization sequence, capture the HID traffic the
vendor software produces. One workflow is to use the kernel’s `usbmon`
interface together with Wireshark or tshark:

1. Enable usbmon:

   ```bash
   sudo modprobe usbmon
   ```

2. Identify the bus where the meter sits (look for `04d9:e000`):

   ```bash
   lsusb | grep 04d9:e000
   ```

   The bus number (first column) maps to the usbmon interface (`usbmon1,
   usbmon2, …`).

3. Start a capture (choose Wireshark or tshark):

   ```bash
   sudo wireshark &          # choose usbmonX as the interface
   # or
   sudo tshark -i usbmon1 -w r8080-vendor.pcapng
   ```

4. With the capture running, execute `make vendor-run`, perform the desired
   actions inside the vendor UI (start live view, etc.), then stop the capture.

5. Filter packets to the Holtek device (`usb.addr == 04d9:e000`) and note the
   HID output/feature reports being sent. Those byte sequences can be fed into
   `reed-r8080` via `--feature` (or baked into the tool) to reproduce the same
   behavior natively.

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
