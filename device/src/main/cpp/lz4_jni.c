#include <jni.h>
#include <stddef.h>

#include "lz4.h"

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
