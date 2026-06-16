package com.visotc.ARPS;

import org.json.JSONObject;

final class Options {
    private static final int DEFAULT_MAX_PACKET_LEN = 64 * 1024 * 1024;

    String connectHost = "127.0.0.1";
    int connectPort = -1;
    int displayId = 0;
    int compressionType = CompressionType.LZ4_BLOCK;
    int maxFps = 30;
    int maxPacketLen = DEFAULT_MAX_PACKET_LEN;
    boolean powerOnIfScreenOff = true;
    boolean turnScreenOff = false;
    boolean keepScreenOn = true;
    CaptureMode captureMode = CaptureMode.AUTO;
    ExitPowerMode exitPowerMode = ExitPowerMode.RESTORE_PREVIOUS;
    int streamMode = StreamMode.PUSH;

    static Options parse(String[] args) {
        Options options = new Options();
        for (String arg : args) {
            if ("--help".equals(arg) || "-h".equals(arg)) {
                throw new IllegalArgumentException(usage());
            }
            if (!arg.startsWith("--") || arg.indexOf('=') < 0) {
                throw new IllegalArgumentException("Invalid argument: " + arg + "\n" + usage());
            }
            int split = arg.indexOf('=');
            String key = arg.substring(2, split);
            String value = arg.substring(split + 1);
            options.applyArgument(key, value);
        }

        if (options.connectPort <= 0 || options.connectPort > 65535) {
            throw new IllegalArgumentException("--connect-port=<1..65535> is required");
        }
        validateSupportedCompression(options.compressionType);
        return options;
    }

    void applyStart(JSONObject json) {
        if (json.has("display_id")) {
            displayId = json.optInt("display_id", displayId);
        }
        if (json.has("pixel_format")) {
            String pixelFormat = json.optString("pixel_format", "argb8888");
            if (!"argb8888".equals(pixelFormat)) {
                throw new IllegalArgumentException("Unsupported pixel_format=" + pixelFormat);
            }
        }
        if (json.has("compression")) {
            compressionType = CompressionType.parse(json.optString("compression"));
        }
        if (json.has("max_fps")) {
            maxFps = json.optInt("max_fps", maxFps);
        }
        if (json.has("max_packet_len")) {
            maxPacketLen = json.optInt("max_packet_len", maxPacketLen);
        }
        if (json.has("power_on_if_screen_off")) {
            powerOnIfScreenOff = json.optBoolean("power_on_if_screen_off", powerOnIfScreenOff);
        }
        if (json.has("turn_screen_off")) {
            turnScreenOff = json.optBoolean("turn_screen_off", turnScreenOff);
        }
        if (json.has("keep_screen_on")) {
            keepScreenOn = json.optBoolean("keep_screen_on", keepScreenOn);
        }
        if (json.has("capture_mode")) {
            captureMode = CaptureMode.parse(json.optString("capture_mode"));
        }
        if (json.has("exit_power_mode")) {
            exitPowerMode = ExitPowerMode.parse(json.optString("exit_power_mode"));
        }
        if (json.has("stream_mode")) {
            streamMode = StreamMode.parse(json.optString("stream_mode"));
        }
        validate();
    }

    private void applyArgument(String key, String value) {
        switch (key) {
            case "connect-host":
                connectHost = value;
                break;
            case "connect-port":
                connectPort = parseInt(key, value);
                break;
            case "display-id":
                displayId = parseInt(key, value);
                break;
            case "compression":
                compressionType = CompressionType.parse(value);
                break;
            case "max-fps":
                maxFps = parseInt(key, value);
                break;
            case "max-packet-len":
                maxPacketLen = parseInt(key, value);
                break;
            case "power-on-if-screen-off":
                powerOnIfScreenOff = parseBoolean(key, value);
                break;
            case "turn-screen-off":
                turnScreenOff = parseBoolean(key, value);
                break;
            case "keep-screen-on":
                keepScreenOn = parseBoolean(key, value);
                break;
            case "capture-mode":
                captureMode = CaptureMode.parse(value);
                break;
            case "exit-power-mode":
                exitPowerMode = ExitPowerMode.parse(value);
                break;
            case "stream-mode":
                streamMode = StreamMode.parse(value);
                break;
            default:
                throw new IllegalArgumentException("Unknown argument: --" + key);
        }
        validate();
    }

    private void validate() {
        if (displayId < 0) {
            throw new IllegalArgumentException("display_id must be >= 0");
        }
        if (maxFps < 0 || maxFps > 240) {
            throw new IllegalArgumentException("max_fps must be in 0..240");
        }
        if (maxPacketLen < 1024 || maxPacketLen > DEFAULT_MAX_PACKET_LEN) {
            throw new IllegalArgumentException("max_packet_len must be in 1024.."
                    + DEFAULT_MAX_PACKET_LEN);
        }
        validateSupportedCompression(compressionType);
    }

    private static void validateSupportedCompression(int compressionType) {
        if (compressionType != CompressionType.RAW
                && compressionType != CompressionType.LZ4_BLOCK) {
            throw new IllegalArgumentException("compression_type is reserved for v1: "
                    + compressionType);
        }
    }

    private static int parseInt(String key, String value) {
        try {
            return Integer.parseInt(value);
        } catch (NumberFormatException e) {
            throw new IllegalArgumentException("--" + key + " must be an integer: " + value, e);
        }
    }

    private static boolean parseBoolean(String key, String value) {
        if ("true".equals(value)) {
            return true;
        }
        if ("false".equals(value)) {
            return false;
        }
        throw new IllegalArgumentException("--" + key + " must be true or false");
    }

    private static String usage() {
        return "Usage: app_process / com.visotc.ARPS.Main "
                + "--connect-port=<port> [--connect-host=127.0.0.1] "
                + "[--compression=raw|lz4_block] [--max-fps=30] "
                + "[--keep-screen-on=true|false] "
                + "[--capture-mode=auto|hardware|bitmap] "
                + "[--stream-mode=push|pull]";
    }
}
