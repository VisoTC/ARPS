package com.visotc.ARPS;

import android.os.Build;
import android.os.Binder;
import android.os.IBinder;
import android.os.IInterface;
import android.os.PowerManager;
import android.os.SystemClock;

import java.lang.reflect.Method;

final class PowerController {
    private static final String WAKE_LOCK_TAG = "ARPS:keep_screen_on";
    private static final String PACKAGE_NAME = "com.android.shell";

    private final IInterface powerManager;
    private final IBinder wakeLockToken = new Binder();
    private InputController inputController;
    private Method isScreenOnMethod;
    private Method acquireWakeLockMethod;
    private Method releaseWakeLockMethod;
    private boolean wakeLockHeld;

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

    synchronized void acquireWakeLock(int displayId) throws Exception {
        if (wakeLockHeld) {
            return;
        }
        Method method = getAcquireWakeLockMethod();
        Object[] args = buildAcquireWakeLockArgs(method, displayId);
        method.invoke(powerManager, args);
        wakeLockHeld = true;
        Log.i("acquireWakeLock(display_id=" + displayId + ") ok");
    }

    synchronized void releaseWakeLock() {
        if (!wakeLockHeld) {
            return;
        }
        try {
            Method method = getReleaseWakeLockMethod();
            method.invoke(powerManager, buildReleaseWakeLockArgs(method));
            Log.i("releaseWakeLock ok");
        } catch (Throwable e) {
            Log.e("releaseWakeLock failed", e);
        } finally {
            wakeLockHeld = false;
        }
    }

    synchronized boolean wakeLockHeld() {
        return wakeLockHeld;
    }

    StateSnapshot snapshot(int displayId) {
        boolean screenOn = isScreenOn(displayId);
        synchronized (this) {
            return new StateSnapshot(screenOn, wakeLockHeld);
        }
    }

    void applyExitMode(int displayId, boolean previousScreenOn, ExitPowerMode mode) {
        boolean screenOn;
        switch (mode) {
            case KEEP_ON:
                screenOn = true;
                break;
            case TURN_OFF:
                screenOn = false;
                break;
            case RESTORE_PREVIOUS:
            default:
                screenOn = previousScreenOn;
                break;
        }
        if (isScreenOn(displayId) != screenOn) {
            pressPower(displayId);
            SystemClock.sleep(300);
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

    private Method getAcquireWakeLockMethod() throws NoSuchMethodException {
        if (acquireWakeLockMethod == null) {
            for (Method method : powerManager.getClass().getMethods()) {
                if ("acquireWakeLock".equals(method.getName())
                        && hasParameter(method, IBinder.class)
                        && hasParameter(method, int.class)
                        && hasParameter(method, String.class)) {
                    acquireWakeLockMethod = method;
                    break;
                }
            }
            if (acquireWakeLockMethod == null) {
                throw new NoSuchMethodException("IPowerManager.acquireWakeLock");
            }
        }
        return acquireWakeLockMethod;
    }

    private Method getReleaseWakeLockMethod() throws NoSuchMethodException {
        if (releaseWakeLockMethod == null) {
            for (Method method : powerManager.getClass().getMethods()) {
                if ("releaseWakeLock".equals(method.getName())
                        && hasParameter(method, IBinder.class)) {
                    releaseWakeLockMethod = method;
                    break;
                }
            }
            if (releaseWakeLockMethod == null) {
                throw new NoSuchMethodException("IPowerManager.releaseWakeLock");
            }
        }
        return releaseWakeLockMethod;
    }

    private Object[] buildAcquireWakeLockArgs(Method method, int displayId) {
        Class<?>[] types = method.getParameterTypes();
        Object[] args = new Object[types.length];
        boolean usedFlags = false;
        boolean usedTag = false;
        boolean usedPackageName = false;
        for (int i = 0; i < types.length; i++) {
            Class<?> type = types[i];
            if (IBinder.class.isAssignableFrom(type)) {
                args[i] = wakeLockToken;
            } else if (type == int.class || type == Integer.TYPE) {
                if (!usedFlags) {
                    args[i] = PowerManager.SCREEN_BRIGHT_WAKE_LOCK;
                    usedFlags = true;
                } else {
                    args[i] = displayId;
                }
            } else if (type == String.class) {
                if (!usedTag) {
                    args[i] = WAKE_LOCK_TAG;
                    usedTag = true;
                } else if (!usedPackageName) {
                    args[i] = PACKAGE_NAME;
                    usedPackageName = true;
                } else {
                    args[i] = null;
                }
            } else if (type == boolean.class || type == Boolean.TYPE) {
                args[i] = false;
            } else if (type == long.class || type == Long.TYPE) {
                args[i] = 0L;
            } else if (type == float.class || type == Float.TYPE) {
                args[i] = 0.0f;
            } else if (type == double.class || type == Double.TYPE) {
                args[i] = 0.0d;
            } else {
                args[i] = null;
            }
        }
        return args;
    }

    private Object[] buildReleaseWakeLockArgs(Method method) {
        Class<?>[] types = method.getParameterTypes();
        Object[] args = new Object[types.length];
        for (int i = 0; i < types.length; i++) {
            Class<?> type = types[i];
            if (IBinder.class.isAssignableFrom(type)) {
                args[i] = wakeLockToken;
            } else if (type == int.class || type == Integer.TYPE) {
                args[i] = 0;
            } else if (type == boolean.class || type == Boolean.TYPE) {
                args[i] = false;
            } else if (type == long.class || type == Long.TYPE) {
                args[i] = 0L;
            } else if (type == float.class || type == Float.TYPE) {
                args[i] = 0.0f;
            } else if (type == double.class || type == Double.TYPE) {
                args[i] = 0.0d;
            } else {
                args[i] = null;
            }
        }
        return args;
    }

    private static boolean hasParameter(Method method, Class<?> parameterType) {
        for (Class<?> type : method.getParameterTypes()) {
            if (type == parameterType || parameterType.isAssignableFrom(type)) {
                return true;
            }
        }
        return false;
    }

    static final class StateSnapshot {
        final boolean screenOn;
        final boolean wakeLockHeld;

        StateSnapshot(boolean screenOn, boolean wakeLockHeld) {
            this.screenOn = screenOn;
            this.wakeLockHeld = wakeLockHeld;
        }
    }
}
