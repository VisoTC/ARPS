package com.visotc.ARPS;

import android.os.Build;
import android.os.IInterface;
import android.os.SystemClock;

import java.lang.reflect.Method;

final class PowerController {
    private final IInterface powerManager;
    private InputController inputController;
    private Method isScreenOnMethod;

    PowerController() {
        powerManager = SystemServices.getInterface("power", "android.os.IPowerManager");
    }

    boolean isScreenOn(int displayId) {
        try {
            Method method = getIsScreenOnMethod();
            if (Build.VERSION.SDK_INT >= 34) {
                return (Boolean) method.invoke(powerManager, displayId);
            }
            return (Boolean) method.invoke(powerManager);
        } catch (Throwable e) {
            Log.e("isScreenOn failed", e);
            return false;
        }
    }

    boolean pressPower(int displayId) {
        if (inputController == null) {
            inputController = new InputController();
        }
        boolean ok = inputController.pressPower();
        Log.i("pressPower(display_id=" + displayId + ") result=" + ok);
        return ok;
    }

    boolean setDisplayPower(int displayId, boolean on) {
        return SurfaceControlBridge.setDisplayPower(displayId, on);
    }

    void applyExitMode(int displayId, boolean previousScreenOn, ExitPowerMode mode) {
        switch (mode) {
            case KEEP_ON:
                setDisplayPower(displayId, true);
                if (!isScreenOn(displayId)) {
                    pressPower(displayId);
                    SystemClock.sleep(300);
                }
                break;
            case TURN_OFF:
                setDisplayPower(displayId, true);
                SystemClock.sleep(100);
                if (isScreenOn(displayId)) {
                    pressPower(displayId);
                }
                break;
            case RESTORE_PREVIOUS:
            default:
                if (previousScreenOn) {
                    setDisplayPower(displayId, true);
                    if (!isScreenOn(displayId)) {
                        pressPower(displayId);
                    }
                } else {
                    setDisplayPower(displayId, true);
                    SystemClock.sleep(100);
                    if (isScreenOn(displayId)) {
                        pressPower(displayId);
                    }
                }
                break;
        }
    }

    private Method getIsScreenOnMethod() throws NoSuchMethodException {
        if (isScreenOnMethod == null) {
            if (Build.VERSION.SDK_INT >= 34) {
                isScreenOnMethod = powerManager.getClass()
                        .getMethod("isDisplayInteractive", int.class);
            } else {
                isScreenOnMethod = powerManager.getClass().getMethod("isInteractive");
            }
        }
        return isScreenOnMethod;
    }
}
