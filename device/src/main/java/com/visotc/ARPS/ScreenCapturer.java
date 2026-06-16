package com.visotc.ARPS;

import android.annotation.SuppressLint;
import android.graphics.Bitmap;
import android.graphics.ColorSpace;
import android.hardware.HardwareBuffer;
import android.os.Build;
import android.os.IBinder;
import android.os.SystemClock;

import java.lang.reflect.Constructor;
import java.lang.reflect.Method;
import java.nio.ByteBuffer;
import java.util.Locale;

@SuppressLint({"PrivateApi", "SoonBlockedPrivateApi", "BlockedPrivateApi"})
final class ScreenCapturer implements AutoCloseable {
    private final DisplayReader displayReader = new DisplayReader();
    private final CaptureMode mode;
    private final BitmapCaptureBackend bitmapBackend = new BitmapCaptureBackend();
    private boolean hardwareLz4Disabled;

    ScreenCapturer(CaptureMode mode) {
        this.mode = mode;
    }

    CapturedFrame capture(int displayId) throws Exception {
        return bitmapBackend.capture(readValidDisplay(displayId));
    }

    CapturedFrame captureHardwareLz4(int displayId) throws Exception {
        if (mode == CaptureMode.BITMAP || hardwareLz4Disabled) {
            return null;
        }
        try {
            return bitmapBackend.captureHardwareLz4(readValidDisplay(displayId));
        } catch (Exception e) {
            if (mode == CaptureMode.HARDWARE) {
                throw e;
            }
            hardwareLz4Disabled = true;
            Log.e("HardwareBuffer direct LZ4 failed; falling back to bitmap capture", e);
            return null;
        }
    }

    @Override
    public void close() {
    }

    private DisplayInfoSnapshot readValidDisplay(int displayId) throws Exception {
        DisplayInfoSnapshot display = displayReader.read(displayId);
        if (display.width <= 0 || display.height <= 0) {
            throw new IllegalStateException("Invalid display size "
                    + display.width + "x" + display.height);
        }
        return display;
    }

    private static final class BitmapCaptureBackend {
        private final Class<?> screenCaptureClass;
        private final Class<?> argsClass;
        private final Class<?> builderClass;
        private final Constructor<?> builderConstructor;
        private final Method setSizeMethod;
        private final Method buildMethod;
        private final Method captureDisplayMethod;
        private Method getHardwareBufferMethod;
        private Method getColorSpaceMethod;
        private IBinder displayToken;

        BitmapCaptureBackend() {
            try {
                screenCaptureClass = screenCaptureClass();
                argsClass = displayCaptureArgsClass();
                builderClass = displayCaptureArgsBuilderClass();
                builderConstructor = builderClass.getDeclaredConstructor(IBinder.class);
                setSizeMethod = builderClass.getDeclaredMethod("setSize", int.class, int.class);
                buildMethod = builderClass.getDeclaredMethod("build");
                captureDisplayMethod = screenCaptureClass.getDeclaredMethod("captureDisplay",
                        argsClass);
            } catch (Exception e) {
                throw new IllegalStateException("Unable to initialize bitmap capture reflection",
                        e);
            }
        }

        CapturedFrame capture(DisplayInfoSnapshot display) throws Exception {
            long captureStartNs = SystemClock.elapsedRealtimeNanos();
            CaptureResult captureResult = captureBitmap(display.width, display.height);
            long captureEndNs = SystemClock.elapsedRealtimeNanos();

            Bitmap bitmap = captureResult.bitmap;
            try {
                int rowBytes = bitmap.getRowBytes();
                int byteCount = rowBytes * bitmap.getHeight();
                byte[] raw = new byte[byteCount];
                ByteBuffer buffer = ByteBuffer.wrap(raw);
                long copyStartNs = SystemClock.elapsedRealtimeNanos();
                bitmap.copyPixelsToBuffer(buffer);
                long copyEndNs = SystemClock.elapsedRealtimeNanos();

                return new CapturedFrame(raw, bitmap.getWidth(), bitmap.getHeight(),
                        rowBytes, display.rotation, captureResult.colorSpace,
                        captureStartNs, nanosToMillis(captureEndNs - captureStartNs),
                        nanosToMillis(copyEndNs - copyStartNs),
                        "android.window.ScreenCapture.captureDisplay");
            } finally {
                bitmap.recycle();
            }
        }

        CapturedFrame captureHardwareLz4(DisplayInfoSnapshot display) throws Exception {
            long captureStartNs = SystemClock.elapsedRealtimeNanos();
            HardwareCaptureResult captureResult = captureHardwareBuffer(display.width,
                    display.height);
            long captureEndNs = SystemClock.elapsedRealtimeNanos();
            try {
                Lz4.HardwareBufferCompression compressed =
                        Lz4.compressHardwareBuffer(captureResult.hardwareBuffer);
                return new CapturedFrame(compressed.payload, compressed.compressMs,
                        compressed.width, compressed.height, compressed.rowBytes,
                        compressed.uncompressedLen, display.rotation, captureResult.colorSpace,
                        captureStartNs, nanosToMillis(captureEndNs - captureStartNs),
                        compressed.lockMs, compressed.format, compressed.usage,
                        "android.window.ScreenCapture.captureDisplay+AHardwareBuffer");
            } finally {
                captureResult.hardwareBuffer.close();
            }
        }

        private CaptureResult captureBitmap(int width, int height) throws Exception {
            HardwareCaptureResult captureResult = captureHardwareBuffer(width, height);
            HardwareBuffer hardwareBuffer = captureResult.hardwareBuffer;
            try {
                Bitmap hardwareBitmap = Bitmap.wrapHardwareBuffer(hardwareBuffer,
                        captureResult.colorSpaceObject);
                if (hardwareBitmap == null) {
                    throw new IllegalStateException("Bitmap.wrapHardwareBuffer returned null");
                }
                try {
                    Bitmap cpuBitmap = hardwareBitmap.copy(Bitmap.Config.ARGB_8888, false);
                    if (cpuBitmap == null) {
                        throw new IllegalStateException("copy from hardware bitmap returned null");
                    }
                    return new CaptureResult(cpuBitmap, captureResult.colorSpace);
                } finally {
                    hardwareBitmap.recycle();
                }
            } finally {
                hardwareBuffer.close();
            }
        }

        private HardwareCaptureResult captureHardwareBuffer(int width, int height)
                throws Exception {
            if (displayToken == null) {
                displayToken = SurfaceControlBridge.getCaptureDisplayToken();
            }
            Object builder = builderConstructor.newInstance(displayToken);
            setSizeMethod.invoke(builder, width, height);
            Object args = buildMethod.invoke(builder);
            Object screenshotHardwareBuffer = captureDisplayMethod.invoke(null, args);
            if (screenshotHardwareBuffer == null) {
                throw new IllegalStateException("captureDisplay returned null");
            }

            if (getHardwareBufferMethod == null) {
                getHardwareBufferMethod = screenshotHardwareBuffer.getClass()
                        .getDeclaredMethod("getHardwareBuffer");
                getColorSpaceMethod = screenshotHardwareBuffer.getClass()
                        .getDeclaredMethod("getColorSpace");
            }

            ColorSpace colorSpace = (ColorSpace) getColorSpaceMethod.invoke(screenshotHardwareBuffer);
            HardwareBuffer hardwareBuffer = (HardwareBuffer) getHardwareBufferMethod
                    .invoke(screenshotHardwareBuffer);
            if (hardwareBuffer == null) {
                throw new IllegalStateException("Screenshot hardware buffer is null");
            }
            return new HardwareCaptureResult(hardwareBuffer, colorSpace,
                    colorSpaceToProtocol(colorSpace));
        }
    }

    private static Class<?> screenCaptureClass() throws ClassNotFoundException {
        if (Build.VERSION.SDK_INT >= 34) {
            return Class.forName("android.window.ScreenCapture");
        }
        return Class.forName("android.view.SurfaceControl");
    }

    private static Class<?> displayCaptureArgsClass() throws ClassNotFoundException {
        if (Build.VERSION.SDK_INT >= 34) {
            return Class.forName("android.window.ScreenCapture$DisplayCaptureArgs");
        }
        return Class.forName("android.view.SurfaceControl$DisplayCaptureArgs");
    }

    private static Class<?> displayCaptureArgsBuilderClass() throws ClassNotFoundException {
        if (Build.VERSION.SDK_INT >= 34) {
            return Class.forName("android.window.ScreenCapture$DisplayCaptureArgs$Builder");
        }
        return Class.forName("android.view.SurfaceControl$DisplayCaptureArgs$Builder");
    }

    private static int colorSpaceToProtocol(ColorSpace colorSpace) {
        if (colorSpace == null) {
            return 0;
        }
        if (colorSpace.isSrgb()) {
            return 1;
        }
        String name = colorSpace.getName();
        if (name != null && name.toLowerCase(Locale.US).contains("p3")) {
            return 2;
        }
        return 0;
    }

    private static double nanosToMillis(long ns) {
        return ns / 1000000.0;
    }

    private static final class CaptureResult {
        final Bitmap bitmap;
        final int colorSpace;

        CaptureResult(Bitmap bitmap, int colorSpace) {
            this.bitmap = bitmap;
            this.colorSpace = colorSpace;
        }
    }

    private static final class HardwareCaptureResult {
        final HardwareBuffer hardwareBuffer;
        final ColorSpace colorSpaceObject;
        final int colorSpace;

        HardwareCaptureResult(HardwareBuffer hardwareBuffer, ColorSpace colorSpaceObject,
                int colorSpace) {
            this.hardwareBuffer = hardwareBuffer;
            this.colorSpaceObject = colorSpaceObject;
            this.colorSpace = colorSpace;
        }
    }
}
