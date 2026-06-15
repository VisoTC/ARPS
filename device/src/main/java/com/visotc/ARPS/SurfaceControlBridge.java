package com.visotc.ARPS;

import android.annotation.SuppressLint;
import android.graphics.Rect;
import android.os.Build;
import android.os.IBinder;
import android.view.Surface;

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

    static IBinder createDisplay(String name) throws Exception {
        Method method = CLASS.getMethod("createDisplay", String.class, boolean.class);
        return (IBinder) method.invoke(null, name, false);
    }

    static void destroyDisplay(IBinder displayToken) {
        if (displayToken == null) {
            return;
        }
        try {
            Method method = CLASS.getMethod("destroyDisplay", IBinder.class);
            method.invoke(null, displayToken);
        } catch (Throwable e) {
            Log.e("SurfaceControl.destroyDisplay failed", e);
        }
    }

    static void setDisplaySurface(IBinder displayToken, Surface surface,
            Rect deviceRect, Rect displayRect, int layerStack) throws Exception {
        Method openTransaction = CLASS.getMethod("openTransaction");
        Method closeTransaction = CLASS.getMethod("closeTransaction");
        Method setDisplaySurface = CLASS.getMethod("setDisplaySurface",
                IBinder.class, Surface.class);
        Method setDisplayProjection = CLASS.getMethod("setDisplayProjection",
                IBinder.class, int.class, Rect.class, Rect.class);
        Method setDisplayLayerStack = CLASS.getMethod("setDisplayLayerStack",
                IBinder.class, int.class);
        openTransaction.invoke(null);
        try {
            setDisplaySurface.invoke(null, displayToken, surface);
            setDisplayProjection.invoke(null, displayToken, 0, deviceRect, displayRect);
            setDisplayLayerStack.invoke(null, displayToken, layerStack);
        } finally {
            closeTransaction.invoke(null);
        }
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
