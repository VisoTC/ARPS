package com.visotc.ARPS;

final class CapturedFrame {
    final byte[] raw;
    final byte[] precompressedPayload;
    final double precompressedMs;
    final int width;
    final int height;
    final int rowBytes;
    final int uncompressedLen;
    final int rotation;
    final int colorSpace;
    final long captureTimeNs;
    final double captureMs;
    final double copyMs;
    final double hardwareLockMs;
    final int hardwareBufferFormat;
    final long hardwareBufferUsage;
    final String captureApi;

    CapturedFrame(byte[] raw, int width, int height, int rowBytes, int rotation,
            int colorSpace, long captureTimeNs, double captureMs, double copyMs) {
        this(raw, null, -1.0, width, height, rowBytes, raw.length, rotation, colorSpace,
                captureTimeNs, captureMs, copyMs, -1.0, 0, 0,
                "android.window.ScreenCapture.captureDisplay");
    }

    CapturedFrame(byte[] raw, int width, int height, int rowBytes, int rotation,
            int colorSpace, long captureTimeNs, double captureMs, double copyMs,
            String captureApi) {
        this(raw, null, -1.0, width, height, rowBytes, raw.length, rotation, colorSpace,
                captureTimeNs, captureMs, copyMs, -1.0, 0, 0, captureApi);
    }

    CapturedFrame(byte[] precompressedPayload, double precompressedMs, int width, int height,
            int rowBytes, int uncompressedLen, int rotation, int colorSpace, long captureTimeNs,
            double captureMs, double hardwareLockMs, int hardwareBufferFormat,
            long hardwareBufferUsage, String captureApi) {
        this(null, precompressedPayload, precompressedMs, width, height, rowBytes,
                uncompressedLen, rotation, colorSpace, captureTimeNs, captureMs, 0.0,
                hardwareLockMs, hardwareBufferFormat, hardwareBufferUsage, captureApi);
    }

    private CapturedFrame(byte[] raw, byte[] precompressedPayload, double precompressedMs,
            int width, int height, int rowBytes, int uncompressedLen, int rotation,
            int colorSpace, long captureTimeNs, double captureMs, double copyMs,
            double hardwareLockMs, int hardwareBufferFormat, long hardwareBufferUsage,
            String captureApi) {
        this.raw = raw;
        this.precompressedPayload = precompressedPayload;
        this.precompressedMs = precompressedMs;
        this.width = width;
        this.height = height;
        this.rowBytes = rowBytes;
        this.uncompressedLen = uncompressedLen;
        this.rotation = rotation;
        this.colorSpace = colorSpace;
        this.captureTimeNs = captureTimeNs;
        this.captureMs = captureMs;
        this.copyMs = copyMs;
        this.hardwareLockMs = hardwareLockMs;
        this.hardwareBufferFormat = hardwareBufferFormat;
        this.hardwareBufferUsage = hardwareBufferUsage;
        this.captureApi = captureApi;
    }
}
