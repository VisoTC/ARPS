package com.visotc.ARPS;

import java.util.Locale;

final class StreamMode {
    static final int PUSH = 0;
    static final int PULL = 1;

    private StreamMode() {
    }

    static int parse(String value) {
        String normalized = value.toLowerCase(Locale.US);
        if ("push".equals(normalized)) {
            return PUSH;
        }
        if ("pull".equals(normalized)) {
            return PULL;
        }
        throw new IllegalArgumentException("Unknown stream_mode: " + value);
    }

    static String nameOf(int mode) {
        switch (mode) {
            case PUSH:
                return "push";
            case PULL:
                return "pull";
            default:
                return "unknown";
        }
    }
}
