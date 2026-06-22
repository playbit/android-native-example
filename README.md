First either adjust `a.cmd` to have correct paths to Android SDK, Android NDK and Java SDK or set them in environment variables.
Then set `APK` varible to desired apk package name. Use Android SDK to install build-tools and platform SDK. 

Now you can use `a.cmd` to build, install and run the application:

    a [command]
    By default build, install and run .apk file.

    Optional [command] is:
      run       - only install and run .apk file
      build     - only build .apk file
      remove    - remove installed .apk
      install   - only install .apk file on connected device
      launch    - ony run already installed .apk file
      log       - show logcat


Clear logs:

```bash
adb logcat -c
```

Dump crashes:

```bash
adb logcat -s "AndroidRuntime"
```

Show app logs:

```bash
adb logcat -s "NativeExample"
```

Extract wgpu to jni/wgpu: https://github.https://github.com/gfx-rs/wgpu-native/releases/download/v29.0.0.0/wgpu-android-aarch64-release.zip