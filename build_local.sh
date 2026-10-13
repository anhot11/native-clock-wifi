#!/data/data/com.termux/files/usr/bin/bash
set -e

echo "[1/4] Compiling modular native C code with clang (-O3 -flto -s)..."
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
  app/src/main/cpp/bench.c \
  -Iapp/src/main/cpp \
  -landroid -llog -lm \
  -o libnative-app.so

strip -s libnative-app.so

echo "[2/4] Packaging APK with aapt..."
mkdir -p lib/arm64-v8a
cp libnative-app.so lib/arm64-v8a/
rm -f base.apk app-debug.apk
aapt package -F base.apk -M app/src/main/AndroidManifest.xml -S app/src/main/res -I /system/framework/framework-res.apk -f
aapt add base.apk lib/arm64-v8a/libnative-app.so

echo "[3/4] Signing APK with apksigner..."
if [ ! -f debug.keystore ]; then
  keytool -genkey -v -keystore debug.keystore -storepass android -alias androiddebugkey -keypass android -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Android Debug,O=Android,C=US"
fi
apksigner sign --ks debug.keystore --ks-pass pass:android --key-pass pass:android --out app-debug.apk base.apk

echo "[4/4] Verifying APK size and ABI..."
ls -lh libnative-app.so app-debug.apk

if adb devices | grep -q "device$"; then
  adb install -r app-debug.apk
  adb shell am start -n com.example.nativec/android.app.NativeActivity
  echo "App successfully installed and launched in portrait mode!"
else
  echo "APK successfully built and signed at app-debug.apk!"
fi
