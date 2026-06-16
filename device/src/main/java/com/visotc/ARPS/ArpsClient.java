package com.visotc.ARPS;

import android.os.Build;
import android.os.SystemClock;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.IOException;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.net.SocketException;
import java.nio.charset.StandardCharsets;

final class ArpsClient {
    private static final int CONNECT_TIMEOUT_MS = 5000;
    private static final int START_TIMEOUT_MS = 10000;
    private static final long NANOS_PER_SECOND = 1000000000L;

    private final Options options;
    private volatile boolean stopRequested;
    private volatile Socket socket;
    private ProtocolWriter writer;
    private PowerController powerController;
    private boolean cleanupDone;
    private boolean previousScreenOn;
    private boolean powerStateKnown;

    ArpsClient(Options options) {
        this.options = options;
    }

    int run() {
        if (Build.VERSION.SDK_INT < 31) {
            Log.e("Android 12/API 31 or newer is required, current SDK=" + Build.VERSION.SDK_INT);
            return 2;
        }

        try {
            Lz4.load();
        } catch (Throwable e) {
            Log.e("Failed to load native LZ4", e);
            return 3;
        }

        int exitCode = 0;
        try {
            powerController = new PowerController();
            previousScreenOn = powerController.isScreenOn(options.displayId);
            powerStateKnown = true;

            connect();
            writer = new ProtocolWriter(socket.getOutputStream());
            writer.writeJson(Protocol.TYPE_HELLO, buildHello());

            ProtocolReader reader = new ProtocolReader(socket.getInputStream());
            socket.setSoTimeout(START_TIMEOUT_MS);
            ProtocolReader.Packet start = reader.readPacket(options.maxPacketLen);
            if (start.type != Protocol.TYPE_START) {
                throw new IOException("Expected START packet, got type=" + start.type);
            }
            options.applyStart(new JSONObject(new String(start.ext, StandardCharsets.UTF_8)));
            socket.setSoTimeout(0);

            if (options.powerOnIfScreenOff && !previousScreenOn) {
                powerController.pressPower(options.displayId);
                SystemClock.sleep(500);
            }
            if (options.keepScreenOn) {
                powerController.acquireWakeLock(options.displayId);
            }

            streamFrames(reader);
            if (!stopRequested && writer != null) {
                writer.writeJson(Protocol.TYPE_STOP, stopJson("normal_stop"));
            }
        } catch (Throwable e) {
            if (isExpectedDisconnect(e)) {
                Log.i("Host disconnected, exiting");
                return 0;
            }
            exitCode = 1;
            Log.e("Client stopped with error", e);
            sendErrorQuietly(e);
        } finally {
            stopRequested = true;
            closeSocketQuietly();
            cleanupPowerQuietly();
        }

        return exitCode;
    }

    void requestStop() {
        stopRequested = true;
        closeSocketQuietly();
    }

    void shutdown() {
        requestStop();
        cleanupPowerQuietly();
    }

    private void connect() throws IOException {
        Socket s = new Socket();
        s.setTcpNoDelay(true);
        s.connect(new InetSocketAddress(options.connectHost, options.connectPort), CONNECT_TIMEOUT_MS);
        socket = s;
        Log.i("Connected to " + options.connectHost + ":" + options.connectPort);
    }

    private void streamFrames(ProtocolReader reader) throws Exception {
        ScreenCapturer capturer = new ScreenCapturer(options.captureMode);
        StreamState state = new StreamState();

        try {
            PreparedFrame warmup = captureAndCompress(capturer);
            writer.writeJson(Protocol.TYPE_READY, readyJson(warmup));
            Log.i("READY stream_mode=" + StreamMode.nameOf(options.streamMode)
                    + " warmup_capture_ms=" + warmup.frame.captureMs
                    + " warmup_compress_ms=" + warmup.compressMs);

            if (options.streamMode == StreamMode.PULL) {
                streamPullFrames(reader, capturer, state);
            } else {
                streamPushFrames(capturer, state);
            }
        } finally {
            capturer.close();
        }
    }

    private void streamPushFrames(ScreenCapturer capturer, StreamState state) throws Exception {
        long frameIntervalNs = options.maxFps > 0 ? NANOS_PER_SECOND / options.maxFps : 0;
        while (!stopRequested) {
            long loopStartNs = SystemClock.elapsedRealtimeNanos();
            writeNextFrame(capturer, state);

            if (frameIntervalNs > 0) {
                long elapsedNs = SystemClock.elapsedRealtimeNanos() - loopStartNs;
                long remainingNs = frameIntervalNs - elapsedNs;
                if (remainingNs > 0) {
                    SystemClock.sleep(remainingNs / 1000000L);
                }
            }
        }
    }

    private void streamPullFrames(ProtocolReader reader, ScreenCapturer capturer,
            StreamState state) throws Exception {
        while (!stopRequested) {
            ProtocolReader.Packet request = reader.readPacket(options.maxPacketLen);
            if (request.type == Protocol.TYPE_FRAME_REQUEST) {
                writeNextFrame(capturer, state);
            } else if (request.type == Protocol.TYPE_STOP) {
                stopRequested = true;
            } else {
                throw new IOException("Expected FRAME_REQUEST packet, got type=" + request.type);
            }
        }
    }

    private void writeNextFrame(ScreenCapturer capturer, StreamState state) throws Exception {
        PreparedFrame prepared = captureAndCompress(capturer);
        state.frameNo++;
        writer.writeFrame(prepared.frame, prepared.payload, options.compressionType,
                state.frameNo, prepared.compressMs);

        if (!state.displayPowerOff && options.turnScreenOff) {
            state.displayPowerOff = powerController.setDisplayPower(options.displayId, false);
            Log.i("setDisplayPower(false) result=" + state.displayPowerOff);
        }
    }

    private PreparedFrame captureAndCompress(ScreenCapturer capturer) throws Exception {
        if (options.compressionType == CompressionType.LZ4_BLOCK) {
            CapturedFrame hardwareFrame = capturer.captureHardwareLz4(options.displayId);
            if (hardwareFrame != null) {
                return new PreparedFrame(hardwareFrame, hardwareFrame.precompressedPayload,
                        hardwareFrame.precompressedMs);
            }
        }

        CapturedFrame frame = capturer.capture(options.displayId);
        if (frame.precompressedPayload != null) {
            return new PreparedFrame(frame, frame.precompressedPayload, frame.precompressedMs);
        }

        long compressStartNs = SystemClock.elapsedRealtimeNanos();

        byte[] payload;
        if (options.compressionType == CompressionType.RAW) {
            payload = frame.raw;
        } else if (options.compressionType == CompressionType.LZ4_BLOCK) {
            payload = Lz4.compress(frame.raw, frame.raw.length);
        } else {
            throw new IOException("Unsupported compression_type=" + options.compressionType);
        }

        long compressEndNs = SystemClock.elapsedRealtimeNanos();
        return new PreparedFrame(frame, payload, nanosToMillis(compressEndNs - compressStartNs));
    }

    private JSONObject buildHello() throws Exception {
        JSONObject root = new JSONObject();
        JSONObject device = new JSONObject();
        device.put("manufacturer", Build.MANUFACTURER);
        device.put("brand", Build.BRAND);
        device.put("model", Build.MODEL);
        device.put("android_sdk", Build.VERSION.SDK_INT);
        device.put("android_release", Build.VERSION.RELEASE);
        root.put("device", device);

        JSONObject capabilities = new JSONObject();
        capabilities.put("pixel_formats", new JSONArray().put("argb8888"));
        capabilities.put("compressions", new JSONArray().put("raw").put("lz4_block"));
        capabilities.put("screen_power", true);
        capabilities.put("max_packet_len", options.maxPacketLen);
        capabilities.put("native_lz4", true);
        capabilities.put("stream_modes", new JSONArray().put("push").put("pull"));
        root.put("capabilities", capabilities);
        return root;
    }

    private JSONObject readyJson(PreparedFrame warmup) throws Exception {
        JSONObject json = new JSONObject();
        json.put("stream_mode", StreamMode.nameOf(options.streamMode));
        json.put("compression", CompressionType.nameOf(options.compressionType));
        json.put("capture_api", warmup.frame.captureApi);
        json.put("capture_ms", warmup.frame.captureMs);
        json.put("copy_ms", warmup.frame.copyMs);
        json.put("compress_ms", warmup.compressMs);
        return json;
    }

    private JSONObject stopJson(String reason) throws Exception {
        JSONObject json = new JSONObject();
        json.put("reason", reason);
        return json;
    }

    private void sendErrorQuietly(Throwable e) {
        ProtocolWriter w = writer;
        if (w == null) {
            return;
        }
        try {
            JSONObject json = new JSONObject();
            json.put("message", String.valueOf(e.getMessage()));
            json.put("type", e.getClass().getName());
            w.writeJson(Protocol.TYPE_ERROR, json);
        } catch (Throwable ignored) {
            // The socket is commonly already closed when this path runs.
        }
    }

    private synchronized void cleanupPowerQuietly() {
        if (cleanupDone || powerController == null || !powerStateKnown) {
            return;
        }
        cleanupDone = true;
        try {
            powerController.releaseWakeLock();
            powerController.applyExitMode(options.displayId, previousScreenOn, options.exitPowerMode);
        } catch (Throwable e) {
            Log.e("Failed to restore screen power", e);
        }
    }

    private void closeSocketQuietly() {
        Socket s = socket;
        if (s == null) {
            return;
        }
        try {
            s.close();
        } catch (IOException ignored) {
            // ignored
        }
    }

    private static double nanosToMillis(long ns) {
        return ns / 1000000.0;
    }

    private static boolean isExpectedDisconnect(Throwable throwable) {
        Throwable current = throwable;
        while (current != null) {
            if (current instanceof SocketException) {
                String message = current.getMessage();
                return message == null
                        || message.contains("Broken pipe")
                        || message.contains("Socket closed")
                        || message.contains("Connection reset");
            }
            current = current.getCause();
        }
        return false;
    }

    private static final class PreparedFrame {
        final CapturedFrame frame;
        final byte[] payload;
        final double compressMs;

        PreparedFrame(CapturedFrame frame, byte[] payload, double compressMs) {
            this.frame = frame;
            this.payload = payload;
            this.compressMs = compressMs;
        }
    }

    private static final class StreamState {
        long frameNo;
        boolean displayPowerOff;
    }
}
