package com.visotc.ARPS;

import java.nio.charset.StandardCharsets;

final class Protocol {
    static final byte[] MAGIC = "ARPSBYVISOTC".getBytes(StandardCharsets.US_ASCII);
    static final int MAJOR = 1;
    static final int MINOR = 2;
    static final int HEADER_LEN = 32;

    static final int TYPE_HELLO = 1;
    static final int TYPE_START = 2;
    static final int TYPE_READY = 3;
    static final int TYPE_FRAME_REQUEST = 4;
    static final int TYPE_FRAME = 5;
    static final int TYPE_ERROR = 6;
    static final int TYPE_STOP = 7;
    static final int TYPE_POWER_CONTROL = 8;
    static final int TYPE_POWER_STATE = 9;

    static final int PIXEL_FORMAT_ANDROID_ARGB_8888_RAW = 1;

    private Protocol() {
    }
}
