#!/usr/bin/env python3
import argparse
import json
import socket
import struct
from pathlib import Path


MAGIC = b"ARPSBYVISOTC"
PROTOCOL_MAJOR = 1
PROTOCOL_MINOR = 1
HEADER = struct.Struct(">12sHHHHIII")
U32 = struct.Struct(">I")
FRAME_BASE = struct.Struct(">QQIIIIIIIIIIII")

TYPE_HELLO = 1
TYPE_START = 2
TYPE_READY = 3
TYPE_FRAME_REQUEST = 4
TYPE_FRAME = 5
TYPE_ERROR = 6
TYPE_STOP = 7
TYPE_POWER_CONTROL = 8


def read_exact(sock, length):
    chunks = []
    remaining = length
    while remaining:
        chunk = sock.recv(remaining)
        if not chunk:
            raise EOFError(f"socket closed with {remaining} bytes remaining")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def read_u32(sock):
    return U32.unpack(read_exact(sock, 4))[0]


def read_section_length(sock, packet_len, consumed):
    if packet_len - consumed < 4:
        raise ValueError("packet section length missing")
    length = read_u32(sock)
    if length > packet_len - consumed - 4:
        raise ValueError(f"section length exceeds packet body: {length}")
    return length


def read_packet(sock, max_packet_len):
    header = read_exact(sock, HEADER.size)
    magic, major, minor, packet_type, header_len, flags, sequence, packet_len = HEADER.unpack(header)
    if magic != MAGIC:
        raise ValueError(f"bad magic: {magic!r}")
    if header_len != HEADER.size:
        raise ValueError(f"bad header_len: {header_len}")
    if packet_len < 12 or packet_len > max_packet_len:
        raise ValueError(f"invalid packet_len: {packet_len}")

    consumed = 0
    base_len = read_section_length(sock, packet_len, consumed)
    consumed += 4
    base = read_exact(sock, base_len)
    consumed += base_len

    bitmap_len = read_section_length(sock, packet_len, consumed)
    consumed += 4
    bitmap = read_exact(sock, bitmap_len)
    consumed += bitmap_len

    ext_len = read_section_length(sock, packet_len, consumed)
    consumed += 4
    ext = read_exact(sock, ext_len)
    consumed += ext_len

    tail_len = packet_len - consumed
    if tail_len:
        read_exact(sock, tail_len)

    return {
        "major": major,
        "minor": minor,
        "type": packet_type,
        "flags": flags,
        "sequence": sequence,
        "base": base,
        "bitmap": bitmap,
        "ext": ext,
    }


def write_packet(sock, packet_type, base=b"", bitmap=b"", ext=b"", sequence=1):
    packet_len = 4 + len(base) + 4 + len(bitmap) + 4 + len(ext)
    sock.sendall(HEADER.pack(MAGIC, PROTOCOL_MAJOR, PROTOCOL_MINOR, packet_type,
                              HEADER.size, 0, sequence, packet_len))
    sock.sendall(U32.pack(len(base)))
    sock.sendall(base)
    sock.sendall(U32.pack(len(bitmap)))
    sock.sendall(bitmap)
    sock.sendall(U32.pack(len(ext)))
    sock.sendall(ext)


def parse_frame_base(base):
    if len(base) < FRAME_BASE.size:
        raise ValueError(f"frame base too small: {len(base)}")
    fields = FRAME_BASE.unpack(base[:FRAME_BASE.size])
    keys = [
        "frame_no",
        "monotonic_time_ns",
        "width",
        "height",
        "row_bytes",
        "rotation",
        "pixel_format",
        "compression_type",
        "uncompressed_len",
        "compressed_len",
        "display_id",
        "color_space",
        "payload_checksum",
        "base_flags",
    ]
    return dict(zip(keys, fields))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=27183)
    parser.add_argument("--frames", type=int, default=1)
    parser.add_argument("--compression", choices=["raw", "lz4_block"], default="lz4_block")
    parser.add_argument("--stream-mode", choices=["push", "pull"], default="push")
    parser.add_argument("--save-payload")
    args = parser.parse_args()

    max_packet_len = 64 * 1024 * 1024
    start = {
        "display_id": 0,
        "pixel_format": "argb8888",
        "compression": args.compression,
        "max_fps": 30,
        "max_packet_len": max_packet_len,
        "power_on_if_screen_off": True,
        "turn_screen_off": False,
        "keep_screen_on": True,
        "exit_power_mode": "restore_previous",
        "stream_mode": args.stream_mode,
    }

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((args.host, args.port))
        server.listen(1)
        print(f"listening on {args.host}:{args.port}")
        conn, addr = server.accept()

        with conn:
            print(f"accepted {addr}")
            hello = read_packet(conn, max_packet_len)
            if hello["type"] != TYPE_HELLO:
                raise ValueError(f"expected HELLO, got type={hello['type']}")
            print("HELLO", hello["ext"].decode("utf-8", "replace"))
            sequence = 1
            write_packet(conn, TYPE_START, ext=json.dumps(start).encode("utf-8"), sequence=sequence)
            sequence += 1

            ready = read_packet(conn, max_packet_len)
            if ready["type"] == TYPE_ERROR:
                raise ValueError(f"device ERROR before READY: {ready['ext'].decode('utf-8', 'replace')}")
            if ready["type"] != TYPE_READY:
                raise ValueError(f"expected READY, got type={ready['type']}")
            print("READY", ready["ext"].decode("utf-8", "replace"))

            last_payload = None
            for _ in range(args.frames):
                if args.stream_mode == "pull":
                    write_packet(conn, TYPE_FRAME_REQUEST, ext=b"{}", sequence=sequence)
                    sequence += 1
                packet = read_packet(conn, max_packet_len)
                if packet["type"] == TYPE_ERROR:
                    print("ERROR", packet["ext"].decode("utf-8", "replace"))
                    break
                if packet["type"] == TYPE_STOP:
                    print("STOP", packet["ext"].decode("utf-8", "replace"))
                    break
                if packet["type"] != TYPE_FRAME:
                    raise ValueError(f"unexpected packet type={packet['type']}")

                meta = parse_frame_base(packet["base"])
                if meta["compressed_len"] != len(packet["bitmap"]):
                    raise ValueError("compressed_len does not match bitmap payload length")
                print("FRAME", json.dumps(meta, ensure_ascii=False))
                if packet["ext"]:
                    print("EXT", packet["ext"].decode("utf-8", "replace"))
                last_payload = packet["bitmap"]

            if args.save_payload and last_payload is not None:
                Path(args.save_payload).write_bytes(last_payload)
                print(f"saved payload to {args.save_payload}")


if __name__ == "__main__":
    main()
