cd "$(dirname "$0")"

NDK=/data/user/0/aidepro.top/no_backup/ndksupport-1710240003/android-ndk-aide

$NDK/ndk-build \
    NDK_PROJECT_PATH=. \
    APP_BUILD_SCRIPT=./jni/Android.mk \
    NDK_APPLICATION_MK=./jni/Application.mk

echo "输出文件：$(pwd)/libs/arm64-v8a/main"