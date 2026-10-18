# Android build (experimental, personal sideload)

The APK runs the same recompiled game as the desktop build, inside SDL's
Android activity, with on-screen touch controls (`nesrecomp/runner/cyc/cyc_touch.c`)
and any connected game controller. It contains no ROM: on first launch it asks
for your own Super Mario Bros. ROM file, checks its SHA-1
(`ea343f4e445a9050d4b4fbac2c77d0693b1d0922`) and copies it into the app's
private storage. The native library is recompiled from that ROM, so keep the
APK to yourself. Release builds are signed with the Android debug key.

Shaders are desktop OpenGL only; on Android the picture is drawn without them.

## Requirements

- Android SDK with NDK 28.2.13676358 and CMake 3.31.6 (`sdkmanager "ndk;28.2.13676358" "cmake;3.31.6" "platforms;android-36"`)
- JDK 17 (`JAVA_HOME`) and Gradle 8.13+ (`gradle` on PATH, or `GRADLE`)
- SDL 2.30.12 source unpacked at `android/third_party/SDL2-2.30.12`. The
  configure step patches its Android `SDL_GetBasePath` to return the app's
  internal storage (see `sdl_include.cmake`).
- A Windows `build_release` (its `assets/` and `mods/packages/` are packed)
  and its host compiler `build_release/nesrecomp-host/Release/NESRecomp.exe`.

## Build

Configure and build each ABI with the NDK toolchain (arm64-v8a for phones,
x86_64 for the emulator), from the repository root:

```powershell
$sdk = "$env:LOCALAPPDATA\Android\Sdk"
cmake -S . -B android\build-arm64 -G Ninja `
  -DCMAKE_MAKE_PROGRAM="$sdk\cmake\3.31.6\bin\ninja.exe" `
  -DCMAKE_TOOLCHAIN_FILE="$sdk\ndk\28.2.13676358\build\cmake\android.toolchain.cmake" `
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 -DCMAKE_BUILD_TYPE=Release `
  -DNESRECOMP_ROM=<path to your smb1.nes> `
  -DNESRECOMP_HOST_COMPILER="$PWD\build_release\nesrecomp-host\Release\NESRecomp.exe" `
  -DSMB_ENABLE_NETPLAY=OFF -DNESRECOMP_REQUIRE_FALCON_OWNER_HELPER=OFF `
  -DNESRECOMP_BACKEND=cycle -DNESRECOMP_ENABLE_TRACE=OFF `
  -DCMAKE_PROJECT_SuperMarioBrosRecomp_INCLUDE="$PWD\android\sdl_include.cmake" `
  -DSMB_ANDROID_SDL_ROOT="$PWD\android\third_party\SDL2-2.30.12"
cmake --build android\build-arm64
```

Then package: `powershell -File android\build-apk.ps1` (add `-Abis arm64-v8a`
to skip the emulator ABI, `-BuildRoot <dir>` if the CMake builds live elsewhere).

## Testing

`-Variant debug` builds a debuggable APK that also accepts host arguments,
so the desktop TCP test server works on a device or emulator:

```powershell
adb install -r android\app-project\app\build\outputs\apk\debug\app-debug.apk
adb shell am start -n com.mattd.smbrecomp/.SetupActivity --es args "'--tcp 4370'"
adb forward tcp:4370 tcp:4370
```

logcat tag `SDL/APP` reports the frame rate and slowest frame every 5 s.
