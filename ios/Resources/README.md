For the public 1.0.0 API, waveform constants and attack/release envelopes, see:

- `../../README.md`

The iOS build copies files from this folder into the compiled product directory:

    <project-dir>/build/iphone/build/Products/<Debug-iphonesimulator|Release-iphoneos>/<app name>.app/

Place your module's iOS bundles and localization files in this folder.

These files overwrite files with the same names in the build directory. Check for conflicts with essential project files or files from other modules.
