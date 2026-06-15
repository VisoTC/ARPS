package com.visotc.ARPS;

import android.annotation.SuppressLint;
import android.os.IBinder;
import android.system.Os;

import java.lang.reflect.Method;

@SuppressLint({"PrivateApi", "SoonBlockedPrivateApi", "BlockedPrivateApi"})
final class DisplayControlBridge {
    private static final Class<?> CLASS = initClass();

    private DisplayControlBridge() {
    }

    static boolean isAvailable() {
        return CLASS != null;
    }

    static long[] getPhysicalDisplayIds() {
        if (CLASS == null) {
            return null;
        }
        try {
            Method method = CLASS.getMethod("getPhysicalDisplayIds");
            return (long[]) method.invoke(null);
        } catch (Throwable e) {
            Log.e("DisplayControl.getPhysicalDisplayIds failed", e);
            return null;
        }
    }

    static IBinder getPhysicalDisplayToken(long physicalDisplayId) {
        if (CLASS == null) {
            return null;
        }
        try {
            Method method = CLASS.getMethod("getPhysicalDisplayToken", long.class);
            return (IBinder) method.invoke(null, physicalDisplayId);
        } catch (Throwable e) {
            Log.e("DisplayControl.getPhysicalDisplayToken failed", e);
            return null;
        }
    }

    private static Class<?> initClass() {
        try {
            Class<?> factoryClass = Class.forName("com.android.internal.os.ClassLoaderFactory");
            Method createClassLoader = factoryClass.getDeclaredMethod("createClassLoader",
                    String.class, String.class, String.class, ClassLoader.class, int.class,
                    boolean.class, String.class);
            String systemServerClasspath = Os.getenv("SYSTEMSERVERCLASSPATH");
            ClassLoader classLoader = (ClassLoader) createClassLoader.invoke(null,
                    systemServerClasspath, null, null, ClassLoader.getSystemClassLoader(), 0,
                    true, null);

            Class<?> displayControlClass =
                    classLoader.loadClass("com.android.server.display.DisplayControl");
            Method loadLibrary0 = Runtime.class.getDeclaredMethod("loadLibrary0",
                    Class.class, String.class);
            loadLibrary0.setAccessible(true);
            loadLibrary0.invoke(Runtime.getRuntime(), displayControlClass, "android_servers");
            return displayControlClass;
        } catch (Throwable e) {
            Log.e("DisplayControl unavailable", e);
            return null;
        }
    }
}
