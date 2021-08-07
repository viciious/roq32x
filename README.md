# roq32x
RoQ FVM playback demo for the Sega 32X.

The demo utilizes the two SH-2 CPUs to improve performance. The master CPU is used for decoding RoQ chunks, the other one - for blitting and conversion from YUV420 colorspace to R5G5B5 the 32x framebuffer uses. If the second CPU is lagging behind, the master CPU will then partly offload the remaining blitting work for the duration of the frame.

The optimal compression settings are 192x144 @ 15fps or 256x144 @ 12fps, or 256x192 @ 12 fps. Both stereo and mono audio is supported, but mono is recommended due to space and bandwidth concerns. Videos with width less or equal to 160 are stretched both horizontally and vertically, and play at 30/25 fps.

Pressing A enables low quality mode, which halves the final picture resolution.

Pressing B toggles runtime debug information.

http://wiki.multimedia.cx/index.php?title=RoQ


Real hardware capture
============
https://youtu.be/p1ADlcjeEnk


License
============
If a source file does not have a license header stating otherwise, then it is covered by the MIT license.

Credits
============
roq32x by Victor Luchitz

32X devkit by Chilly Willy

Based on original RoQ player by Tim Ferguson
