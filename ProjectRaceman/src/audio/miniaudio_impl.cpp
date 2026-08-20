// Single translation unit that compiles the miniaudio implementation.
// Everything else includes <miniaudio/miniaudio.h> as a plain header.

#define MINIAUDIO_IMPLEMENTATION

// The editor only needs playback. Dropping capture-side and unused codecs keeps
// this TU's compile time and the binary size down.
#define MA_NO_ENCODING
#define MA_NO_GENERATION

#include <miniaudio/miniaudio.h>
