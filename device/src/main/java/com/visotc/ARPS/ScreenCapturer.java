package com.visotc.ARPS;

import android.annotation.SuppressLint;
import android.graphics.Bitmap;
import android.graphics.ColorSpace;
import android.graphics.PixelFormat;
import android.graphics.Rect;
import android.hardware.HardwareBuffer;
import android.media.Image;
import android.media.ImageReader;
import android.os.Build;
import android.os.IBinder;
import android.os.SystemClock;
import android.view.Surface;

import java.lang.reflect.Constructor;
import java.lang.reflect.Method;
import java.nio.ByteBuffer;
import java.util.Locale;

@SuppressLint({"PrivateApi", "SoonBlockedPrivateApi", "BlockedPrivateApi"})
final class ScreenCapturer implements AutoCloseable {
    private final DisplayReader displayReader = new DisplayReader();
    private final CaptureMode mode;
    private final BitmapCaptureBackend bitmapBackend = new BitmapCaptureBackend();
    private SurfaceCaptureBackend surfaceBackend;
    private boolean surfaceDisabled;

    ScreenCapturer(CaptureMode mode) {
        this.mode = mode;
    }

    CapturedFrame capture(int displayId) throws Exception {
        DisplayInfoSnapshot display = displayReader.read(displayId);
        if (display.width <= 0 || display.height <= 0) {
            throw new IllegalStateException("Invalid display size "
                    + display.width + "x" + display.height);
        }

        if (mode != CaptureMode.BITMAP && !surfaceDisabled) {
            try {
                if (surfaceBackend == null || !surfaceBackend.matches(display)) {
                    closeSurfaceBackend();
                    surfaceBackend = new SurfaceCaptureBackend(display);
                    Log.i("Capture backend: ImageReader surface "
                            + display.width + "x" + display.height
                            + " layerStack=" + display.layerStack);
                }
                return surfaceBackend.capture(display);
            } catch (Throwable e) {
                closeSurfaceBackend();
                surfaceDisabled = true;
                Log.e("ImageReader surface capture failed; falling back to bitmap capture", e);
                if (mode == CaptureMode.SURFACE) {
                    throw e;
                }
            }
        }

        return bitmapBackend.capture(display);
    }

    @Override
    public void close() {
        closeSurfaceBackend();
    }

    private void closeSurfaceBackend() {
        if (surfaceBackend != null) {
            surfaceBackend.close();
            surfaceBackend = null;
        }
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

        private CaptureResult captureBitmap(int width, int height) throws Exception {
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

            try {
                Bitmap hardwareBitmap = Bitmap.wrapHardwareBuffer(hardwareBuffer, colorSpace);
                if (hardwareBitmap == null) {
                    throw new IllegalStateException("Bitmap.wrapHardwareBuffer returned null");
                }
                try {
                    Bitmap cpuBitmap = hardwareBitmap.copy(Bitmap.Config.ARGB_8888, false);
                    if (cpuBitmap == null) {
                        throw new IllegalStateException("copy from hardware bitmap returned null");
                    }
                    return new CaptureResult(cpuBitmap, colorSpaceToProtocol(colorSpace));
                } finally {
                    hardwareBitmap.recycle();
                }
            } finally {
                hardwareBuffer.close();
            }
        }
    }

    private static final class SurfaceCaptureBackend {
        private static final int MAX_IMAGES = 2;
        private final int width;
        private final int height;
        private final int rotation;
        private final int layerStack;
        private final ImageReader reader;
        private final Surface surface;
        private final IBinder displayToken;

        SurfaceCaptureBackend(DisplayInfoSnapshot display) throws Exception {
            width = display.width;
            height = display.height;
            rotation = display.rotation;
            layerStack = display.layerStack;
            reader = ImageReader.newInstance(width, height, PixelFormat.RGBA_8888, MAX_IMAGES);
            surface = reader.getSurface();
            displayToken = SurfaceControlBridge.createDisplay("arps-imagereader");
            Rect rect = new Rect(0, 0, width, height);
            SurfaceControlBridge.setDisplaySurface(displayToken, surface, rect, rect, layerStack);
        }

        boolean matches(DisplayInfoSnapshot display) {
            return display.width == width
                    && display.height == height
                    && display.rotation == rotation
                    && display.layerStack == layerStack;
        }

        CapturedFrame capture(DisplayInfoSnapshot display) throws Exception {
            long acquireStartNs = SystemClock.elapsedRealtimeNanos();
            Image image = acquireLatestImage(120);
            long acquireEndNs = SystemClock.elapsedRealtimeNanos();
            if (image == null) {
                throw new IllegalStateException("ImageReader produced no image");
            }
            try {
                Image.Plane[] planes = image.getPlanes();
                if (planes == null || planes.length == 0) {
                    throw new IllegalStateException("ImageReader image has no planes");
                }
                Image.Plane plane = planes[0];
                int pixelStride = plane.getPixelStride();
                int srcRowStride = plane.getRowStride();
                if (pixelStride < 4 || srcRowStride < width * pixelStride) {
                    throw new IllegalStateException("Unsupported ImageReader plane stride: pixel="
                            + pixelStride + " row=" + srcRowStride);
                }

                int rowBytes = width * 4;
                byte[] raw = new byte[rowBytes * height];
                ByteBuffer buffer = plane.getBuffer();
                long copyStartNs = SystemClock.elapsedRealtimeNanos();
                copyPlane(buffer, raw, width, height, pixelStride, srcRowStride, rowBytes);
                long copyEndNs = SystemClock.elapsedRealtimeNanos();
                return new CapturedFrame(raw, width, height, rowBytes, display.rotation,
                        0, acquireStartNs, nanosToMillis(acquireEndNs - acquireStartNs),
                        nanosToMillis(copyEndNs - copyStartNs),
                        "android.media.ImageReader+SurfaceControl");
            } finally {
                image.close();
            }
        }

        void close() {
            SurfaceControlBridge.destroyDisplay(displayToken);
            surface.release();
            reader.close();
        }

        private Image acquireLatestImage(long timeoutMs) {
            long deadline = SystemClock.uptimeMillis() + timeoutMs;
            Image image;
            do {
                image = reader.acquireLatestImage();
                if (image != null) {
                    return image;
                }
                SystemClock.sleep(2);
            } while (SystemClock.uptimeMillis() < deadline);
            return null;
        }

        private static void copyPlane(ByteBuffer src, byte[] dst, int width, int height,
                int pixelStride, int srcRowStride, int dstRowStride) {
            byte[] pixel = pixelStride == 4 ? null : new byte[pixelStride];
            for (int y = 0; y < height; y++) {
                int srcRow = y * srcRowStride;
                int dstRow = y * dstRowStride;
                if (pixelStride == 4) {
                    ByteBuffer row = src.duplicate();
                    row.position(srcRow);
                    row.get(dst, dstRow, dstRowStride);
                } else {
                    for (int x = 0; x < width; x++) {
                        src.position(srcRow + x * pixelStride);
                        src.get(pixel, 0, pixelStride);
                        int out = dstRow + x * 4;
                        dst[out] = pixel[0];
                        dst[out + 1] = pixel[1];
                        dst[out + 2] = pixel[2];
                        dst[out + 3] = pixel[3];
                    }
                }
            }
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
}
