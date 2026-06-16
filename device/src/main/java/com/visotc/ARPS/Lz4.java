package com.visotc.ARPS;

import android.os.Build;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.Arrays;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

final class Lz4 {
    private static final String LIB_NAME = "liblz4arps.so";
    private static final File TEMP_DIR = new File("/data/local/tmp");
    private static boolean loaded;

    private Lz4() {
    }

    static synchronized void load() throws IOException {
        if (loaded) {
            return;
        }

        String classpath = System.getenv("CLASSPATH");
        if (classpath == null || classpath.length() == 0) {
            throw new IOException("CLASSPATH is empty; cannot locate arps-device.apk");
        }

        IOException lastError = null;
        String[] entries = classpath.split(File.pathSeparator);
        for (String entry : entries) {
            if (entry.length() == 0) {
                continue;
            }
            File apk = new File(entry);
            if (!apk.isFile()) {
                continue;
            }
            try {
                extractAndLoad(apk);
                loaded = true;
                return;
            } catch (IOException e) {
                lastError = e;
            }
        }

        if (lastError != null) {
            throw lastError;
        }
        throw new IOException("No apk file found in CLASSPATH=" + classpath);
    }

    static byte[] compress(byte[] src, int srcLen) {
        int bound = nativeCompressBound(srcLen);
        if (bound <= 0) {
            throw new IllegalArgumentException("Invalid LZ4 compress bound for len=" + srcLen);
        }
        byte[] dst = new byte[bound];
        int written = nativeCompress(src, srcLen, dst, dst.length);
        if (written <= 0) {
            throw new IllegalStateException("LZ4 compression failed for len=" + srcLen);
        }
        return Arrays.copyOf(dst, written);
    }

    private static void extractAndLoad(File apk) throws IOException {
        try (ZipFile zip = new ZipFile(apk)) {
            String abi = findSupportedAbi(zip);
            if (abi == null) {
                throw new IOException("No " + LIB_NAME + " for supported ABIs "
                        + Arrays.toString(Build.SUPPORTED_ABIS));
            }

            ZipEntry entry = zip.getEntry("lib/" + abi + "/" + LIB_NAME);
            File output = File.createTempFile("arps-" + abi + "-", "-" + LIB_NAME, TEMP_DIR);
            output.deleteOnExit();
            restrictOwnerAccess(output);
            try (InputStream in = zip.getInputStream(entry);
                    FileOutputStream out = new FileOutputStream(output, false)) {
                byte[] buffer = new byte[64 * 1024];
                int read;
                while ((read = in.read(buffer)) != -1) {
                    out.write(buffer, 0, read);
                }
                out.getFD().sync();
            }
            output.setWritable(false, true);
            try {
                System.load(output.getAbsolutePath());
            } finally {
                if (output.exists() && !output.delete()) {
                    Log.i("Temporary native library remains until process exit: "
                            + output.getAbsolutePath());
                }
            }
        }
    }

    private static void restrictOwnerAccess(File file) {
        file.setReadable(false, false);
        file.setWritable(false, false);
        file.setExecutable(false, false);
        file.setReadable(true, true);
        file.setWritable(true, true);
    }

    private static String findSupportedAbi(ZipFile zip) {
        for (String abi : Build.SUPPORTED_ABIS) {
            if (zip.getEntry("lib/" + abi + "/" + LIB_NAME) != null) {
                return abi;
            }
        }
        return null;
    }

    private static native int nativeCompressBound(int inputSize);

    private static native int nativeCompress(byte[] src, int srcLen, byte[] dst, int dstCapacity);
}
