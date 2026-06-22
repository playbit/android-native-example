#!/bin/bash

APK=NativeExample.apk

[ -z "$ANDROID_NDK" ] && ANDROID_NDK=$HOME/android/ndk
[ -z "$ANDROID_SDK" ] && ANDROID_SDK=$HOME/android/sdk
[ -z "$JAVA_JDK" ] && JAVA_JDK=/usr/lib/jvm/java-8-openjdk

check() {
  if [ ! -f "$ANDROID_NDK/ndk-build" ]; then
    echo "Android NDK not found in \"$ANDROID_NDK\""
    return 1
  fi
  if [ ! -d "$ANDROID_SDK" ]; then
    echo "Android SDK not found in \"$ANDROID_SDK\""
    return 1
  fi
  if [ ! -f "$JAVA_JDK/bin/javac" ]; then
    echo "Java JDK not found in \"$JAVA_JDK\""
    return 1
  fi
  if [ ! -f "$ANDROID_SDK/platform-tools/adb" ]; then
    echo "Please install Android SDK Platform-tools"
    return 1
  fi
  BUILD_TOOLS=$(ls -t "$ANDROID_SDK/build-tools" | head -1)
  if [ ! -f "$ANDROID_SDK/build-tools/$BUILD_TOOLS/aapt" ]; then
    echo "Please install Android SDK Build-tools"
    return 1
  fi
  PLATFORM=$(ls -t "$ANDROID_SDK/platforms" | head -1)
  if [ ! -f "$ANDROID_SDK/platforms/$PLATFORM/android.jar" ]; then
    echo "Please install at least one Android SDK platform"
    return 1
  fi
  return 0
}

get_package_activity() {
  PACKAGE=$("$ANDROID_SDK/build-tools/$BUILD_TOOLS/aapt" dump badging bin/$APK | grep "package:" | sed -n "s/package: name='\([^']*\)'.*/\1/p")
  ACTIVITY=$("$ANDROID_SDK/build-tools/$BUILD_TOOLS/aapt" dump badging bin/$APK | grep "launchable-activity:" | sed -n "s/launchable-activity: name='\([^']*\)'.*/\1/p")
}

build() {
  [ ! -d bin ] && mkdir bin
  mkdir -p bin/classes

  "$ANDROID_NDK/ndk-build" -j4 NDK_LIBS_OUT=lib/lib || return 1

  CLASSPATH="$ANDROID_SDK/platforms/$PLATFORM/android.jar$DEP_JARS"

  mkdir -p bin/classes/com/example/NativeExample
  mkdir -p bin/src/com/example/NativeExample
  cp jni/MainActivity.java bin/src/com/example/NativeExample/

  "$JAVA_JDK/bin/javac" \
    -d "bin/classes" \
    -classpath "$CLASSPATH" \
    bin/src/com/example/NativeExample/MainActivity.java || return 1

  D8_INPUTS="bin/classes/com/example/NativeExample/MainActivity.class"
  for dep_dir in .deps/*/; do
    [ -f "$dep_dir/classes.jar" ] && D8_INPUTS="$D8_INPUTS $dep_dir/classes.jar"
  done

  "$ANDROID_SDK/build-tools/$BUILD_TOOLS/d8" \
    --min-api 26 \
    --output bin/ \
    $D8_INPUTS || return 1

  "$ANDROID_SDK/build-tools/$BUILD_TOOLS/aapt" package \
    -f \
    -M AndroidManifest.xml \
    -I "$ANDROID_SDK/platforms/$PLATFORM/android.jar" \
    -A assets \
    $DEP_RES_FLAGS \
    -F bin/$APK.build \
    bin lib || return 1

  if [ ! -f .keystore ]; then
    "$JAVA_JDK/bin/keytool" -genkey -dname "CN=Android Debug, O=Android, C=US" \
      -keystore .keystore -alias androiddebugkey \
      -storepass android -keypass android \
      -keyalg RSA -validity 30000 || return 1
  fi

  "$JAVA_JDK/bin/jarsigner" \
    -storepass android \
    -keystore .keystore \
    bin/$APK.build androiddebugkey > /dev/null || return 1

  "$ANDROID_SDK/build-tools/$BUILD_TOOLS/zipalign" -f 4 bin/$APK.build bin/$APK || return 1

  rm -f bin/$APK.build

  return 0
}

remove() {
  get_package_activity
  "$ANDROID_SDK/platform-tools/adb" uninstall $PACKAGE || return 1
  return 0
}

install() {
  "$ANDROID_SDK/platform-tools/adb" install -r bin/$APK || return 1
  return 0
}

launch() {
  get_package_activity
  "$ANDROID_SDK/platform-tools/adb" shell am start -n $PACKAGE/$ACTIVITY || return 1
  return 0
}

log() {
  "$ANDROID_SDK/platform-tools/adb" logcat -d NativeExample:V *:S || return 1
  return 0
}

go() {
  build || return 1
  install || return 1
  launch || return 1
  return 0
}

run() {
  install || return 1
  launch || return 1
  return 0
}

check || exit 1

case "$1" in
  "run")
    run
    ;;
  "build")
    build
    ;;
  "remove")
    remove
    ;;
  "install")
    install
    ;;
  "launch")
    launch
    ;;
  "log")
    log
    ;;
  "")
    go
    ;;
  *)
    echo "Usage: $0 [command]"
    echo "By default build, install and run .apk file."
    echo ""
    echo "Optional [command] can be:"
    echo "  run       - only install and run .apk file"
    echo "  build     - only build .apk file"
    echo "  remove    - remove installed .apk"
    echo "  install   - only install .apk file on connected device"
    echo "  launch    - ony run already installed .apk file"
    echo "  log       - show logcat"
    ;;
esac