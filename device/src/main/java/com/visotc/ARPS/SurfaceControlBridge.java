package com.visotc.ARPS;

import android.annotation.SuppressLint;
import android.os.Build;
import android.os.IBinder;

import java.lang.reflect.Method;

@SuppressLint({"PrivateApi", "SoonBlockedPrivateApi", "BlockedPrivateApi"})
final class SurfaceControlBridge {
    private static final int POWER_MODE_OFF = 0;
    private static final int POWER_MODE_NORMAL = 2;
    private static final Class<?> CLASS = initClass();

    private SurfaceControlBridge() {
    }

    static IBinder getCaptureDisplayToken() {
        if (Build.VERSION.SDK_INT >= 34 && DisplayControlBridge.isAvailable()) {
            long[] ids = DisplayControlBridge.getPhysicalDisplayIds();
            if (ids != null && ids.length > 0) {
                IBinder token = DisplayControlBridge.getPhysicalDisplayToken(ids[0]);
                if (token != null) {
                    return token;
                }
            }
        }

        IBinder token = getBuiltInDisplay();
        if (token != null) {
            return token;
        }
        throw new IllegalStateException("Unable to obtain display token");
    }

    static boolean setDisplayPower(int displayId, boolean on) {
        int mode = on ? POWER_MODE_NORMAL : POWER_MODE_OFF;
        boolean applyToPhysicalDisplays = Build.VERSION.SDK_INT >= 29;
        if (Build.VERSION.SDK_INT >= 34 && "honor".equalsIgnoreCase(Build.BRAND)
                && hasBuiltInDisplayMethod()) {
            applyToPhysicalDisplays = false;
        }

        if (applyToPhysicalDisplays) {
            boolean useDisplayControl = Build.VERSION.SDK_INT >= 34
                    && !hasPhysicalDisplayIdsMethod();
            long[] ids = useDisplayControl
                    ? DisplayControlBridge.getPhysicalDisplayIds()
                    : getPhysicalDisplayIds();
            if (ids != null && ids.length > 0) {
                boolean allOk = true;
                for (long id : ids) {
                    IBinder token = useDisplayControl
                            ? DisplayControlBridge.getPhysicalDisplayToken(id)
                            : getPhysicalDisplayToken(id);
                    allOk &= setDisplayPowerMode(token, mode);
                }
                return allOk;
            }
        }

        IBinder token = getBuiltInDisplay();
        if (token == null) {
            Log.e("No built-in display token for setDisplayPower display_id=" + displayId);
            return false;
        }
        return setDisplayPowerMode(token, mode);
    }

    private static Class<?> initClass() {
        try {
            return Class.forName("android.view.SurfaceControl");
        } catch (ClassNotFoundException e) {
            throw new AssertionError(e);
        }
    }

    private static boolean hasBuiltInDisplayMethod() {
        try {
            getBuiltInDisplayMethod();
            return true;
        } catch (NoSuchMethodException e) {
            return false;
        }
    }

    private static boolean hasPhysicalDisplayIdsMethod() {
        try {
            CLASS.getMethod("getPhysicalDisplayIds");
            return true;
        } catch (NoSuchMethodException e) {
            return false;
        }
    }

    private static IBinder getBuiltInDisplay() {
        try {
            Method method = getBuiltInDisplayMethod();
            if (Build.VERSION.SDK_INT < 29) {
                return (IBinder) method.invoke(null, 0);
            }
            return (IBinder) method.invoke(null);
        } catch (Throwable e) {
            Log.e("SurfaceControl.getBuiltInDisplay failed", e);
            return null;
        }
    }

    private static Method getBuiltInDisplayMethod() throws NoSuchMethodException {
        if (Build.VERSION.SDK_INT < 29) {
            return CLASS.getMethod("getBuiltInDisplay", int.class);
        }
        return CLASS.getMethod("getInternalDisplayToken");
    }

    private static long[] getPhysicalDisplayIds() {
        try {
            Method method = CLASS.getMethod("getPhysicalDisplayIds");
            return (long[]) method.invoke(null);
        } catch (Throwable e) {
            Log.e("SurfaceControl.getPhysicalDisplayIds failed", e);
            return null;
        }
    }

    private static IBinder getPhysicalDisplayToken(long physicalDisplayId) {
        try {
            Method method = CLASS.getMethod("getPhysicalDisplayToken", long.class);
            return (IBinder) method.invoke(null, physicalDisplayId);
        } catch (Throwable e) {
            Log.e("SurfaceControl.getPhysicalDisplayToken failed", e);
            return null;
        }
    }

    private static boolean setDisplayPowerMode(IBinder token, int mode) {
        if (token == null) {
            return false;
        }
        try {
            Method method = CLASS.getMethod("setDisplayPowerMode", IBinder.class, int.class);
            method.invoke(null, token, mode);
            return true;
        } catch (Throwable e) {
            Log.e("SurfaceControl.setDisplayPowerMode failed", e);
            return false;
        }
    }
}
