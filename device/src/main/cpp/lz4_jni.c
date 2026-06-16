#include <jni.h>
#include <android/hardware_buffer.h>
#include <android/hardware_buffer_jni.h>
#include <stdint.h>
#include <stddef.h>
#include <time.h>

#include "lz4.h"

static int64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t) ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static int bytes_per_pixel(uint32_t format) {
    switch (format) {
        case AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM:
        case AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM:
            return 4;
        default:
            return 0;
    }
}

static void fill_metrics(JNIEnv *env, jlongArray metrics,
        const AHardwareBuffer_Desc *desc, int64_t uncompressed_len,
        int lock_result, int64_t lock_ns, int64_t compress_ns, int64_t total_ns) {
    if (metrics == NULL || (*env)->GetArrayLength(env, metrics) < 10) {
        return;
    }
    jlong values[10];
    values[0] = desc != NULL ? (jlong) desc->width : -1;
    values[1] = desc != NULL ? (jlong) desc->height : -1;
    values[2] = desc != NULL ? (jlong) desc->format : -1;
    values[3] = desc != NULL ? (jlong) desc->stride : -1;
    values[4] = desc != NULL ? (jlong) desc->usage : -1;
    values[5] = (jlong) uncompressed_len;
    values[6] = (jlong) lock_ns;
    values[7] = (jlong) compress_ns;
    values[8] = (jlong) total_ns;
    values[9] = (jlong) lock_result;
    (*env)->SetLongArrayRegion(env, metrics, 0, 10, values);
}

JNIEXPORT jint JNICALL
Java_com_visotc_ARPS_Lz4_nativeCompressBound(JNIEnv *env, jclass clazz, jint input_size) {
    (void) env;
    (void) clazz;

    if (input_size < 0) {
        return 0;
    }
    return LZ4_compressBound(input_size);
}

JNIEXPORT jint JNICALL
Java_com_visotc_ARPS_Lz4_nativeCompress(JNIEnv *env, jclass clazz, jbyteArray src,
        jint src_len, jbyteArray dst, jint dst_capacity) {
    (void) clazz;

    if (src == NULL || dst == NULL || src_len < 0 || dst_capacity <= 0) {
        return 0;
    }

    jsize src_array_len = (*env)->GetArrayLength(env, src);
    jsize dst_array_len = (*env)->GetArrayLength(env, dst);
    if (src_len > src_array_len || dst_capacity > dst_array_len) {
        return 0;
    }

    jboolean src_copy = JNI_FALSE;
    jboolean dst_copy = JNI_FALSE;
    jbyte *src_ptr = (*env)->GetByteArrayElements(env, src, &src_copy);
    if (src_ptr == NULL) {
        return 0;
    }

    jbyte *dst_ptr = (*env)->GetByteArrayElements(env, dst, &dst_copy);
    if (dst_ptr == NULL) {
        (*env)->ReleaseByteArrayElements(env, src, src_ptr, JNI_ABORT);
        return 0;
    }

    int written = LZ4_compress_default((const char *) src_ptr, (char *) dst_ptr,
            src_len, dst_capacity);

    (*env)->ReleaseByteArrayElements(env, src, src_ptr, JNI_ABORT);
    (*env)->ReleaseByteArrayElements(env, dst, dst_ptr, written > 0 ? 0 : JNI_ABORT);
    return written;
}

JNIEXPORT jint JNICALL
Java_com_visotc_ARPS_Lz4_nativeHardwareBufferInfo(JNIEnv *env, jclass clazz,
        jobject hardware_buffer, jlongArray metrics) {
    (void) clazz;

    if (hardware_buffer == NULL) {
        return 0;
    }
    AHardwareBuffer *buffer = AHardwareBuffer_fromHardwareBuffer(env, hardware_buffer);
    if (buffer == NULL) {
        return 0;
    }

    AHardwareBuffer_Desc desc;
    AHardwareBuffer_describe(buffer, &desc);
    int bpp = bytes_per_pixel(desc.format);
    if (bpp <= 0 || desc.width == 0 || desc.height == 0 || desc.layers != 1) {
        fill_metrics(env, metrics, &desc, -1, -1, 0, 0, 0);
        return 0;
    }
    int64_t row_bytes = (int64_t) desc.stride * bpp;
    int64_t uncompressed_len = row_bytes * desc.height;
    if (uncompressed_len <= 0 || uncompressed_len > INT32_MAX) {
        fill_metrics(env, metrics, &desc, uncompressed_len, -1, 0, 0, 0);
        return 0;
    }
    fill_metrics(env, metrics, &desc, uncompressed_len, 0, 0, 0, 0);
    return (jint) uncompressed_len;
}

JNIEXPORT jint JNICALL
Java_com_visotc_ARPS_Lz4_nativeCompressHardwareBuffer(JNIEnv *env, jclass clazz,
        jobject hardware_buffer, jbyteArray dst, jint dst_capacity, jlongArray metrics) {
    (void) clazz;

    int64_t total_start = now_ns();
    if (hardware_buffer == NULL || dst == NULL || dst_capacity <= 0) {
        return 0;
    }

    AHardwareBuffer *buffer = AHardwareBuffer_fromHardwareBuffer(env, hardware_buffer);
    if (buffer == NULL) {
        return 0;
    }

    AHardwareBuffer_Desc desc;
    AHardwareBuffer_describe(buffer, &desc);
    int bpp = bytes_per_pixel(desc.format);
    if (bpp <= 0 || desc.width == 0 || desc.height == 0 || desc.layers != 1) {
        fill_metrics(env, metrics, &desc, -1, -1, 0, 0, now_ns() - total_start);
        return 0;
    }

    int64_t row_bytes = (int64_t) desc.stride * bpp;
    int64_t uncompressed_len = row_bytes * desc.height;
    if (uncompressed_len <= 0 || uncompressed_len > INT32_MAX) {
        fill_metrics(env, metrics, &desc, uncompressed_len, -1, 0, 0,
                now_ns() - total_start);
        return 0;
    }

    jsize dst_array_len = (*env)->GetArrayLength(env, dst);
    if (dst_capacity > dst_array_len) {
        fill_metrics(env, metrics, &desc, uncompressed_len, -1, 0, 0,
                now_ns() - total_start);
        return 0;
    }

    void *source = NULL;
    int64_t lock_start = now_ns();
    int lock_result = AHardwareBuffer_lock(buffer, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN,
            -1, NULL, &source);
    int64_t lock_ns = now_ns() - lock_start;
    if (lock_result != 0 || source == NULL) {
        fill_metrics(env, metrics, &desc, uncompressed_len, lock_result, lock_ns, 0,
                now_ns() - total_start);
        return 0;
    }

    jbyte *dst_ptr = (*env)->GetByteArrayElements(env, dst, NULL);
    if (dst_ptr == NULL) {
        AHardwareBuffer_unlock(buffer, NULL);
        fill_metrics(env, metrics, &desc, uncompressed_len, lock_result, lock_ns, 0,
                now_ns() - total_start);
        return 0;
    }

    int64_t compress_start = now_ns();
    int written = LZ4_compress_default((const char *) source, (char *) dst_ptr,
            (int) uncompressed_len, dst_capacity);
    int64_t compress_ns = now_ns() - compress_start;

    AHardwareBuffer_unlock(buffer, NULL);
    (*env)->ReleaseByteArrayElements(env, dst, dst_ptr, written > 0 ? 0 : JNI_ABORT);
    fill_metrics(env, metrics, &desc, uncompressed_len, lock_result, lock_ns, compress_ns,
            now_ns() - total_start);
    return written;
}
