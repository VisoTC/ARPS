#include "arps/receiver.h"

#include <assert.h>
#include <lz4.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

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

void SendAll(int fd, const std::vector<std::uint8_t>& bytes) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        ssize_t wrote = send(fd, bytes.data() + offset, bytes.size() - offset, 0);
        assert(wrote > 0);
        offset += static_cast<std::size_t>(wrote);
    }
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
    assert(base.size() == arps::kFrameBaseLenV1);
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
    std::uint32_t packet_len = 4 + base.size() + 4 + bitmap.size() + 4 + ext.size()
            + tail.size();
    WriteBe32(out, packet_len);
    WriteBe32(out, static_cast<std::uint32_t>(base.size()));
    out.insert(out.end(), base.begin(), base.end());
    WriteBe32(out, static_cast<std::uint32_t>(bitmap.size()));
    out.insert(out.end(), bitmap.begin(), bitmap.end());
    WriteBe32(out, static_cast<std::uint32_t>(ext.size()));
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
    int fds[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    SendAll(fds[1], bytes);
    close(fds[1]);
    arps::ArpsReceiver receiver;
    std::string error;
    assert(receiver.AdoptConnectedSocket(fds[0], &error));
    arps::ArpsReadResult result = receiver.ReadNext(1000);
    fn(result);
}

void TestHello() {
    WithReadFromBytes(Packet(arps::kPacketHello, {}, {}, "{\"ok\":true}"),
            [](const arps::ArpsReadResult& result) {
                assert(result.status == arps::ArpsReadStatus::Hello);
                assert(result.json == "{\"ok\":true}");
            });
}

void TestRawFrame() {
    std::vector<std::uint8_t> raw(4 * 3 * 4);
    for (std::size_t i = 0; i < raw.size(); ++i) {
        raw[i] = static_cast<std::uint8_t>(i);
    }
    auto base = FrameBase(4, 3, 16, arps::kCompressionRaw, raw.size(), raw.size());
    WithReadFromBytes(Packet(arps::kPacketFrame, base, raw,
            "{\"capture_ms\":1.5,\"copy_ms\":0.5}", {1, 2, 3}),
            [&raw](const arps::ArpsReadResult& result) {
                assert(result.status == arps::ArpsReadStatus::Frame);
                assert(result.frame.meta.frame_no == 7);
                assert(result.frame.meta.width == 4);
                assert(result.frame.meta.height == 3);
                assert(result.frame.argb8888_len == raw.size());
                assert(memcmp(result.frame.argb8888, raw.data(), raw.size()) == 0);
                assert(result.frame.device_timings.capture_ms == 1.5);
            });
}

void TestLz4Frame() {
    std::vector<std::uint8_t> raw(8 * 4 * 4);
    for (std::size_t i = 0; i < raw.size(); ++i) {
        raw[i] = static_cast<std::uint8_t>((i * 17) & 0xff);
    }
    std::vector<std::uint8_t> compressed(LZ4_compressBound(static_cast<int>(raw.size())));
    int written = LZ4_compress_default(reinterpret_cast<const char*>(raw.data()),
            reinterpret_cast<char*>(compressed.data()), static_cast<int>(raw.size()),
            static_cast<int>(compressed.size()));
    assert(written > 0);
    compressed.resize(static_cast<std::size_t>(written));
    auto base = FrameBase(8, 4, 32, arps::kCompressionLz4Block, raw.size(),
            compressed.size());
    WithReadFromBytes(Packet(arps::kPacketFrame, base, compressed,
            "{\"compress_ms\":2.25}"), [&raw, &compressed](
            const arps::ArpsReadResult& result) {
                assert(result.status == arps::ArpsReadStatus::Frame);
                assert(result.frame.argb8888_len == raw.size());
                assert(memcmp(result.frame.argb8888, raw.data(), raw.size()) == 0);
                assert(result.frame.bitmap_payload_len == compressed.size());
                assert(result.frame.device_timings.compress_ms == 2.25);
            });
}

void TestProtocolErrors() {
    WithReadFromBytes(PacketWithBadMagic(), [](const arps::ArpsReadResult& result) {
        assert(result.status == arps::ArpsReadStatus::ProtocolError);
    });
    WithReadFromBytes(PacketWithLargeLen(), [](const arps::ArpsReadResult& result) {
        assert(result.status == arps::ArpsReadStatus::ProtocolError);
    });

    std::vector<std::uint8_t> raw(16, 0xaa);
    auto short_base = FrameBase(2, 2, 8, arps::kCompressionRaw, raw.size(), raw.size());
    short_base.resize(12);
    WithReadFromBytes(Packet(arps::kPacketFrame, short_base, raw, ""),
            [](const arps::ArpsReadResult& result) {
                assert(result.status == arps::ArpsReadStatus::ProtocolError);
            });

    auto mismatch_base = FrameBase(2, 2, 8, arps::kCompressionRaw, raw.size(), raw.size() + 1);
    WithReadFromBytes(Packet(arps::kPacketFrame, mismatch_base, raw, ""),
            [](const arps::ArpsReadResult& result) {
                assert(result.status == arps::ArpsReadStatus::ProtocolError);
            });

    std::vector<std::uint8_t> bad_lz4 = {0, 1, 2, 3, 4};
    auto bad_lz4_base = FrameBase(2, 2, 8, arps::kCompressionLz4Block, raw.size(),
            bad_lz4.size());
    WithReadFromBytes(Packet(arps::kPacketFrame, bad_lz4_base, bad_lz4, ""),
            [](const arps::ArpsReadResult& result) {
                assert(result.status == arps::ArpsReadStatus::ProtocolError);
            });
}

void TestSendStart() {
    int fds[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    arps::ArpsReceiver receiver;
    std::string error;
    assert(receiver.AdoptConnectedSocket(fds[0], &error));
    arps::ArpsStartOptions options;
    options.compression = "raw";
    assert(receiver.SendStart(options, &error));
    std::uint8_t header[arps::kHeaderLen];
    assert(recv(fds[1], header, sizeof(header), MSG_WAITALL) == sizeof(header));
    assert(memcmp(header, arps::kMagic, arps::kMagicSize) == 0);
    assert(header[16] == 0);
    assert(header[17] == arps::kPacketStart);
    close(fds[1]);
}

}  // namespace

int main() {
    TestHello();
    TestRawFrame();
    TestLz4Frame();
    TestProtocolErrors();
    TestSendStart();
    std::cout << "receiver_tests: ok\n";
    return 0;
}
