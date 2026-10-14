# To do

- [ ] **Read Standard MIDI Files and convert them to synthesizer events.**
  Requested by César after the Titanium Asteroids procedural jingle work.
  Explicitly reserved for another day and a dedicated MIDI session; do not
  implement as part of the current Asteroids fixes.
  Investigate a reader/converter for `.mid` files that maps notes, note-on/off
  timing, velocity, tempo changes, tracks and channels to the module's synthesis
  primitives. Preserve rests, chords and note lengths, with explicit instrument
  mappings instead of assuming General MIDI instrument playback. Define supported
  file formats and controller events, voice budgets, cancellation, pause/resume,
  and an Android/iOS parity contract. Test tempo changes, simultaneous tracks,
  overlapping notes and malformed files. The current module does not read MIDI;
  this item records future work and does not introduce a new API.
