package com.visotc.ARPS;

import java.io.DataInputStream;
import java.io.EOFException;
import java.io.IOException;
import java.io.InputStream;
import java.util.Arrays;

final class ProtocolReader {
    private final DataInputStream in;

    ProtocolReader(InputStream in) {
        this.in = new DataInputStream(in);
    }

    Packet readPacket(int maxPacketLen) throws IOException {
        byte[] magic = new byte[Protocol.MAGIC.length];
        in.readFully(magic);
        if (!Arrays.equals(magic, Protocol.MAGIC)) {
            throw new IOException("Invalid ARPS magic");
        }

        int major = in.readUnsignedShort();
        int minor = in.readUnsignedShort();
        int type = in.readUnsignedShort();
        int headerLen = in.readUnsignedShort();
        int flags = in.readInt();
        int sequence = in.readInt();
        int packetLen = in.readInt();

        if (major != Protocol.MAJOR) {
            throw new IOException("Unsupported protocol_major=" + major);
        }
        if (headerLen != Protocol.HEADER_LEN) {
            throw new IOException("Unsupported header_len=" + headerLen);
        }
        if (packetLen < 12 || packetLen > maxPacketLen) {
            throw new IOException("Invalid packet_len=" + packetLen);
        }

        int consumed = 0;
        int baseLen = readBodyLength(packetLen, consumed);
        consumed += 4;
        skipFully(baseLen);
        consumed += baseLen;

        int bitmapLen = readBodyLength(packetLen, consumed);
        consumed += 4;
        skipFully(bitmapLen);
        consumed += bitmapLen;

        int extLen = readBodyLength(packetLen, consumed);
        consumed += 4;
        if (extLen > packetLen - consumed) {
            throw new IOException("ext_len exceeds packet body: " + extLen);
        }
        byte[] ext = new byte[extLen];
        in.readFully(ext);
        consumed += extLen;

        int tailLen = packetLen - consumed;
        if (tailLen < 0) {
            throw new IOException("Packet overread");
        }
        skipFully(tailLen);

        return new Packet(major, minor, type, flags, sequence, ext);
    }

    private int readBodyLength(int packetLen, int consumed) throws IOException {
        if (packetLen - consumed < 4) {
            throw new EOFException("Packet section length missing");
        }
        int len = in.readInt();
        if (len < 0 || len > packetLen - consumed - 4) {
            throw new IOException("Invalid section length=" + len);
        }
        return len;
    }

    private void skipFully(int len) throws IOException {
        int remaining = len;
        while (remaining > 0) {
            int skipped = in.skipBytes(remaining);
            if (skipped <= 0) {
                if (in.read() == -1) {
                    throw new EOFException("Unexpected EOF while skipping");
                }
                skipped = 1;
            }
            remaining -= skipped;
        }
    }

    static final class Packet {
        final int major;
        final int minor;
        final int type;
        final int flags;
        final int sequence;
        final byte[] ext;

        Packet(int major, int minor, int type, int flags, int sequence, byte[] ext) {
            this.major = major;
            this.minor = minor;
            this.type = type;
            this.flags = flags;
            this.sequence = sequence;
            this.ext = ext;
        }
    }
}
