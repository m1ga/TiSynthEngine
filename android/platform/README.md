For the public 1.0.0 API, waveform constants and attack/release envelopes, see:

- `../../README.md`

The `./build/android` directory is treated like a Gradle project's `./src/main` directory. These files will be bundled into the module's built AAR library and copied into the built app's APK.

Files in the following folder are added to the APK's root "assets" folder. (Note that a Titanium app's files under the `Resources` directory are copied to the APK's `assets/Resources`.)

    <project-dir>/build/android/assets

Add resource files to the APK's "res" folder through directories such as these:

    <project-dir>/build/android/res/drawable
    <project-dir>/build/android/res/raw
    <project-dir>/build/android/res/xml

Prebuilt C/C++ `*.so` libraries must go under the following folder to be bundled with the AAR and APK.

    <project-dir>/build/android/jniLibs

Put Android AIDL code generation files in the following folder. The build system generates their Java code when it builds the app.

    <project-dir>/build/android/aidl

Put Android "RenderScript" files (`*.rs`) in the following folder.

    <project-dir>/build/android/rs
