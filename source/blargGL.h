/*
    Copyright 2014-2022 Arisotura

    This file is part of blargSnes.

    blargSnes is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    blargSnes is distributed in the hope that it will be useful, but WITHOUT ANY 
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS 
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along 
    with blargSnes. If not, see http://www.gnu.org/licenses/.
*/

#ifndef BGL_H
#define BGL_H

#include <stdlib.h>
#include <string.h>
#include <3ds/types.h>
#include <3ds/gpu/registers.h>
#include <3ds/gpu/gpu.h>
#include <3ds/gpu/shbin.h>
#include <3ds/gpu/shaderProgram.h>

// blargGL -- thin wrapper around the ctrulib GPU API
// Not meant to be on par with OpenGL, just meant to be somewhat sane~

void bglInit();
void bglDeInit();


u32 bglUniformLoc(GPU_SHADER_TYPE type, const char* name);
void bglUniform(GPU_SHADER_TYPE type, u32 id, float* val);
void bglUniformMatrix(GPU_SHADER_TYPE type, u32 id, float* val);

void bglOutputBuffers(void* color, void* depth, u32 w, u32 h);
void bglViewport(u32 x, u32 y, u32 w, u32 h);

void bglScissorMode(GPU_SCISSORMODE mode);
void bglScissor(u32 x, u32 y, u32 w, u32 h);

void bglDepthRange(float min, float max);
void bglEnableDepthTest(bool enable);
void bglDepthFunc(GPU_TESTFUNC func);

void bglFaceCulling(GPU_CULLMODE mode);

void bglEnableStencilTest(bool enable);
void bglStencilFunc(GPU_TESTFUNC func, u32 ref, u32 mask, u32 replace);
void bglStencilOp(GPU_STENCILOP sfail, GPU_STENCILOP dfail, GPU_STENCILOP pass);

void bglColorDepthMask(GPU_WRITEMASK mask);

void bglEnableAlphaTest(bool enable);
void bglAlphaFunc(GPU_TESTFUNC func, u32 ref);

void bglBlendColor(u32 r, u32 g, u32 b, u32 a);
void bglBlendEquation(GPU_BLENDEQUATION coloreq, GPU_BLENDEQUATION alphaeq);
void bglBlendFunc(GPU_BLENDFACTOR colorsrc, GPU_BLENDFACTOR colordst, GPU_BLENDFACTOR alphasrc, GPU_BLENDFACTOR alphadst);

void bglEnableTextures(GPU_TEXUNIT units);
void bglTexEnv(u32 id, u32 colorsrc, u32 alphasrc, u32 colorop, u32 alphaop, GPU_COMBINEFUNC colorcomb, GPU_COMBINEFUNC alphacomb, u32 constcol);
void bglDummyTexEnv(u32 id);
void bglTexImage(GPU_TEXUNIT unit, void* data, u32 width, u32 height, u32 param, GPU_TEXCOLOR colortype);

void bglNumAttribs(u32 num);
void bglAttribBuffer(void* data);
void bglAttribType(u32 id, GPU_FORMATS datatype, u32 numcomps);

void bglDrawArrays(GPU_Primitive_t type, u32 numvertices);

void bglFlush();

#endif
