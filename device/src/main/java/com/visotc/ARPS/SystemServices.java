package com.visotc.ARPS;

import android.annotation.SuppressLint;
import android.os.IBinder;
import android.os.IInterface;

import java.lang.reflect.Method;

@SuppressLint({"PrivateApi", "DiscouragedPrivateApi"})
final class SystemServices {
    private static final Method GET_SERVICE = getServiceMethod();

    private SystemServices() {
    }

    static IInterface getInterface(String serviceName, String interfaceName) {
        try {
            IBinder binder = (IBinder) GET_SERVICE.invoke(null, serviceName);
            if (binder == null) {
                throw new IllegalStateException("Service not found: " + serviceName);
            }
            Class<?> stubClass = Class.forName(interfaceName + "$Stub");
            Method asInterface = stubClass.getMethod("asInterface", IBinder.class);
            return (IInterface) asInterface.invoke(null, binder);
        } catch (Throwable e) {
            throw new IllegalStateException("Unable to load service " + serviceName
                    + " as " + interfaceName, e);
        }
    }

    private static Method getServiceMethod() {
        try {
            return Class.forName("android.os.ServiceManager")
                    .getDeclaredMethod("getService", String.class);
        } catch (Throwable e) {
            throw new AssertionError(e);
        }
    }
}
