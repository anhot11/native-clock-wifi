#!/data/data/com.termux/files/usr/bin/bash
set -e

echo "[1/5] Compiling modular native C code with clang (-O3 -flto -s)..."
clang -shared -fPIC -O3 -flto -fvisibility=hidden -ffunction-sections -fdata-sections \
  -Wl,--gc-sections -Wall \
  app/src/main/cpp/main.c \
  app/src/main/cpp/ui.c \
  app/src/main/cpp/render.c \
  app/src/main/cpp/text.c \
  app/src/main/cpp/theme.c \
  app/src/main/cpp/sys_cpu.c \
  app/src/main/cpp/sys_mem.c \
  app/src/main/cpp/sys_net.c \
  app/src/main/cpp/sys_batt.c \
  app/src/main/cpp/sys_bridge.c \
  app/src/main/cpp/bench.c \
  -Iapp/src/main/cpp \
  -landroid -llog -lm \
  -o libnative-app.so

strip -s libnative-app.so

echo "[2/5] Compiling Shizuku/ADB privileged bridge daemon..."
mkdir -p build
clang -O3 -Wall tools/monitor_bridge.c -o build/monitor_bridge
strip -s build/monitor_bridge

echo "[3/5] Packaging APK with aapt..."
mkdir -p lib/arm64-v8a
cp libnative-app.so lib/arm64-v8a/
rm -f base.apk app-debug.apk
aapt package -F base.apk -M app/src/main/AndroidManifest.xml -S app/src/main/res -I /system/framework/framework-res.apk -f
aapt add base.apk lib/arm64-v8a/libnative-app.so

echo "[4/5] Signing APK with apksigner..."
if [ ! -f debug.keystore ]; then
  keytool -genkey -v -keystore debug.keystore -storepass android -alias androiddebugkey -keypass android -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Android Debug,O=Android,C=US"
fi
apksigner sign --ks debug.keystore --ks-pass pass:android --key-pass pass:android --out app-debug.apk base.apk

echo "[5/5] Verifying APK size and deploying via ADB..."
ls -lh libnative-app.so build/monitor_bridge app-debug.apk

if adb devices | grep -q "device$"; then
  adb push build/monitor_bridge /data/local/tmp/monitor_bridge
  adb shell "chmod 755 /data/local/tmp/monitor_bridge; killall monitor_bridge 2>/dev/null; nohup /data/local/tmp/monitor_bridge </dev/null >/dev/null 2>&1 &"
  adb install -r app-debug.apk
  adb shell am force-stop com.example.nativec
  adb shell am start -n com.example.nativec/android.app.NativeActivity
  echo "App + Shizuku/ADB Bridge successfully installed and launched!"
else
  echo "APK successfully built and signed at app-debug.apk!"
fi
