#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <poll.h>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <vector>

#include <linux/hidraw.h>

namespace fs = std::filesystem;

namespace {

struct Options {
    bool listDevices = false;
    bool showDescriptor = false;
    bool dumpRaw = false;
    bool decodeAscii = true;
    bool decodeBcd = false;
    bool stream = true;
    std::string devicePath;
    std::optional<uint16_t> vendor = 0x04d9; // Holtek Semiconductor
    std::optional<uint16_t> product = 0xe000; // R8080 HID interface
    size_t reportSize = 64;
    size_t maxSamples = 0; // 0 = unlimited
    std::optional<std::chrono::seconds> duration = std::nullopt;
    int pollTimeoutMs = 1000;
    std::string csvPath;
    bool quiet = false;
    struct {
        bool enabled = false;
        size_t offset = 0;
        size_t nibbles = 8; // default 4 bytes
        size_t decimals = 1;
        bool isSigned = false;
    } bcd;
    std::vector<uint8_t> featureReport;
};

std::atomic<bool> g_shouldStop{false};

void handleSignal(int) {
    g_shouldStop = true;
}

[[noreturn]] void die(const std::string &msg) {
    throw std::runtime_error(msg);
}

uint16_t parseHex16(const std::string &value) {
    std::string v = value;
    if (v.rfind("0x", 0) == 0 || v.rfind("0X", 0) == 0) {
        v = v.substr(2);
    }
    uint16_t result = 0;
    std::stringstream ss;
    ss << std::hex << v;
    ss >> result;
    if (ss.fail()) {
        die("Unable to parse hex value: " + value);
    }
    return result;
}

std::vector<uint8_t> parseHexBytes(const std::string &spec) {
    std::vector<uint8_t> bytes;
    std::string token;
    std::stringstream ss(spec);
    while (ss >> token) {
        if (token.size() > 2 && (token.rfind("0x", 0) == 0 || token.rfind("0X", 0) == 0)) {
            token = token.substr(2);
        }
        if (token.size() == 1) {
            token = std::string("0") + token;
        }
        uint8_t value = 0;
        std::stringstream hex;
        hex << std::hex << token;
        hex >> value;
        if (hex.fail()) {
            die("Failed to parse hex byte in feature report: " + token);
        }
        bytes.push_back(value);
    }
    return bytes;
}

Options parseArgs(int argc, char **argv) {
    Options opts;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: reed-r8080 [options]\n"
                      << "  --list                     List available hidraw devices\n"
                      << "  --device <path>            Use explicit /dev/hidrawX path\n"
                      << "  --vendor <hex>            Match USB vendor id (default 0x04d9)\n"
                      << "  --product <hex>           Match USB product id (default 0xe000)\n"
                      << "  --descriptor              Print HID descriptor then exit\n"
                      << "  --report-size <bytes>     Expected report size (default 64)\n"
                      << "  --samples <n>             Stop after n samples\n"
                      << "  --seconds <n>             Stop after n seconds\n"
                      << "  --ascii / --no-ascii      Enable/disable ASCII decoder\n"
                      << "  --bcd                     Enable simple BCD decoder\n"
                      << "     --bcd-offset <n>       Byte offset for BCD data\n"
                      << "     --bcd-nibbles <n>      Number of BCD nibbles (default 8)\n"
                      << "     --bcd-decimals <n>     Decimal places for BCD (default 1)\n"
                      << "  --raw                     Print raw hex payload\n"
                      << "  --csv <file>              Append decoded values to CSV file\n"
                      << "  --feature <hex-bytes>     Send feature report before streaming\n"
                      << "  --quiet                   Suppress per-sample console output\n"
                      << "  --poll-timeout <ms>       Poll timeout (default 1000)\n"
                      << std::endl;
            std::exit(EXIT_SUCCESS);
        } else if (arg == "--list") {
            opts.listDevices = true;
            opts.stream = false;
        } else if (arg == "--device" && i + 1 < argc) {
            opts.devicePath = argv[++i];
        } else if (arg == "--vendor" && i + 1 < argc) {
            opts.vendor = parseHex16(argv[++i]);
        } else if (arg == "--product" && i + 1 < argc) {
            opts.product = parseHex16(argv[++i]);
        } else if (arg == "--descriptor") {
            opts.showDescriptor = true;
        } else if (arg == "--report-size" && i + 1 < argc) {
            opts.reportSize = static_cast<size_t>(std::stoul(argv[++i]));
        } else if (arg == "--samples" && i + 1 < argc) {
            opts.maxSamples = static_cast<size_t>(std::stoul(argv[++i]));
        } else if (arg == "--seconds" && i + 1 < argc) {
            opts.duration = std::chrono::seconds(std::stoul(argv[++i]));
        } else if (arg == "--ascii") {
            opts.decodeAscii = true;
        } else if (arg == "--no-ascii") {
            opts.decodeAscii = false;
        } else if (arg == "--bcd") {
            opts.decodeBcd = true;
            opts.bcd.enabled = true;
        } else if (arg == "--bcd-offset" && i + 1 < argc) {
            opts.bcd.offset = std::stoul(argv[++i]);
            opts.decodeBcd = true;
            opts.bcd.enabled = true;
        } else if (arg == "--bcd-nibbles" && i + 1 < argc) {
            opts.bcd.nibbles = std::stoul(argv[++i]);
            opts.decodeBcd = true;
            opts.bcd.enabled = true;
        } else if (arg == "--bcd-decimals" && i + 1 < argc) {
            opts.bcd.decimals = std::stoul(argv[++i]);
            opts.decodeBcd = true;
            opts.bcd.enabled = true;
        } else if (arg == "--bcd-signed") {
            opts.bcd.isSigned = true;
            opts.decodeBcd = true;
            opts.bcd.enabled = true;
        } else if (arg == "--raw") {
            opts.dumpRaw = true;
        } else if (arg == "--csv" && i + 1 < argc) {
            opts.csvPath = argv[++i];
        } else if (arg == "--feature" && i + 1 < argc) {
            opts.featureReport = parseHexBytes(argv[++i]);
        } else if (arg == "--quiet") {
            opts.quiet = true;
        } else if (arg == "--poll-timeout" && i + 1 < argc) {
            opts.pollTimeoutMs = std::stoi(argv[++i]);
        } else {
            die("Unknown or incomplete argument: " + arg);
        }
    }
    return opts;
}

std::string readSmallFile(const fs::path &file) {
    std::ifstream ifs(file);
    if (!ifs) {
        return {};
    }
    std::string content;
    std::getline(ifs, content);
    while (!content.empty() && (content.back() == '\n' || content.back() == '\r')) {
        content.pop_back();
    }
    return content;
}

struct HidInfo {
    std::string devNode;
    uint16_t vendor = 0;
    uint16_t product = 0;
    std::string manufacturer;
    std::string productString;
    std::string serial;
};

HidInfo inspectDevice(const fs::path &hidraw) {
    HidInfo info;
    info.devNode = hidraw.string();
    try {
        fs::path device = fs::canonical(hidraw / "device");
        info.vendor = static_cast<uint16_t>(std::stoul(readSmallFile(device / "idVendor"), nullptr, 16));
        info.product = static_cast<uint16_t>(std::stoul(readSmallFile(device / "idProduct"), nullptr, 16));
        info.manufacturer = readSmallFile(device / "manufacturer");
        info.productString = readSmallFile(device / "product");
        info.serial = readSmallFile(device / "serial" );
    } catch (const std::exception &) {
        // Ignore missing sysfs entries
    }
    return info;
}

std::vector<HidInfo> enumerateDevices() {
    std::vector<HidInfo> devices;
    for (const auto &entry : fs::directory_iterator("/sys/class/hidraw")) {
        if (!fs::is_directory(entry)) {
            continue;
        }
        auto node = fs::path("/dev") / entry.path().filename();
        devices.push_back(inspectDevice(node));
    }
    std::sort(devices.begin(), devices.end(), [](const HidInfo &a, const HidInfo &b) {
        return a.devNode < b.devNode;
    });
    return devices;
}

std::optional<HidInfo> autoSelectDevice(const Options &opts) {
    auto devices = enumerateDevices();
    for (const auto &dev : devices) {
        if (opts.vendor && dev.vendor != *opts.vendor) {
            continue;
        }
        if (opts.product && dev.product != *opts.product) {
            continue;
        }
        return dev;
    }
    return std::nullopt;
}

void printDeviceList(const std::vector<HidInfo> &devices) {
    if (devices.empty()) {
        std::cout << "No hidraw devices found.\n";
        return;
    }
    for (const auto &dev : devices) {
        std::cout << dev.devNode << "  vid:pid="
                  << std::hex << std::setfill('0') << std::setw(4) << dev.vendor
                  << ":" << std::setw(4) << dev.product << std::dec;
        if (!dev.productString.empty()) {
            std::cout << "  product='" << dev.productString << "'";
        }
        if (!dev.manufacturer.empty()) {
            std::cout << "  manufacturer='" << dev.manufacturer << "'";
        }
        if (!dev.serial.empty()) {
            std::cout << "  serial='" << dev.serial << "'";
        }
        std::cout << "\n";
    }
}

void dumpDescriptor(int fd) {
    hidraw_report_descriptor desc{};
    memset(&desc, 0, sizeof(desc));
    desc.size = sizeof(desc.value);

    if (ioctl(fd, HIDIOCGRDESC, &desc) == -1) {
        perror("HIDIOCGRDESC");
        return;
    }

    std::cout << "Report descriptor (" << desc.size << " bytes):\n";
    for (unsigned int i = 0; i < desc.size; ++i) {
        if (i % 16 == 0) {
            std::cout << std::setw(4) << std::setfill('0') << std::hex << i << ": ";
        }
        std::cout << std::setw(2) << static_cast<int>(desc.value[i]) << ' ';
        if (i % 16 == 15 || i + 1 == desc.size) {
            std::cout << '\n';
        }
    }
    std::cout << std::dec;
}

bool sendFeatureReport(int fd, const std::vector<uint8_t> &payload) {
    if (payload.empty()) {
        return true;
    }
    std::vector<uint8_t> buffer(payload);
    if (ioctl(fd, HIDIOCSFEATURE(buffer.size()), buffer.data()) == -1) {
        perror("HIDIOCSFEATURE");
        return false;
    }
    return true;
}

std::string timestampNow() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&t);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    auto remaining = now - std::chrono::system_clock::from_time_t(t);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count();
    oss << '.' << std::setw(3) << std::setfill('0') << ms;
    return oss.str();
}

struct DecodedSample {
    double value;
    std::string units = "dB";
    std::string extra;
};

std::optional<DecodedSample> decodeAscii(const uint8_t *data, size_t len) {
    std::string text;
    text.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        char c = static_cast<char>(data[i]);
        if (c == '\0') {
            break;
        }
        if (std::isprint(static_cast<unsigned char>(c))) {
            text.push_back(c);
        }
    }
    if (text.empty()) {
        return std::nullopt;
    }

    // Attempt to extract first number from the text
    std::string number;
    for (char c : text) {
        if ((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+') {
            number.push_back(c);
        } else if (!number.empty()) {
            break;
        }
    }
    if (number.empty()) {
        return std::nullopt;
    }
    try {
        double value = std::stod(number);
        DecodedSample sample{value, "dB", {}};
        if (text.find("dBA") != std::string::npos) {
            sample.units = "dBA";
        } else if (text.find("dBC") != std::string::npos) {
            sample.units = "dBC";
        }
        sample.extra = text;
        return sample;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<DecodedSample> decodeBcd(const Options &opts, const uint8_t *data, size_t len) {
    if (!opts.bcd.enabled) {
        return std::nullopt;
    }
    const size_t totalNibbles = opts.bcd.nibbles;
    const size_t startByte = opts.bcd.offset;
    const size_t bytesNeeded = (totalNibbles + 1) / 2;
    if (startByte + bytesNeeded > len) {
        return std::nullopt;
    }
    std::string digits;
    digits.reserve(totalNibbles);
    for (size_t n = 0; n < totalNibbles; ++n) {
        size_t byteIndex = startByte + n / 2;
        bool highNibble = (n % 2) == 0;
        uint8_t value = highNibble ? (data[byteIndex] >> 4) & 0x0F : data[byteIndex] & 0x0F;
        if (value > 9) {
            if (opts.bcd.isSigned && value == 0x0A && digits.empty()) {
                digits.push_back('-');
                continue;
            }
            return std::nullopt;
        }
        digits.push_back(static_cast<char>('0' + value));
    }
    if (digits.empty()) {
        return std::nullopt;
    }
    if (opts.bcd.decimals > 0 && digits.size() > opts.bcd.decimals) {
        digits.insert(digits.end() - opts.bcd.decimals, '.');
    }
    try {
        double value = std::stod(digits);
        return DecodedSample{value, "dB", "BCD"};
    } catch (...) {
        return std::nullopt;
    }
}

std::string bytesToHex(const uint8_t *data, size_t len) {
    std::ostringstream oss;
    for (size_t i = 0; i < len; ++i) {
        if (i) oss << ' ';
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(data[i]);
    }
    return oss.str();
}

void streamData(int fd, const Options &opts) {
    std::ofstream csv;
    if (!opts.csvPath.empty()) {
        csv.open(opts.csvPath, std::ios::app);
        if (!csv) {
            die("Unable to open CSV file: " + opts.csvPath);
        }
        csv << "timestamp,value,units,extra,raw\n";
    }

    std::vector<uint8_t> buffer(opts.reportSize);
    size_t samples = 0;
    auto start = std::chrono::steady_clock::now();
    double minValue = std::numeric_limits<double>::infinity();
    double maxValue = -std::numeric_limits<double>::infinity();
    double sumValue = 0.0;
    size_t decodedCount = 0;

    while (!g_shouldStop.load()) {
        if (opts.maxSamples && samples >= opts.maxSamples) {
            break;
        }
        if (opts.duration) {
            auto elapsed = std::chrono::steady_clock::now() - start;
            if (elapsed >= *opts.duration) {
                break;
            }
        }
        pollfd pfd{fd, POLLIN, 0};
        int ready = poll(&pfd, 1, opts.pollTimeoutMs);
        if (ready == 0) {
            continue; // timeout
        } else if (ready < 0) {
            if (errno == EINTR) continue;
            perror("poll");
            break;
        }
        ssize_t n = read(fd, buffer.data(), buffer.size());
        if (n < 0) {
            if (errno == EAGAIN)
                continue;
            perror("read");
            break;
        }
        if (n == 0) {
            continue;
        }
        ++samples;
        std::string ts = timestampNow();
        std::string rawHex = opts.dumpRaw ? bytesToHex(buffer.data(), static_cast<size_t>(n)) : "";

        std::optional<DecodedSample> decoded;
        if (opts.decodeAscii) {
            decoded = decodeAscii(buffer.data(), static_cast<size_t>(n));
        }
        if (!decoded && opts.decodeBcd) {
            decoded = decodeBcd(opts, buffer.data(), static_cast<size_t>(n));
        }

        if (decoded) {
            minValue = std::min(minValue, decoded->value);
            maxValue = std::max(maxValue, decoded->value);
            sumValue += decoded->value;
            ++decodedCount;
        }

        if (!opts.quiet) {
            std::ostringstream line;
            line << ts << "  ";
            if (decoded) {
                line << std::fixed << std::setprecision(2) << decoded->value << ' ' << decoded->units;
                if (!decoded->extra.empty()) {
                    line << "  [" << decoded->extra << "]";
                }
            } else {
                line << "(no decode)";
            }
            if (!rawHex.empty()) {
                line << "  raw=" << rawHex;
            }
            std::cout << line.str() << '\n';
        }

        if (csv && decoded) {
            csv << ts << ',' << decoded->value << ',' << decoded->units << ",\""
                << decoded->extra << "\",\""
                << rawHex << "\"\n";
        }
    }

    if (decodedCount > 0) {
        double avg = sumValue / static_cast<double>(decodedCount);
        std::cout << "\nDecoded " << decodedCount << " samples."
                  << "  min=" << std::fixed << std::setprecision(2) << minValue
                  << "  max=" << maxValue
                  << "  avg=" << avg << '\n';
    } else {
        std::cout << "\nNo samples decoded.\n";
    }
}

} // namespace

int main(int argc, char **argv) {
    try {
        Options opts = parseArgs(argc, argv);
        std::signal(SIGINT, handleSignal);

        if (opts.listDevices) {
            printDeviceList(enumerateDevices());
            return 0;
        }

        HidInfo selected;
        if (!opts.devicePath.empty()) {
            selected = inspectDevice(opts.devicePath);
        } else {
            auto match = autoSelectDevice(opts);
            if (!match) {
                die("Unable to locate matching hidraw device. Use --list or --device.");
            }
            selected = *match;
        }

        std::cout << "Using device " << selected.devNode;
        std::cout << " (vid:pid=" << std::hex << std::setw(4) << std::setfill('0') << selected.vendor
                  << ':' << std::setw(4) << selected.product << std::dec << ")\n";

        int fd = open(selected.devNode.c_str(), O_RDWR | O_NONBLOCK);
        if (fd < 0) {
            perror("open hidraw");
            return 1;
        }

        if (opts.showDescriptor) {
            dumpDescriptor(fd);
            close(fd);
            return 0;
        }

        if (!opts.featureReport.empty()) {
            if (!sendFeatureReport(fd, opts.featureReport)) {
                std::cerr << "Failed to send feature report; continuing without it.\n";
            }
        }

        if (opts.stream) {
            streamData(fd, opts);
        }

        close(fd);
    } catch (const std::exception &ex) {
        std::cerr << "Error: " << ex.what() << '\n';
        return 1;
    }
    return 0;
}
