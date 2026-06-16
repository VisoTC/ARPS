package com.visotc.ARPS;

import java.util.Locale;

enum CaptureMode {
    AUTO,
    HARDWARE,
    BITMAP;

    static CaptureMode parse(String value) {
        String normalized = value.toLowerCase(Locale.US);
        if ("auto".equals(normalized)) {
            return AUTO;
        }
        if ("hardware".equals(normalized) || "hardware_buffer".equals(normalized)
                || "ahardwarebuffer".equals(normalized)) {
            return HARDWARE;
        }
        if ("bitmap".equals(normalized) || "capture_display".equals(normalized)) {
            return BITMAP;
        }
        throw new IllegalArgumentException("Unknown capture_mode: " + value);
    }
}
