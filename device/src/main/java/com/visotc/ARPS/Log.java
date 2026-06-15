package com.visotc.ARPS;

final class Log {
    private static final String PREFIX = "[ARPS] ";

    private Log() {
    }

    static void i(String message) {
        System.err.println(PREFIX + message);
    }

    static void e(String message) {
        System.err.println(PREFIX + "ERROR: " + message);
    }

    static void e(String message, Throwable throwable) {
        e(message + ": " + throwable);
        throwable.printStackTrace(System.err);
    }
}
