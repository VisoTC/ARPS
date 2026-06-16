# ARPS mini receiver

Minimal host-side receiver for Android client debugging. It only uses Python
standard library: listen, read `HELLO`, send `START`, wait for `READY`, print
frame metadata, then close the socket so the device process exits. In pull mode
it sends one `FRAME_REQUEST` before each expected frame.

```bash
python3 host-debug/minirecv/arps_minirecv.py --port 27183 --frames 1 --compression lz4_block --stream-mode pull
adb reverse tcp:27183 tcp:27183
adb push device/build/outputs/apk/debug/arps-device.apk /data/local/tmp/arps-device.apk
adb shell CLASSPATH=/data/local/tmp/arps-device.apk app_process / com.visotc.ARPS.Main --connect-port=27183
```

For raw payload inspection:

```bash
python3 host-debug/minirecv/arps_minirecv.py --port 27183 --frames 1 --compression raw --stream-mode pull --save-payload frame.rgba
```
