package com.visotc.ARPS;

import android.os.IInterface;
import android.os.SystemClock;
import android.view.InputDevice;
import android.view.InputEvent;
import android.view.KeyCharacterMap;
import android.view.KeyEvent;

import java.lang.reflect.Method;

final class InputController {
    private static final int INJECT_INPUT_EVENT_MODE_ASYNC = 0;

    private final IInterface inputManager;
    private Method injectInputEventMethod;

    InputController() {
        inputManager = SystemServices.getInterface("input", "android.hardware.input.IInputManager");
    }

    boolean pressPower() {
        return injectKey(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_POWER)
                && injectKey(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_POWER);
    }

    private boolean injectKey(int action, int keyCode) {
        long now = SystemClock.uptimeMillis();
        KeyEvent event = new KeyEvent(now, now, action, keyCode, 0, 0,
                KeyCharacterMap.VIRTUAL_KEYBOARD, 0, 0, InputDevice.SOURCE_KEYBOARD);
        try {
            return (Boolean) getInjectInputEventMethod().invoke(inputManager, event,
                    INJECT_INPUT_EVENT_MODE_ASYNC);
        } catch (Throwable e) {
            Log.e("injectInputEvent failed", e);
            return false;
        }
    }

    private Method getInjectInputEventMethod() throws NoSuchMethodException {
        if (injectInputEventMethod == null) {
            injectInputEventMethod = inputManager.getClass()
                    .getMethod("injectInputEvent", InputEvent.class, int.class);
        }
        return injectInputEventMethod;
    }
}
