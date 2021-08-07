# roq32x
RoQ FVM playback demo for the Sega 32X.

The demo utilizes the two SH-2 CPUs to improve performance. The master CPU is used for decoding RoQ chunks, the other one - for blitting and conversion from YUV420 colorspace to R5G5B5 the 32x framebuffer uses. If the second CPU is lagging behind, the master CPU will then partly offload the remaining blitting work for the duration of the frame.

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
