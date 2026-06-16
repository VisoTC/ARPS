package com.visotc.ARPS;

import org.json.JSONObject;

import java.io.BufferedOutputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;

final class ProtocolWriter {
    private static final byte[] EMPTY = new byte[0];
    private static final int FRAME_BASE_LEN = 64;

    private final DataOutputStream out;
    private int sequence = 1;

    ProtocolWriter(OutputStream outputStream) {
        out = new DataOutputStream(new BufferedOutputStream(outputStream, 256 * 1024));
    }

    synchronized void writeJson(int type, JSONObject json) throws IOException {
        byte[] ext = json.toString().getBytes(StandardCharsets.UTF_8);
        writePacket(type, EMPTY, EMPTY, ext);
    }

    synchronized void writeFrame(CapturedFrame frame, byte[] payload, int compressionType,
            long frameNo, double compressMs) throws IOException {
        byte[] base = frameBaseData(frame, payload.length, compressionType, frameNo);
        byte[] ext = frameExtData(frame, compressionType, compressMs)
                .getBytes(StandardCharsets.UTF_8);
        writePacket(Protocol.TYPE_FRAME, base, payload, ext);
    }

    private void writePacket(int type, byte[] base, byte[] payload, byte[] ext)
            throws IOException {
        int packetLen = 4 + base.length + 4 + payload.length + 4 + ext.length;
        out.write(Protocol.MAGIC);
        out.writeShort(Protocol.MAJOR);
        out.writeShort(Protocol.MINOR);
        out.writeShort(type);
        out.writeShort(Protocol.HEADER_LEN);
        out.writeInt(0);
        out.writeInt(sequence++);
        out.writeInt(packetLen);
        out.writeInt(base.length);
        out.write(base);
        out.writeInt(payload.length);
        out.write(payload);
        out.writeInt(ext.length);
        out.write(ext);
        out.flush();
    }

    private byte[] frameBaseData(CapturedFrame frame, int compressedLen, int compressionType,
            long frameNo) {
        ByteBuffer buffer = ByteBuffer.allocate(FRAME_BASE_LEN).order(ByteOrder.BIG_ENDIAN);
        buffer.putLong(frameNo);
        buffer.putLong(frame.captureTimeNs);
        buffer.putInt(frame.width);
        buffer.putInt(frame.height);
        buffer.putInt(frame.rowBytes);
        buffer.putInt(frame.rotation);
        buffer.putInt(Protocol.PIXEL_FORMAT_ANDROID_ARGB_8888_RAW);
        buffer.putInt(compressionType);
        buffer.putInt(frame.uncompressedLen);
        buffer.putInt(compressedLen);
        buffer.putInt(0);
        buffer.putInt(frame.colorSpace);
        buffer.putInt(0);
        buffer.putInt(0);
        return buffer.array();
    }

    private String frameExtData(CapturedFrame frame, int compressionType, double compressMs) {
        return "{"
                + "\"capture_api\":\"" + frame.captureApi + "\","
                + "\"android_sdk\":" + android.os.Build.VERSION.SDK_INT + ","
                + "\"compression\":\"" + CompressionType.nameOf(compressionType) + "\","
                + "\"capture_ms\":" + frame.captureMs + ","
                + "\"copy_ms\":" + frame.copyMs + ","
                + "\"compress_ms\":" + compressMs
                + hardwareExtData(frame)
                + "}";
    }

    private String hardwareExtData(CapturedFrame frame) {
        if (frame.hardwareLockMs < 0.0) {
            return "";
        }
        return ",\"lock_ms\":" + frame.hardwareLockMs
                + ",\"hardware_buffer_format\":" + frame.hardwareBufferFormat
                + ",\"hardware_buffer_usage\":" + frame.hardwareBufferUsage;
    }
}
