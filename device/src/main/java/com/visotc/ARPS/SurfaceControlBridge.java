package com.visotc.ARPS;

import android.annotation.SuppressLint;
import android.os.Build;
import android.os.IBinder;

import java.lang.reflect.Method;

@SuppressLint({"PrivateApi", "SoonBlockedPrivateApi", "BlockedPrivateApi"})
final class SurfaceControlBridge {
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

    private static Class<?> initClass() {
        try {
            return Class.forName("android.view.SurfaceControl");
        } catch (ClassNotFoundException e) {
            throw new AssertionError(e);
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
}
