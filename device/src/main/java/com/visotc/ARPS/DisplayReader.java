package com.visotc.ARPS;

import android.graphics.Point;
import android.os.IInterface;

import java.lang.reflect.Field;
import java.lang.reflect.Method;

final class DisplayReader {
    private final IInterface windowManager;
    private final IInterface displayManager;

    DisplayReader() {
        windowManager = SystemServices.getInterface("window", "android.view.IWindowManager");
        displayManager = SystemServices.getInterface("display", "android.hardware.display.IDisplayManager");
    }

    DisplayInfoSnapshot read(int displayId) {
        DisplayInfoSnapshot fromDisplayManager = readFromDisplayManager(displayId);
        if (fromDisplayManager != null) {
            return fromDisplayManager;
        }

        DisplayInfoSnapshot fromWindowManager = readFromWindowManager(displayId);
        if (fromWindowManager != null) {
            return fromWindowManager;
        }

        throw new IllegalStateException("Unable to read display info for display_id=" + displayId);
    }

    private DisplayInfoSnapshot readFromDisplayManager(int displayId) {
        try {
            Method method = displayManager.getClass().getMethod("getDisplayInfo", int.class);
            Object displayInfo = method.invoke(displayManager, displayId);
            if (displayInfo == null) {
                return null;
            }

            int width = readIntField(displayInfo, "logicalWidth", "appWidth");
            int height = readIntField(displayInfo, "logicalHeight", "appHeight");
            int rotation = readIntField(displayInfo, "rotation");
            int layerStack = readOptionalIntField(displayInfo, 0, "layerStack");
            if (width > 0 && height > 0) {
                return new DisplayInfoSnapshot(width, height, rotation, layerStack);
            }
        } catch (Throwable e) {
            Log.e("IDisplayManager.getDisplayInfo failed", e);
        }
        return null;
    }

    private DisplayInfoSnapshot readFromWindowManager(int displayId) {
        try {
            Point size = new Point();
            Method sizeMethod = findMethod(windowManager.getClass(),
                    new String[]{"getBaseDisplaySize", "getInitialDisplaySize"},
                    int.class, Point.class);
            sizeMethod.invoke(windowManager, displayId, size);
            int rotation = readRotation();
            int width = size.x;
            int height = size.y;
            if (rotation == 1 || rotation == 3) {
                width = size.y;
                height = size.x;
            }
            if (width > 0 && height > 0) {
                return new DisplayInfoSnapshot(width, height, rotation, 0);
            }
        } catch (Throwable e) {
            Log.e("IWindowManager display size failed", e);
        }
        return null;
    }

    private int readRotation() throws Exception {
        Method method = findMethod(windowManager.getClass(),
                new String[]{"getDefaultDisplayRotation", "getRotation"});
        return (Integer) method.invoke(windowManager);
    }

    private static Method findMethod(Class<?> clazz, String[] names, Class<?>... parameterTypes)
            throws NoSuchMethodException {
        for (String name : names) {
            try {
                return clazz.getMethod(name, parameterTypes);
            } catch (NoSuchMethodException ignored) {
                // Try the next known name.
            }
        }
        throw new NoSuchMethodException(names[0]);
    }

    private static int readIntField(Object target, String... names) throws Exception {
        Class<?> clazz = target.getClass();
        for (String name : names) {
            try {
                Field field = clazz.getField(name);
                return field.getInt(target);
            } catch (NoSuchFieldException ignored) {
                // Try the next known field name.
            }
        }
        throw new NoSuchFieldException(names[0]);
    }

    private static int readOptionalIntField(Object target, int fallback, String... names) {
        try {
            return readIntField(target, names);
        } catch (Exception ignored) {
            return fallback;
        }
    }
}
