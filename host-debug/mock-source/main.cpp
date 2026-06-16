#include "arps/protocol.h"

#include "socket_compat.h"

#include <lz4.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace sockets = arps::socket_compat;

struct Args {
    std::string host = "127.0.0.1";
    std::uint16_t port = 27183;
    std::uint32_t width = 640;
    std::uint32_t height = 360;
    std::uint32_t fps = 30;
    std::uint32_t frames = 0;
};

std::uint32_t U32Size(std::size_t size) {
    if (size > std::numeric_limits<std::uint32_t>::max()) {
        std::cerr << "size exceeds uint32_t: " << size << "\n";
        std::exit(1);
    }
    return static_cast<std::uint32_t>(size);
}

int IntSize(std::size_t size) {
    if (size > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        std::cerr << "size exceeds int: " << size << "\n";
        std::exit(1);
    }
    return static_cast<int>(size);
}

struct Packet {
    std::uint16_t type = 0;
    std::string ext;
};

void WriteBe16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
    out.push_back(static_cast<std::uint8_t>(value & 0xff));
}

void WriteBe32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xff));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xff));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
    out.push_back(static_cast<std::uint8_t>(value & 0xff));
}

void WriteBe64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    WriteBe32(out, static_cast<std::uint32_t>(value >> 32));
    WriteBe32(out, static_cast<std::uint32_t>(value & 0xffffffffu));
}

std::uint16_t ReadBe16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>((p[0] << 8) | p[1]);
}

std::uint32_t ReadBe32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24)
            | (static_cast<std::uint32_t>(p[1]) << 16)
            | (static_cast<std::uint32_t>(p[2]) << 8)
            | static_cast<std::uint32_t>(p[3]);
}

std::uint64_t SteadyNanos() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

double MsSince(std::chrono::steady_clock::time_point start) {
    using Duration = std::chrono::duration<double, std::milli>;
    return Duration(std::chrono::steady_clock::now() - start).count();
}

bool ParseU16(const std::string& value, std::uint16_t* out) {
    char* end = nullptr;
    long parsed = strtol(value.c_str(), &end, 10);
    if (*end != '\0' || parsed <= 0 || parsed > 65535) {
        return false;
    }
    *out = static_cast<std::uint16_t>(parsed);
    return true;
}

bool ParseU32(const std::string& value, std::uint32_t* out) {
    char* end = nullptr;
    unsigned long parsed = strtoul(value.c_str(), &end, 10);
    if (*end != '\0' || parsed > 0xfffffffful) {
        return false;
    }
    *out = static_cast<std::uint32_t>(parsed);
    return true;
}

void PrintUsage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " [--host=127.0.0.1] [--port=27183]\n"
              << "       [--width=640] [--height=360] [--fps=30] [--frames=0]\n";
}

bool ParseArgs(int argc, char** argv, Args* args) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            return false;
        }
        if (arg.rfind("--", 0) != 0 || arg.find('=') == std::string::npos) {
            std::cerr << "Invalid argument: " << arg << "\n";
            PrintUsage(argv[0]);
            return false;
        }
        std::size_t split = arg.find('=');
        std::string key = arg.substr(2, split - 2);
        std::string value = arg.substr(split + 1);
        if (key == "host") {
            args->host = value;
        } else if (key == "port") {
            if (!ParseU16(value, &args->port)) {
                std::cerr << "Invalid --port\n";
                return false;
            }
        } else if (key == "width") {
            if (!ParseU32(value, &args->width) || args->width == 0 || args->width > 4096) {
                std::cerr << "Invalid --width\n";
                return false;
            }
        } else if (key == "height") {
            if (!ParseU32(value, &args->height) || args->height == 0 || args->height > 4096) {
                std::cerr << "Invalid --height\n";
                return false;
            }
        } else if (key == "fps") {
            if (!ParseU32(value, &args->fps) || args->fps > 240) {
                std::cerr << "Invalid --fps\n";
                return false;
            }
        } else if (key == "frames") {
            if (!ParseU32(value, &args->frames)) {
                std::cerr << "Invalid --frames\n";
                return false;
            }
        } else {
            std::cerr << "Unknown argument: --" << key << "\n";
            return false;
        }
    }
    return true;
}

bool SendAll(arps::ArpsSocket socket, const std::vector<std::uint8_t>& bytes,
        std::string* error) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        int wrote = sockets::Send(socket, bytes.data() + offset, bytes.size() - offset,
                error);
        if (wrote < 0) {
            return false;
        }
        if (wrote == 0) {
            if (error) {
                *error = "send returned 0";
            }
            return false;
        }
        offset += static_cast<std::size_t>(wrote);
    }
    return true;
}

bool ReadExact(arps::ArpsSocket socket, std::uint8_t* out, std::size_t len,
        std::string* error) {
    std::size_t offset = 0;
    while (offset < len) {
        int got = sockets::Recv(socket, out + offset, len - offset, error);
        if (got < 0) {
            return false;
        }
        if (got == 0) {
            if (error) {
                *error = "socket closed";
            }
            return false;
        }
        offset += static_cast<std::size_t>(got);
    }
    return true;
}

std::vector<std::uint8_t> BuildPacket(std::uint16_t type,
        const std::vector<std::uint8_t>& base, const std::vector<std::uint8_t>& bitmap,
        const std::string& ext, std::uint32_t sequence) {
    std::vector<std::uint8_t> out;
    std::uint32_t packet_len = 4u + U32Size(base.size()) + 4u + U32Size(bitmap.size())
            + 4u + U32Size(ext.size());
    out.insert(out.end(), arps::kMagic, arps::kMagic + arps::kMagicSize);
    WriteBe16(out, arps::kProtocolMajor);
    WriteBe16(out, arps::kProtocolMinor);
    WriteBe16(out, type);
    WriteBe16(out, arps::kHeaderLen);
    WriteBe32(out, 0);
    WriteBe32(out, sequence);
    WriteBe32(out, packet_len);
    WriteBe32(out, U32Size(base.size()));
    out.insert(out.end(), base.begin(), base.end());
    WriteBe32(out, U32Size(bitmap.size()));
    out.insert(out.end(), bitmap.begin(), bitmap.end());
    WriteBe32(out, U32Size(ext.size()));
    out.insert(out.end(), ext.begin(), ext.end());
    return out;
}

bool ReadPacket(arps::ArpsSocket socket, Packet* packet, std::string* error) {
    std::uint8_t header[arps::kHeaderLen];
    if (!ReadExact(socket, header, sizeof(header), error)) {
        return false;
    }
    if (std::memcmp(header, arps::kMagic, arps::kMagicSize) != 0) {
        if (error) {
            *error = "bad ARPS magic";
        }
        return false;
    }
    packet->type = ReadBe16(header + 16);
    std::uint32_t packet_len = ReadBe32(header + 28);
    std::uint32_t consumed = 0;
    auto read_u32 = [&]() -> std::uint32_t {
        std::uint8_t buf[4];
        if (!ReadExact(socket, buf, sizeof(buf), error)) {
            return 0;
        }
        consumed += 4;
        return ReadBe32(buf);
    };
    auto skip = [&](std::uint32_t len) -> bool {
        std::vector<std::uint8_t> tmp(len);
        if (len == 0) {
            return true;
        }
        if (!ReadExact(socket, tmp.data(), tmp.size(), error)) {
            return false;
        }
        consumed += len;
        return true;
    };

    std::uint32_t base_len = read_u32();
    if (!skip(base_len)) {
        return false;
    }
    std::uint32_t bitmap_len = read_u32();
    if (!skip(bitmap_len)) {
        return false;
    }
    std::uint32_t ext_len = read_u32();
    std::vector<std::uint8_t> ext(ext_len);
    if (ext_len > 0 && !ReadExact(socket, ext.data(), ext.size(), error)) {
        return false;
    }
    consumed += ext_len;
    packet->ext.assign(reinterpret_cast<const char*>(ext.data()), ext.size());
    if (packet_len > consumed) {
        return skip(packet_len - consumed);
    }
    return true;
}

std::string JsonString(const std::string& json, const char* key, const std::string& fallback) {
    std::string quoted = std::string("\"") + key + "\"";
    std::size_t pos = json.find(quoted);
    if (pos == std::string::npos) {
        return fallback;
    }
    pos = json.find(':', pos + quoted.size());
    if (pos == std::string::npos) {
        return fallback;
    }
    pos = json.find('"', pos);
    if (pos == std::string::npos) {
        return fallback;
    }
    std::size_t end = json.find('"', pos + 1);
    if (end == std::string::npos) {
        return fallback;
    }
    return json.substr(pos + 1, end - pos - 1);
}

std::uint32_t JsonU32(const std::string& json, const char* key, std::uint32_t fallback) {
    std::string quoted = std::string("\"") + key + "\"";
    std::size_t pos = json.find(quoted);
    if (pos == std::string::npos) {
        return fallback;
    }
    pos = json.find(':', pos + quoted.size());
    if (pos == std::string::npos) {
        return fallback;
    }
    char* end = nullptr;
    unsigned long parsed = strtoul(json.c_str() + pos + 1, &end, 10);
    if (end == json.c_str() + pos + 1 || parsed > 0xfffffffful) {
        return fallback;
    }
    return static_cast<std::uint32_t>(parsed);
}

arps::ArpsSocket Connect(const Args& args, std::string* error) {
    return sockets::ConnectIpv4(args.host, args.port, error);
}

std::vector<std::uint8_t> BuildHello(std::uint32_t sequence) {
    std::string ext =
            "{\"device\":{\"manufacturer\":\"mock\",\"brand\":\"host\","
            "\"model\":\"synthetic\",\"android_sdk\":35,\"android_release\":\"mock\"},"
            "\"capabilities\":{\"pixel_formats\":[\"argb8888\"],"
            "\"compressions\":[\"raw\",\"lz4_block\"],\"screen_power\":false,"
            "\"max_packet_len\":67108864,\"native_lz4\":true}}";
    return BuildPacket(arps::kPacketHello, {}, {}, ext, sequence);
}

void GenerateFrame(std::uint32_t width, std::uint32_t height, std::uint64_t frame_no,
        std::vector<std::uint8_t>* raw) {
    raw->assign(static_cast<std::size_t>(width) * height * 4, 0);
    std::uint32_t box_size = std::max<std::uint32_t>(24, std::min(width, height) / 5);
    std::uint32_t box_x = static_cast<std::uint32_t>((frame_no * 7) % (width + box_size))
            - std::min<std::uint32_t>(box_size, width);
    std::uint32_t box_y = static_cast<std::uint32_t>((frame_no * 5) % (height + box_size))
            - std::min<std::uint32_t>(box_size, height);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            std::size_t offset = (static_cast<std::size_t>(y) * width + x) * 4;
            bool in_box = x >= box_x && x < box_x + box_size && y >= box_y
                    && y < box_y + box_size;
            (*raw)[offset + 0] = in_box ? 255 : static_cast<std::uint8_t>((x + frame_no) & 0xff);
            (*raw)[offset + 1] = in_box ? 220 : static_cast<std::uint8_t>((y * 2) & 0xff);
            (*raw)[offset + 2] = in_box ? 40
                                        : static_cast<std::uint8_t>((x + y + frame_no * 3) & 0xff);
            (*raw)[offset + 3] = 255;
        }
    }
}

std::vector<std::uint8_t> BuildFrameBase(std::uint64_t frame_no, std::uint32_t width,
        std::uint32_t height, std::uint32_t compression_type, std::uint32_t uncompressed_len,
        std::uint32_t compressed_len) {
    std::vector<std::uint8_t> base;
    WriteBe64(base, frame_no);
    WriteBe64(base, SteadyNanos());
    WriteBe32(base, width);
    WriteBe32(base, height);
    WriteBe32(base, width * 4);
    WriteBe32(base, 0);
    WriteBe32(base, arps::kPixelFormatAndroidArgb8888Raw);
    WriteBe32(base, compression_type);
    WriteBe32(base, uncompressed_len);
    WriteBe32(base, compressed_len);
    WriteBe32(base, 0);
    WriteBe32(base, 1);
    WriteBe32(base, 0);
    WriteBe32(base, 0);
    return base;
}

std::string FrameExt(double capture_ms, double compress_ms, double previous_write_ms) {
    std::ostringstream out;
    out << "{\"capture_api\":\"host-debug.mock-source\","
        << "\"capture_ms\":" << capture_ms << ","
        << "\"copy_ms\":0,"
        << "\"compress_ms\":" << compress_ms << ","
        << "\"previous_write_ms\":" << previous_write_ms << "}";
    return out.str();
}

}  // namespace

int main(int argc, char** argv) {
    Args args;
    if (!ParseArgs(argc, argv, &args)) {
        return 2;
    }

    std::string error;
    arps::ArpsSocket socket = Connect(args, &error);
    if (sockets::IsInvalid(socket)) {
        std::cerr << "Connect failed: " << error << "\n";
        return 1;
    }
    std::cout << "Connected to " << args.host << ":" << args.port << "\n";

    std::uint32_t sequence = 1;
    if (!SendAll(socket, BuildHello(sequence++), &error)) {
        std::cerr << "Send HELLO failed: " << error << "\n";
        sockets::Close(socket);
        return 1;
    }

    Packet start;
    if (!ReadPacket(socket, &start, &error) || start.type != arps::kPacketStart) {
        std::cerr << "Read START failed: " << error << "\n";
        sockets::Close(socket);
        return 1;
    }
    std::string compression = JsonString(start.ext, "compression", "lz4_block");
    std::uint32_t start_fps = JsonU32(start.ext, "max_fps", args.fps);
    std::uint32_t fps = args.fps;
    if (start_fps > 0) {
        fps = std::min(fps, start_fps);
    }
    if (compression != "raw" && compression != "lz4_block") {
        std::cerr << "Unsupported START compression: " << compression << "\n";
        sockets::Close(socket);
        return 1;
    }
    std::cout << "START " << start.ext << "\n";
    std::cout << "Streaming " << args.width << "x" << args.height << " "
              << compression << " at " << fps << " fps\n";

    std::vector<std::uint8_t> raw;
    std::vector<std::uint8_t> payload;
    double previous_write_ms = -1.0;
    auto frame_interval = fps > 0 ? std::chrono::nanoseconds(1000000000ull / fps)
                                  : std::chrono::nanoseconds(0);
    std::uint64_t frame_no = 0;
    while (args.frames == 0 || frame_no < args.frames) {
        auto frame_start = std::chrono::steady_clock::now();
        frame_no++;
        GenerateFrame(args.width, args.height, frame_no, &raw);
        double capture_ms = MsSince(frame_start);

        auto compress_start = std::chrono::steady_clock::now();
        std::uint32_t compression_type = arps::kCompressionRaw;
        if (compression == "lz4_block") {
            payload.assign(static_cast<std::size_t>(LZ4_compressBound(IntSize(raw.size()))),
                    0);
            int written = LZ4_compress_default(reinterpret_cast<const char*>(raw.data()),
                    reinterpret_cast<char*>(payload.data()), IntSize(raw.size()),
                    IntSize(payload.size()));
            if (written <= 0) {
                std::cerr << "LZ4 compression failed\n";
                sockets::Close(socket);
                return 1;
            }
            payload.resize(static_cast<std::size_t>(written));
            compression_type = arps::kCompressionLz4Block;
        } else {
            payload = raw;
        }
        double compress_ms = MsSince(compress_start);

        std::vector<std::uint8_t> base = BuildFrameBase(frame_no, args.width, args.height,
                compression_type, U32Size(raw.size()), U32Size(payload.size()));
        std::string ext = FrameExt(capture_ms, compress_ms, previous_write_ms);
        std::vector<std::uint8_t> packet = BuildPacket(arps::kPacketFrame, base, payload, ext,
                sequence++);

        auto write_start = std::chrono::steady_clock::now();
        if (!SendAll(socket, packet, &error)) {
            std::cerr << "Send FRAME failed: " << error << "\n";
            sockets::Close(socket);
            return 1;
        }
        previous_write_ms = MsSince(write_start);

        if (frame_no % 60 == 0) {
            std::cout << "sent frame " << frame_no << " payload=" << payload.size() << "\n";
        }

        if (frame_interval.count() > 0) {
            auto elapsed = std::chrono::steady_clock::now() - frame_start;
            if (elapsed < frame_interval) {
                std::this_thread::sleep_for(frame_interval - elapsed);
            }
        }
    }

    std::string stop_ext = "{\"reason\":\"mock_finished\"}";
    SendAll(socket, BuildPacket(arps::kPacketStop, {}, {}, stop_ext, sequence++), &error);
    sockets::Close(socket);
    return 0;
}
