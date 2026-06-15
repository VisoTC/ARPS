package com.visotc.ARPS;

final class CapturedFrame {
    final byte[] raw;
    final int width;
    final int height;
    final int rowBytes;
    final int rotation;
    final int colorSpace;
    final long captureTimeNs;
    final double captureMs;
    final double copyMs;
    final String captureApi;

    CapturedFrame(byte[] raw, int width, int height, int rowBytes, int rotation,
            int colorSpace, long captureTimeNs, double captureMs, double copyMs) {
        this(raw, width, height, rowBytes, rotation, colorSpace, captureTimeNs, captureMs,
                copyMs, "android.window.ScreenCapture.captureDisplay");
    }

    CapturedFrame(byte[] raw, int width, int height, int rowBytes, int rotation,
            int colorSpace, long captureTimeNs, double captureMs, double copyMs,
            String captureApi) {
        this.raw = raw;
        this.width = width;
        this.height = height;
        this.rowBytes = rowBytes;
        this.rotation = rotation;
        this.colorSpace = colorSpace;
        this.captureTimeNs = captureTimeNs;
        this.captureMs = captureMs;
        this.copyMs = copyMs;
        this.captureApi = captureApi;
    }
}
