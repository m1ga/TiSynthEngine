For the public 1.0.0 API, waveform constants and attack/release envelopes, see:

- `../../README.md`

The iOS build copies files from this folder into its build directory:

    <project-dir>/build/iphone

Place asset catalogs, .framework files and storyboards in this directory.

These files overwrite files with the same names in the build directory. Check for conflicts with essential project files or files from other modules.
