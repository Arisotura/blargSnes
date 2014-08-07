TODO

short term

 * change 16bit access handlers to use ldrh/strh since unaligned accesses seem to work on the 3DS
 * PPU emulation!
 * SPC700 (port the core from lolSnes, figure out how to make threads work so we can use it)
 
long term

 * sound
 * scaling/borders (render to a texture and use the PICA200 instead of rendering to framebuffer)
 * expansion chips and other fancy shiz