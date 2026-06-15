package com.visotc.ARPS;

import java.util.Locale;

enum ExitPowerMode {
    RESTORE_PREVIOUS,
    KEEP_ON,
    TURN_OFF;

    static ExitPowerMode parse(String value) {
        String normalized = value.toLowerCase(Locale.US);
        if ("restore_previous".equals(normalized)) {
            return RESTORE_PREVIOUS;
        }
        if ("keep_on".equals(normalized)) {
            return KEEP_ON;
        }
        if ("turn_off".equals(normalized)) {
            return TURN_OFF;
        }
        throw new IllegalArgumentException("Unknown exit_power_mode: " + value);
    }
}
