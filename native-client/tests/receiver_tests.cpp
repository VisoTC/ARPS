#include "arps/receiver.h"

#include "../src/socket_compat.h"

#include <lz4.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

namespace compat = arps::socket_compat;

[[noreturn]] void CheckFailed(const char* condition, const char* file, int line) {
    std::cerr << "CHECK failed: " << condition << " at " << file << ":" << line << "\n";
    std::abort();
}

#define CHECK(condition) \
    ((condition) ? static_cast<void>(0) : CheckFailed(#condition, __FILE__, __LINE__))

std::uint32_t U32Size(std::size_t size) {
    CHECK(size <= std::numeric_limits<std::uint32_t>::max());
    return static_cast<std::uint32_t>(size);
}

int IntSize(std::size_t size) {
    CHECK(size <= static_cast<std::size_t>(std::numeric_limits<int>::max()));
    return static_cast<int>(size);
}

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

void SendAll(arps::ArpsSocket socket, const std::vector<std::uint8_t>& bytes) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        std::string error;
        int wrote = compat::Send(socket, bytes.data() + offset, bytes.size() - offset,
                &error);
        CHECK(wrote > 0);
        offset += static_cast<std::size_t>(wrote);
    }
}

void RecvAll(arps::ArpsSocket socket, std::uint8_t* data, std::size_t len) {
    std::size_t offset = 0;
    while (offset < len) {
        std::string error;
        int got = compat::Recv(socket, data + offset, len - offset, &error);
        CHECK(got > 0);
        offset += static_cast<std::size_t>(got);
    }
}

std::uint32_t ReadBe32(const std::uint8_t* data) {
    return (static_cast<std::uint32_t>(data[0]) << 24)
            | (static_cast<std::uint32_t>(data[1]) << 16)
            | (static_cast<std::uint32_t>(data[2]) << 8)
            | static_cast<std::uint32_t>(data[3]);
}

std::vector<std::uint8_t> FrameBase(std::uint32_t width, std::uint32_t height,
        std::uint32_t row_bytes, std::uint32_t compression_type,
        std::uint32_t uncompressed_len, std::uint32_t compressed_len) {
    std::vector<std::uint8_t> base;
    WriteBe64(base, 7);
    WriteBe64(base, 123456);
    WriteBe32(base, width);
    WriteBe32(base, height);
    WriteBe32(base, row_bytes);
    WriteBe32(base, 0);
    WriteBe32(base, arps::kPixelFormatAndroidArgb8888Raw);
    WriteBe32(base, compression_type);
    WriteBe32(base, uncompressed_len);
    WriteBe32(base, compressed_len);
    WriteBe32(base, 0);
    WriteBe32(base, 1);
    WriteBe32(base, 0);
    WriteBe32(base, 0);
    CHECK(base.size() == arps::kFrameBaseLenV1);
    return base;
}

std::vector<std::uint8_t> Packet(std::uint16_t type, const std::vector<std::uint8_t>& base,
        const std::vector<std::uint8_t>& bitmap, const std::string& ext,
        const std::vector<std::uint8_t>& tail = {}) {
    std::vector<std::uint8_t> out;
    out.insert(out.end(), arps::kMagic, arps::kMagic + arps::kMagicSize);
    WriteBe16(out, arps::kProtocolMajor);
    WriteBe16(out, arps::kProtocolMinor);
    WriteBe16(out, type);
    WriteBe16(out, arps::kHeaderLen);
    WriteBe32(out, 0);
    WriteBe32(out, 1);
    std::uint32_t packet_len = 4u + U32Size(base.size()) + 4u + U32Size(bitmap.size())
            + 4u + U32Size(ext.size()) + U32Size(tail.size());
    WriteBe32(out, packet_len);
    WriteBe32(out, U32Size(base.size()));
    out.insert(out.end(), base.begin(), base.end());
    WriteBe32(out, U32Size(bitmap.size()));
    out.insert(out.end(), bitmap.begin(), bitmap.end());
    WriteBe32(out, U32Size(ext.size()));
    out.insert(out.end(), ext.begin(), ext.end());
    out.insert(out.end(), tail.begin(), tail.end());
    return out;
}

std::vector<std::uint8_t> PacketWithBadMagic() {
    std::vector<std::uint8_t> bytes = Packet(arps::kPacketHello, {}, {}, "{}");
    bytes[0] = 'X';
    return bytes;
}

std::vector<std::uint8_t> PacketWithLargeLen() {
    std::vector<std::uint8_t> out;
    out.insert(out.end(), arps::kMagic, arps::kMagic + arps::kMagicSize);
    WriteBe16(out, arps::kProtocolMajor);
    WriteBe16(out, arps::kProtocolMinor);
    WriteBe16(out, arps::kPacketHello);
    WriteBe16(out, arps::kHeaderLen);
    WriteBe32(out, 0);
    WriteBe32(out, 1);
    WriteBe32(out, arps::kDefaultMaxPacketLen + 1);
    return out;
}

template <typename Fn>
void WithReadFromBytes(const std::vector<std::uint8_t>& bytes, Fn fn) {
    arps::ArpsSocket sockets[2];
    std::string error;
    CHECK(compat::CreateConnectedSocketPair(sockets, &error));
    SendAll(sockets[1], bytes);
    compat::Close(sockets[1]);
    arps::ArpsReceiver receiver;
    CHECK(receiver.AdoptConnectedSocket(sockets[0], &error));
    arps::ArpsReadResult result = receiver.ReadNext(1000);
    fn(result);
}

void TestHello() {
    WithReadFromBytes(Packet(arps::kPacketHello, {}, {}, "{\"ok\":true}"),
            [](const arps::ArpsReadResult& result) {
                CHECK(result.status == arps::ArpsReadStatus::Hello);
                CHECK(result.json == "{\"ok\":true}");
            });
}

void TestReady() {
    WithReadFromBytes(Packet(arps::kPacketReady, {}, {}, "{\"ready\":true}"),
            [](const arps::ArpsReadResult& result) {
                CHECK(result.status == arps::ArpsReadStatus::Ready);
                CHECK(result.json == "{\"ready\":true}");
            });
}

void TestControlStatuses() {
    WithReadFromBytes(Packet(arps::kPacketError, {}, {}, "{\"message\":\"boom\"}"),
            [](const arps::ArpsReadResult& result) {
                CHECK(result.status == arps::ArpsReadStatus::Error);
                CHECK(result.json == "{\"message\":\"boom\"}");
            });
    WithReadFromBytes(Packet(arps::kPacketStop, {}, {}, "{\"reason\":\"done\"}"),
            [](const arps::ArpsReadResult& result) {
                CHECK(result.status == arps::ArpsReadStatus::Stop);
                CHECK(result.json == "{\"reason\":\"done\"}");
            });
}

void TestRawFrame() {
    std::vector<std::uint8_t> raw(4 * 3 * 4);
    for (std::size_t i = 0; i < raw.size(); ++i) {
        raw[i] = static_cast<std::uint8_t>(i);
    }
    auto base = FrameBase(4, 3, 16, arps::kCompressionRaw, U32Size(raw.size()),
            U32Size(raw.size()));
    WithReadFromBytes(Packet(arps::kPacketFrame, base, raw,
            "{\"capture_ms\":1.5,\"copy_ms\":0.5}", {1, 2, 3}),
            [&raw](const arps::ArpsReadResult& result) {
                CHECK(result.status == arps::ArpsReadStatus::Frame);
                CHECK(result.frame.meta.frame_no == 7);
                CHECK(result.frame.meta.width == 4);
                CHECK(result.frame.meta.height == 3);
                CHECK(result.frame.argb8888_len == raw.size());
                CHECK(std::memcmp(result.frame.argb8888, raw.data(), raw.size()) == 0);
                CHECK(result.frame.device_timings.capture_ms == 1.5);
            });
}

void TestLz4Frame() {
    std::vector<std::uint8_t> raw(8 * 4 * 4);
    for (std::size_t i = 0; i < raw.size(); ++i) {
        raw[i] = static_cast<std::uint8_t>((i * 17) & 0xff);
    }
    std::vector<std::uint8_t> compressed(
            static_cast<std::size_t>(LZ4_compressBound(IntSize(raw.size()))));
    int written = LZ4_compress_default(reinterpret_cast<const char*>(raw.data()),
            reinterpret_cast<char*>(compressed.data()), IntSize(raw.size()),
            IntSize(compressed.size()));
    CHECK(written > 0);
    compressed.resize(static_cast<std::size_t>(written));
    auto base = FrameBase(8, 4, 32, arps::kCompressionLz4Block, U32Size(raw.size()),
            U32Size(compressed.size()));
    WithReadFromBytes(Packet(arps::kPacketFrame, base, compressed,
            "{\"compress_ms\":2.25}"), [&raw, &compressed](
            const arps::ArpsReadResult& result) {
                CHECK(result.status == arps::ArpsReadStatus::Frame);
                CHECK(result.frame.argb8888_len == raw.size());
                CHECK(std::memcmp(result.frame.argb8888, raw.data(), raw.size()) == 0);
                CHECK(result.frame.bitmap_payload_len == compressed.size());
                CHECK(result.frame.device_timings.compress_ms == 2.25);
            });
}

void TestProtocolErrors() {
    WithReadFromBytes(PacketWithBadMagic(), [](const arps::ArpsReadResult& result) {
        CHECK(result.status == arps::ArpsReadStatus::ProtocolError);
    });
    WithReadFromBytes(PacketWithLargeLen(), [](const arps::ArpsReadResult& result) {
        CHECK(result.status == arps::ArpsReadStatus::ProtocolError);
    });

    std::vector<std::uint8_t> raw(16, static_cast<std::uint8_t>(0xaa));
    auto short_base = FrameBase(2, 2, 8, arps::kCompressionRaw, U32Size(raw.size()),
            U32Size(raw.size()));
    short_base.resize(12);
    WithReadFromBytes(Packet(arps::kPacketFrame, short_base, raw, ""),
            [](const arps::ArpsReadResult& result) {
                CHECK(result.status == arps::ArpsReadStatus::ProtocolError);
            });

    auto mismatch_base = FrameBase(2, 2, 8, arps::kCompressionRaw, U32Size(raw.size()),
            U32Size(raw.size() + 1));
    WithReadFromBytes(Packet(arps::kPacketFrame, mismatch_base, raw, ""),
            [](const arps::ArpsReadResult& result) {
                CHECK(result.status == arps::ArpsReadStatus::ProtocolError);
            });

    auto narrow_stride_base = FrameBase(1000, 2, 8, arps::kCompressionRaw, U32Size(raw.size()),
            U32Size(raw.size()));
    WithReadFromBytes(Packet(arps::kPacketFrame, narrow_stride_base, raw, ""),
            [](const arps::ArpsReadResult& result) {
                CHECK(result.status == arps::ArpsReadStatus::ProtocolError);
            });

    std::uint32_t huge_height = arps::kDefaultMaxPacketLen / 4u + 1u;
    std::uint32_t huge_uncompressed_len = huge_height * 4u;
    std::vector<std::uint8_t> tiny_lz4 = {0};
    auto huge_lz4_base = FrameBase(1, huge_height, 4, arps::kCompressionLz4Block,
            huge_uncompressed_len, U32Size(tiny_lz4.size()));
    WithReadFromBytes(Packet(arps::kPacketFrame, huge_lz4_base, tiny_lz4, ""),
            [](const arps::ArpsReadResult& result) {
                CHECK(result.status == arps::ArpsReadStatus::ProtocolError);
            });

    std::vector<std::uint8_t> bad_lz4 = {0, 1, 2, 3, 4};
    auto bad_lz4_base = FrameBase(2, 2, 8, arps::kCompressionLz4Block,
            U32Size(raw.size()), U32Size(bad_lz4.size()));
    WithReadFromBytes(Packet(arps::kPacketFrame, bad_lz4_base, bad_lz4, ""),
            [](const arps::ArpsReadResult& result) {
                CHECK(result.status == arps::ArpsReadStatus::ProtocolError);
            });
}

void TestSendStart() {
    arps::ArpsSocket sockets[2];
    std::string error;
    CHECK(compat::CreateConnectedSocketPair(sockets, &error));
    arps::ArpsReceiver receiver;
    CHECK(receiver.AdoptConnectedSocket(sockets[0], &error));
    arps::ArpsStartOptions options;
    options.compression = "raw";
    CHECK(receiver.SendStart(options, &error));
    std::uint8_t header[arps::kHeaderLen];
    RecvAll(sockets[1], header, sizeof(header));
    CHECK(std::memcmp(header, arps::kMagic, arps::kMagicSize) == 0);
    CHECK(header[14] == 0);
    CHECK(header[15] == arps::kProtocolMinor);
    CHECK(header[16] == 0);
    CHECK(header[17] == arps::kPacketStart);
    std::uint32_t packet_len = ReadBe32(header + 28);
    std::vector<std::uint8_t> packet(packet_len);
    RecvAll(sockets[1], packet.data(), packet.size());
    std::uint32_t base_len = ReadBe32(packet.data());
    std::uint32_t bitmap_len = ReadBe32(packet.data() + 4 + base_len);
    std::uint32_t ext_len = ReadBe32(packet.data() + 4 + base_len + 4 + bitmap_len);
    const std::uint8_t* ext_data = packet.data() + 4 + base_len + 4 + bitmap_len + 4;
    std::string ext(reinterpret_cast<const char*>(ext_data), ext_len);
    CHECK(ext.find("\"keep_screen_on\":true") != std::string::npos);
    CHECK(ext.find("\"stream_mode\":\"push\"") != std::string::npos);
    CHECK(ext.find("require_non_black_start") == std::string::npos);
    compat::Close(sockets[1]);
}

void TestRequestFrame() {
    arps::ArpsSocket sockets[2];
    std::string error;
    CHECK(compat::CreateConnectedSocketPair(sockets, &error));
    arps::ArpsReceiver receiver;
    CHECK(receiver.AdoptConnectedSocket(sockets[0], &error));
    CHECK(receiver.RequestFrame(&error));
    std::uint8_t header[arps::kHeaderLen];
    RecvAll(sockets[1], header, sizeof(header));
    CHECK(std::memcmp(header, arps::kMagic, arps::kMagicSize) == 0);
    CHECK(header[14] == 0);
    CHECK(header[15] == arps::kProtocolMinor);
    CHECK(header[16] == 0);
    CHECK(header[17] == arps::kPacketFrameRequest);
    std::uint32_t packet_len = ReadBe32(header + 28);
    std::vector<std::uint8_t> packet(packet_len);
    RecvAll(sockets[1], packet.data(), packet.size());
    std::uint32_t base_len = ReadBe32(packet.data());
    std::uint32_t bitmap_len = ReadBe32(packet.data() + 4 + base_len);
    std::uint32_t ext_len = ReadBe32(packet.data() + 4 + base_len + 4 + bitmap_len);
    const std::uint8_t* ext_data = packet.data() + 4 + base_len + 4 + bitmap_len + 4;
    std::string ext(reinterpret_cast<const char*>(ext_data), ext_len);
    CHECK(base_len == 0);
    CHECK(bitmap_len == 0);
    CHECK(ext == "{}");
    compat::Close(sockets[1]);
}

void TestSendPowerControl() {
    arps::ArpsSocket sockets[2];
    std::string error;
    CHECK(compat::CreateConnectedSocketPair(sockets, &error));
    arps::ArpsReceiver receiver;
    CHECK(receiver.AdoptConnectedSocket(sockets[0], &error));
    CHECK(receiver.SendPowerControl(true, true, "task_start", &error));
    std::uint8_t header[arps::kHeaderLen];
    RecvAll(sockets[1], header, sizeof(header));
    CHECK(std::memcmp(header, arps::kMagic, arps::kMagicSize) == 0);
    CHECK(header[14] == 0);
    CHECK(header[15] == arps::kProtocolMinor);
    CHECK(header[16] == 0);
    CHECK(header[17] == arps::kPacketPowerControl);
    std::uint32_t packet_len = ReadBe32(header + 28);
    std::vector<std::uint8_t> packet(packet_len);
    RecvAll(sockets[1], packet.data(), packet.size());
    std::uint32_t base_len = ReadBe32(packet.data());
    std::uint32_t bitmap_len = ReadBe32(packet.data() + 4 + base_len);
    std::uint32_t ext_len = ReadBe32(packet.data() + 4 + base_len + 4 + bitmap_len);
    const std::uint8_t* ext_data = packet.data() + 4 + base_len + 4 + bitmap_len + 4;
    std::string ext(reinterpret_cast<const char*>(ext_data), ext_len);
    CHECK(base_len == 0);
    CHECK(bitmap_len == 0);
    CHECK(ext.find("\"keep_screen_on\":true") != std::string::npos);
    CHECK(ext.find("\"power_on_if_screen_off\":true") != std::string::npos);
    CHECK(ext.find("\"reason\":\"task_start\"") != std::string::npos);
    compat::Close(sockets[1]);
}

void TestSendStop() {
    arps::ArpsSocket sockets[2];
    std::string error;
    CHECK(compat::CreateConnectedSocketPair(sockets, &error));
    arps::ArpsReceiver receiver;
    CHECK(receiver.AdoptConnectedSocket(sockets[0], &error));
    CHECK(receiver.SendStop("host_stop", &error));
    std::uint8_t header[arps::kHeaderLen];
    RecvAll(sockets[1], header, sizeof(header));
    CHECK(std::memcmp(header, arps::kMagic, arps::kMagicSize) == 0);
    CHECK(header[14] == 0);
    CHECK(header[15] == arps::kProtocolMinor);
    CHECK(header[16] == 0);
    CHECK(header[17] == arps::kPacketStop);
    std::uint32_t packet_len = ReadBe32(header + 28);
    std::vector<std::uint8_t> packet(packet_len);
    RecvAll(sockets[1], packet.data(), packet.size());
    std::uint32_t base_len = ReadBe32(packet.data());
    std::uint32_t bitmap_len = ReadBe32(packet.data() + 4 + base_len);
    std::uint32_t ext_len = ReadBe32(packet.data() + 4 + base_len + 4 + bitmap_len);
    const std::uint8_t* ext_data = packet.data() + 4 + base_len + 4 + bitmap_len + 4;
    std::string ext(reinterpret_cast<const char*>(ext_data), ext_len);
    CHECK(base_len == 0);
    CHECK(bitmap_len == 0);
    CHECK(ext == "{\"reason\":\"host_stop\"}");
    compat::Close(sockets[1]);
}

}  // namespace

int main() {
    TestHello();
    TestReady();
    TestControlStatuses();
    TestRawFrame();
    TestLz4Frame();
    TestProtocolErrors();
    TestSendStart();
    TestRequestFrame();
    TestSendPowerControl();
    TestSendStop();
    std::cout << "receiver_tests: ok\n";
    return 0;
}
