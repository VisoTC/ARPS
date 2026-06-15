package com.visotc.ARPS;

import java.util.Locale;

final class CompressionType {
    static final int RAW = 0;
    static final int LZ4_BLOCK = 1;
    static final int DELTA_LZ4 = 2;
    static final int EXTENDED = 3;

    private CompressionType() {
    }

    static int parse(String value) {
        String normalized = value.toLowerCase(Locale.US);
        if ("raw".equals(normalized)) {
            return RAW;
        }
        if ("lz4".equals(normalized) || "lz4_block".equals(normalized)) {
            return LZ4_BLOCK;
        }
        if ("delta_lz4".equals(normalized)) {
            return DELTA_LZ4;
        }
        if ("ext".equals(normalized) || "extended".equals(normalized)) {
            return EXTENDED;
        }
        throw new IllegalArgumentException("Unknown compression: " + value);
    }

    static String nameOf(int compressionType) {
        switch (compressionType) {
            case RAW:
                return "raw";
            case LZ4_BLOCK:
                return "lz4_block";
            case DELTA_LZ4:
                return "delta_lz4";
            case EXTENDED:
                return "extended";
            default:
                return "unknown";
        }
    }
}
