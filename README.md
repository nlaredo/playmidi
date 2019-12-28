# playmidi
Playmidi is a web and curses and SDL-based MIDI file player for Linux and MacOS and Chrome (via Web MIDI and Web Audio apis).  The live version for use in chrome is found at https://nlaredo.github.io/playmidi.html

Play Roland Integra-7 demo, Wormhole (from Roland 1993):

[![Roland Integra-7 demo, Wormhole (from Roland 1993)](https://img.youtube.com/vi/ZviJcNXnBZo/mqdefault.jpg)](https://www.youtube.com/watch?v=ZviJcNXnBZo)

It supports software rendering of midi files via SDL audio and can also output midi events to external midi devices (in time with SDL audio soft synth) via both alsa sequencer api (linux) and coremidi api (osx).

Math-based synthesis (when no sf2 is loaded and no external midi output is set) is planned to evolve over time to support most of the features found in a minimoog voyager, and wavetable based sf2 support is expected to evolve to properly emulate all the features of a roland sc88 (but not there yet).

Not all sf2 files are yet fully supported, but currently most development in the git tree has happened with one you can find via google search called Scc1t2.sf2. It provides a good starting point for those without external midi hardware

[Discord](https://discord.gg/EUfcrzb)
