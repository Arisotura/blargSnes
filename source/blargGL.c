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

#include <3ds.h>

#include "blargGL.h"


typedef union
{
	struct
	{
		u8 R;
		u8 G;
		u8 B;
		u8 A;
	};
	u32 Val;
	
} bglColor;

struct
{
	u32 GeometryStride;
	u32 ShaderAttrMask; // for vertex->geometry shaders

	shaderProgram_s* Shader;
	
	// physical addresses
	u32 ColorBuffer;
	u32 DepthBuffer;
	u32 OutputW, OutputH;
	
	u32 ViewportX, ViewportY;
	u32 ViewportW, ViewportH;
	
	GPU_SCISSORMODE ScissorMode;
	u32 ScissorX, ScissorY;
	u32 ScissorW, ScissorH;
	
	
	float DepthMin, DepthMax;
	
	
	GPU_CULLMODE CullMode;
	
	
	bool StencilTest;
	GPU_TESTFUNC StencilFunc;
	u8 StencilRef, StencilMask, StencilReplace;
	
	GPU_STENCILOP StencilOpSFail, StencilOpDFail, StencilOpPass;
	
	
	bglColor BlendingColor;
	
	
	bool DepthTest;
	GPU_TESTFUNC DepthFunc;
	
	GPU_WRITEMASK ColorDepthMask;
	
	
	GPU_BLENDEQUATION ColorBlendEquation, AlphaBlendEquation;
	GPU_BLENDFACTOR ColorSrcFactor, ColorDstFactor, AlphaSrcFactor, AlphaDstFactor;
	
	
	bool AlphaTest;
	GPU_TESTFUNC AlphaFunc;
	u8 AlphaRef;
	
	
	GPU_TEXUNIT TextureEnable;
	
	
	struct
	{
		u16 RGBSources, AlphaSources;
		u16 RGBOperands, AlphaOperands;
		GPU_COMBINEFUNC RGBCombine, AlphaCombine;
		bglColor ConstantColor;
		
	} TextureEnv[6];
	
	
	struct
	{
		u32 Data; // physical address
		u16 Width, Height;
		u32 Parameters;
		GPU_TEXCOLOR ColorType;
		
	} Texture[3];
	
	
	u8 NumAttribBuffers;
	u32 AttribBufferPtr; // physical address
	struct
	{
		u8 NumComponents;
		GPU_FORMATS DataType;
		
	} AttribBuffer[16];
	
	
	// bit0: shader changed
	// bit1: buffer attributes changed
	// bit2: viewport/scissor changed
	// the rest: anything else changed
	u32 DirtyFlags;
	
	bool DrawnSomething;
	
} bglState;

u32 bglCommandBufferSize; // this size in words
void* bglCommandBuffer;
u32 bglCommandBufferPos;



void bglInit()
{
	// hah, lazy
	memset(&bglState, 0, sizeof(bglState));
	bglState.DirtyFlags = 0xFFFFFFFF;
	
	bglCommandBufferSize = 0x80000;
	bglCommandBuffer = linearAlloc(bglCommandBufferSize * 4);
	
	bglCommandBufferPos = 0;
	GPUCMD_SetBuffer(&bglCommandBuffer[bglCommandBufferPos], bglCommandBufferSize, 0);
	
	// sane defaults
	bglDepthRange(-1.0f, 0.0f);
	bglScissorMode(GPU_SCISSOR_DISABLE);
	bglFaceCulling(GPU_CULL_BACK_CCW);
	bglEnableDepthTest(true);
	bglDepthFunc(GPU_GREATER);
	bglColorDepthMask(GPU_WRITE_ALL);
	bglBlendEquation(GPU_BLEND_ADD, GPU_BLEND_ADD);
	bglBlendFunc(GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);

}

void bglDeInit()
{
	linearFree(bglCommandBuffer);
}



// update PICA200 state as needed before drawing shit
void _bglUpdateState()
{
	u32 i;
	
	u32 dirty = bglState.DirtyFlags;
	if (!dirty) return;
	bglState.DirtyFlags = 0;
	
	if (bglState.DrawnSomething)
	{
		// not needed, probably doesn't help performance
		//if ((dirty & 0x5) != 0x4)
		//	GPU_FinishDrawing();
		bglState.DrawnSomething = false;
	}
	
	if (dirty & 0x1)
		shaderProgramUse(bglState.Shader);
	
	// TODO: a lot of this could be optimized with incremental writes
	
	if (dirty & 0x4)
	{
		GPUCMD_AddWrite(GPUREG_FRAMEBUFFER_FLUSH, 1);
		GPUCMD_AddWrite(GPUREG_FRAMEBUFFER_INVALIDATE, 1);
		
		GPUCMD_AddWrite(GPUREG_DEPTHBUFFER_LOC, bglState.DepthBuffer >> 3);
		GPUCMD_AddWrite(GPUREG_COLORBUFFER_LOC, bglState.ColorBuffer >> 3);
		
		GPUCMD_AddWrite(GPUREG_FRAMEBUFFER_DIM, bglState.OutputW | ((bglState.OutputH - 1) << 12) | (1<<24));
		GPUCMD_AddWrite(GPUREG_RENDERBUF_DIM, bglState.OutputW | ((bglState.OutputH - 1) << 12) | (1<<24));
		
		GPUCMD_AddWrite(GPUREG_DEPTHBUFFER_FORMAT, 0x00000003);
		GPUCMD_AddWrite(GPUREG_COLORBUFFER_FORMAT, 0x00000002);
		GPUCMD_AddWrite(GPUREG_FRAMEBUFFER_BLOCK32, 0x00000000);
		
		GPUCMD_AddWrite(GPUREG_COLORBUFFER_READ, bglState.ColorBuffer ? 0xF : 0);
		GPUCMD_AddWrite(GPUREG_COLORBUFFER_WRITE, bglState.ColorBuffer ? 0xF : 0);
		GPUCMD_AddWrite(GPUREG_DEPTHBUFFER_READ, bglState.DepthBuffer ? 0x3 : 0);
		GPUCMD_AddWrite(GPUREG_DEPTHBUFFER_WRITE, bglState.DepthBuffer ? 0x3 : 0);
	}
	
	if (dirty & 0x404)
	{
		GPUCMD_AddWrite(GPUREG_VIEWPORT_XY, bglState.ViewportX | (bglState.ViewportY << 16));
		GPUCMD_AddWrite(GPUREG_VIEWPORT_WIDTH, f32tof24((float)(bglState.ViewportW >> 1)));
		GPUCMD_AddWrite(GPUREG_VIEWPORT_INVW, f32tof31(2.0f / (float)bglState.ViewportW) << 1);
		GPUCMD_AddWrite(GPUREG_VIEWPORT_HEIGHT, f32tof24((float)(bglState.ViewportH >> 1)));
		GPUCMD_AddWrite(GPUREG_VIEWPORT_INVH, f32tof31(2.0f / (float)bglState.ViewportH) << 1);
	}
	
	if (dirty & 0x800)
	{
		GPUCMD_AddWrite(GPUREG_SCISSORTEST_MODE, bglState.ScissorMode);
		GPUCMD_AddWrite(GPUREG_SCISSORTEST_POS, bglState.ScissorX | (bglState.ScissorY << 16));
		GPUCMD_AddWrite(GPUREG_SCISSORTEST_DIM, (bglState.ScissorW-1) | ((bglState.ScissorH-1) << 16));
	}
	
	if (dirty & 0x8)
	{
		GPUCMD_AddWrite(GPUREG_DEPTHMAP_ENABLE, 1);
		GPUCMD_AddWrite(GPUREG_DEPTHMAP_SCALE, f32tof24(bglState.DepthMin));
		GPUCMD_AddWrite(GPUREG_DEPTHMAP_OFFSET, f32tof24(bglState.DepthMax));
	}
	
	if (dirty & 0x10)
	{
		GPUCMD_AddWrite(GPUREG_FACECULLING_CONFIG, bglState.CullMode);
	}
	
	if (dirty & 0x20)
	{
		GPUCMD_AddWrite(GPUREG_STENCIL_TEST,
			bglState.StencilTest |
			(bglState.StencilFunc << 4) |
			(bglState.StencilReplace << 8) | // buffer mask
			(bglState.StencilRef << 16) |
			(bglState.StencilMask << 24));
	}
	
	if (dirty & 0x80)
	{
		GPUCMD_AddWrite(GPUREG_FRAGOP_ALPHA_TEST, 
			bglState.AlphaTest | 
			(bglState.AlphaFunc << 4) | 
			(bglState.AlphaRef << 8));
	}
	
	if (dirty & 0x100)
	{
		GPUCMD_AddWrite(GPUREG_BLEND_COLOR, bglState.BlendingColor.Val);
		GPUCMD_AddWrite(GPUREG_COLOR_OPERATION, 0x00E40100);
		GPUCMD_AddWrite(GPUREG_BLEND_FUNC,
			bglState.ColorBlendEquation |
			(bglState.AlphaBlendEquation << 8) |
			(bglState.ColorSrcFactor << 16) |
			(bglState.ColorDstFactor << 20) |
			(bglState.AlphaSrcFactor << 24) |
			(bglState.AlphaDstFactor << 28));
	}
	
	if (dirty & 0x40)
	{
		GPUCMD_AddWrite(GPUREG_DEPTH_COLOR_MASK, 
			bglState.DepthTest | 
			(bglState.DepthFunc << 4) | 
			(bglState.ColorDepthMask << 8));
		GPUCMD_AddWrite(GPUREG_STENCIL_OP,
			bglState.StencilOpSFail |
			(bglState.StencilOpDFail << 4) |
			(bglState.StencilOpPass << 8));
	}

	// TODO: do the early-depth registers need to be set?
	
	// texturing
	if (dirty & 0x200)
	{
		GPUCMD_AddMaskedWrite(GPUREG_SH_OUTATTR_CLOCK, 0x2, bglState.TextureEnable << 8);
		GPUCMD_AddWrite(GPUREG_TEXUNIT_CONFIG, bglState.TextureEnable | (1<<12) | (1<<16));
		
		for (i = 0; i < 6; i++)
		{
			u32 regbases[6] = {GPUREG_TEXENV0_SOURCE, GPUREG_TEXENV1_SOURCE, GPUREG_TEXENV2_SOURCE,
			                   GPUREG_TEXENV3_SOURCE, GPUREG_TEXENV4_SOURCE, GPUREG_TEXENV5_SOURCE};
			
			GPUCMD_AddWrite(regbases[i]+0, bglState.TextureEnv[i].RGBSources | (bglState.TextureEnv[i].AlphaSources << 16));
			GPUCMD_AddWrite(regbases[i]+1, bglState.TextureEnv[i].RGBOperands | (bglState.TextureEnv[i].AlphaOperands << 12));
			GPUCMD_AddWrite(regbases[i]+2, bglState.TextureEnv[i].RGBCombine | (bglState.TextureEnv[i].AlphaCombine << 16));
			GPUCMD_AddWrite(regbases[i]+3, bglState.TextureEnv[i].ConstantColor.Val);
			GPUCMD_AddWrite(regbases[i]+4, 0);
		}
		
		if (bglState.TextureEnable & GPU_TEXUNIT0)
		{
			GPUCMD_AddWrite(GPUREG_TEXUNIT0_TYPE, bglState.Texture[0].ColorType);
			GPUCMD_AddWrite(GPUREG_TEXUNIT0_ADDR1, bglState.Texture[0].Data >> 3);
			GPUCMD_AddWrite(GPUREG_TEXUNIT0_DIM, (bglState.Texture[0].Width << 16) | bglState.Texture[0].Height);
			GPUCMD_AddWrite(GPUREG_TEXUNIT0_PARAM, bglState.Texture[0].Parameters);
		}
		if (bglState.TextureEnable & GPU_TEXUNIT1)
		{
			GPUCMD_AddWrite(GPUREG_TEXUNIT1_TYPE, bglState.Texture[1].ColorType);
			GPUCMD_AddWrite(GPUREG_TEXUNIT1_ADDR, bglState.Texture[1].Data >> 3);
			GPUCMD_AddWrite(GPUREG_TEXUNIT1_DIM, (bglState.Texture[1].Width << 16) | bglState.Texture[1].Height);
			GPUCMD_AddWrite(GPUREG_TEXUNIT1_PARAM, bglState.Texture[1].Parameters);
		}
		if (bglState.TextureEnable & GPU_TEXUNIT2)
		{
			GPUCMD_AddWrite(GPUREG_TEXUNIT2_TYPE, bglState.Texture[2].ColorType);
			GPUCMD_AddWrite(GPUREG_TEXUNIT2_ADDR, bglState.Texture[2].Data >> 3);
			GPUCMD_AddWrite(GPUREG_TEXUNIT2_DIM, (bglState.Texture[2].Width << 16) | bglState.Texture[2].Height);
			GPUCMD_AddWrite(GPUREG_TEXUNIT2_PARAM, bglState.Texture[2].Parameters);
		}
	}
	
	// attribute buffers
	// this lacks flexiblity
	if (dirty & 0x2)
	{
		u32 datasizes[4] = {1, 1, 2, 4};
		u32 numattr = bglState.NumAttribBuffers - 1;
		u64 attrib = 0;
		u64 permut = 0;
		u32 numcomp = 0;
		u32 stride = 0;
		
		for (i = 0; i < bglState.NumAttribBuffers; i++)
		{
			attrib |= GPU_ATTRIBFMT(i, bglState.AttribBuffer[i].NumComponents, bglState.AttribBuffer[i].DataType);
			attrib |= (1ULL << (48+i));
			permut |= ((u64)i << (i*4));
			
			numcomp++;
			stride += bglState.AttribBuffer[i].NumComponents * datasizes[bglState.AttribBuffer[i].DataType];
		}
		
		attrib |= ((u64)numattr << 60);
		attrib ^= (0xFFFULL << 48);
		
		permut |= ((u64)numcomp << 60);
		permut |= ((u64)stride << 48);
		
		GPUCMD_AddWrite(GPUREG_ATTRIBBUFFERS_LOC, bglState.AttribBufferPtr >> 3);
		GPUCMD_AddWrite(GPUREG_ATTRIBBUFFERS_FORMAT_LOW, (u32)attrib);
		GPUCMD_AddWrite(GPUREG_ATTRIBBUFFERS_FORMAT_HIGH, (u32)(attrib >> 32));
		
		GPUCMD_AddWrite(GPUREG_ATTRIBBUFFER0_OFFSET, 0);
		GPUCMD_AddWrite(GPUREG_ATTRIBBUFFER0_CONFIG1, (u32)permut);
		GPUCMD_AddWrite(GPUREG_ATTRIBBUFFER0_CONFIG2, (u32)(permut >> 32));
		
		GPUCMD_AddMaskedWrite(GPUREG_VSH_INPUTBUFFER_CONFIG, 0xB, 0xA0000000 | numattr);
		GPUCMD_AddWrite(GPUREG_VSH_NUM_ATTR, numattr);

		GPUCMD_AddWrite(GPUREG_VSH_ATTRIBUTES_PERMUTATION_LOW, (u32)permut);
		GPUCMD_AddWrite(GPUREG_VSH_ATTRIBUTES_PERMUTATION_HIGH, (u32)(permut >> 32) & 0xFFFF);
	}
}


void bglUseShader(shaderProgram_s* shader)
{
	if (bglState.Shader == shader)
		return;
	
	bglState.Shader = shader;
	bglState.DirtyFlags |= 0x1;
}


u32 bglUniformLoc(GPU_SHADER_TYPE type, const char* name)
{
	return shaderInstanceGetUniformLocation((type == GPU_GEOMETRY_SHADER) ? bglState.Shader->geometryShader : bglState.Shader->vertexShader, name);
}

void bglUniform(GPU_SHADER_TYPE type, u32 id, float* val)
{
	u32 regbase = (type == GPU_GEOMETRY_SHADER) ? GPUREG_GSH_FLOATUNIFORM_CONFIG : GPUREG_VSH_FLOATUNIFORM_CONFIG;
	
	/*u32 conv[4] = {f32tof24(val[0]), f32tof24(val[1]), f32tof24(val[2]), f32tof24(val[3])};
	
	GPUCMD_AddWrite(regbase, id);
	GPUCMD_AddWrite(regbase+1, conv[3] | (conv[2] << 24));
	GPUCMD_AddWrite(regbase+1, (conv[2] >> 8) | (conv[1] << 16));
	GPUCMD_AddWrite(regbase+1, (conv[1] >> 16) | (conv[0] << 8));*/
	
	GPUCMD_AddWrite(regbase, id | (1<<31));
	GPUCMD_AddWrite(regbase+1, *(u32*)&val[3]);
	GPUCMD_AddWrite(regbase+1, *(u32*)&val[2]);
	GPUCMD_AddWrite(regbase+1, *(u32*)&val[1]);
	GPUCMD_AddWrite(regbase+1, *(u32*)&val[0]);
}

void bglUniformMatrix(GPU_SHADER_TYPE type, u32 id, float* val)
{
	u32 regbase = (type == GPU_GEOMETRY_SHADER) ? GPUREG_GSH_FLOATUNIFORM_CONFIG : GPUREG_VSH_FLOATUNIFORM_CONFIG;
	
	/*GPUCMD_AddWrite(regbase, id);
	for (int i = 0; i < 16; i+=4)
	{
		u32 conv[4] = {f32tof24(val[i+0]), f32tof24(val[i+1]), f32tof24(val[i+2]), f32tof24(val[i+3])};
		
		GPUCMD_AddWrite(regbase+1, conv[3] | (conv[2] << 24));
		GPUCMD_AddWrite(regbase+1, (conv[2] >> 8) | (conv[1] << 16));
		GPUCMD_AddWrite(regbase+1, (conv[1] >> 16) | (conv[0] << 8));
	}*/
	
	GPUCMD_AddWrite(regbase, id | (1<<31));
	for (int i = 0; i < 16; i+=4)
	{
		GPUCMD_AddWrite(regbase+1, *(u32*)&val[i+3]);
		GPUCMD_AddWrite(regbase+1, *(u32*)&val[i+2]);
		GPUCMD_AddWrite(regbase+1, *(u32*)&val[i+1]);
		GPUCMD_AddWrite(regbase+1, *(u32*)&val[i+0]);
	}
}


void bglOutputBuffers(void* color, void* depth, u32 w, u32 h)
{
	bglState.ColorBuffer = osConvertVirtToPhys(color);
	bglState.DepthBuffer = osConvertVirtToPhys(depth);
	bglState.OutputW = w;
	bglState.OutputH = h;
	bglState.DirtyFlags |= 0x4;
}

void bglViewport(u32 x, u32 y, u32 w, u32 h)
{
	bglState.ViewportX = x;
	bglState.ViewportY = y;
	bglState.ViewportW = w;
	bglState.ViewportH = h;
	bglState.DirtyFlags |= 0x400;
	
	//bglScissorMode(GPU_SCISSOR_DISABLE);
	//bglScissor(x, y, w, h);
}

void bglScissorMode(GPU_SCISSORMODE mode)
{
	bglState.ScissorMode = mode;
	bglState.DirtyFlags |= 0x800;
}

void bglScissor(u32 x, u32 y, u32 w, u32 h)
{
	bglState.ScissorX = x;
	bglState.ScissorY = y;
	bglState.ScissorW = w;
	bglState.ScissorH = h;
	bglState.DirtyFlags |= 0x800;
}


void bglDepthRange(float min, float max)
{
	bglState.DepthMin = min;
	bglState.DepthMax = max;
	bglState.DirtyFlags |= 0x8;
}

void bglEnableDepthTest(bool enable)
{
	bglState.DepthTest = enable;
	bglState.DirtyFlags |= 0x40;
}

void bglDepthFunc(GPU_TESTFUNC func)
{
	bglState.DepthFunc = func;
	bglState.DirtyFlags |= 0x40;
}


void bglFaceCulling(GPU_CULLMODE mode)
{
	bglState.CullMode = mode;
	bglState.DirtyFlags |= 0x10;
}


void bglEnableStencilTest(bool enable)
{
	bglState.StencilTest = enable;
	bglState.DirtyFlags |= 0x20;
}

void bglStencilFunc(GPU_TESTFUNC func, u32 ref, u32 mask, u32 replace)
{
	bglState.StencilFunc = func;
	bglState.StencilRef = ref;
	bglState.StencilMask = mask;
	bglState.StencilReplace = replace;
	bglState.DirtyFlags |= 0x20;
}

void bglStencilOp(GPU_STENCILOP sfail, GPU_STENCILOP dfail, GPU_STENCILOP pass)
{
	bglState.StencilOpSFail = sfail;
	bglState.StencilOpDFail = dfail;
	bglState.StencilOpPass = pass;
	bglState.DirtyFlags |= 0x20;
}


void bglColorDepthMask(GPU_WRITEMASK mask)
{
	bglState.ColorDepthMask = mask;
	bglState.DirtyFlags |= 0x40;
}


void bglEnableAlphaTest(bool enable)
{
	bglState.AlphaTest = enable;
	bglState.DirtyFlags |= 0x80;
}

void bglAlphaFunc(GPU_TESTFUNC func, u32 ref)
{
	bglState.AlphaFunc = func;
	bglState.AlphaRef = ref;
	bglState.DirtyFlags |= 0x80;
}


void bglBlendColor(u32 r, u32 g, u32 b, u32 a)
{
	bglState.BlendingColor.R = r;
	bglState.BlendingColor.G = g;
	bglState.BlendingColor.B = b;
	bglState.BlendingColor.A = a;
	bglState.DirtyFlags |= 0x100;
}

void bglBlendEquation(GPU_BLENDEQUATION coloreq, GPU_BLENDEQUATION alphaeq)
{
	bglState.ColorBlendEquation = coloreq;
	bglState.AlphaBlendEquation = alphaeq;
	bglState.DirtyFlags |= 0x100;
}

void bglBlendFunc(GPU_BLENDFACTOR colorsrc, GPU_BLENDFACTOR colordst, GPU_BLENDFACTOR alphasrc, GPU_BLENDFACTOR alphadst)
{
	bglState.ColorSrcFactor = colorsrc;
	bglState.ColorDstFactor = colordst;
	bglState.AlphaSrcFactor = alphasrc;
	bglState.AlphaDstFactor = alphadst;
	bglState.DirtyFlags |= 0x100;
}


void bglEnableTextures(GPU_TEXUNIT units)
{
	bglState.TextureEnable = units;
	bglState.DirtyFlags |= 0x200;
}

void bglTexEnv(u32 id, u32 colorsrc, u32 alphasrc, u32 colorop, u32 alphaop, GPU_COMBINEFUNC colorcomb, GPU_COMBINEFUNC alphacomb, u32 constcol)
{
	bglState.TextureEnv[id].RGBSources = colorsrc;
	bglState.TextureEnv[id].AlphaSources = alphasrc;
	bglState.TextureEnv[id].RGBOperands = colorop;
	bglState.TextureEnv[id].AlphaOperands = alphaop;
	bglState.TextureEnv[id].RGBCombine = colorcomb;
	bglState.TextureEnv[id].AlphaCombine = alphacomb;
	bglState.TextureEnv[id].ConstantColor.Val = constcol;
	bglState.DirtyFlags |= 0x200;
}

void bglDummyTexEnv(u32 id)
{
	bglTexEnv(id,
		GPU_TEVSOURCES(GPU_PREVIOUS, 0, 0),
		GPU_TEVSOURCES(GPU_PREVIOUS, 0, 0),
		GPU_TEVOPERANDS(0,0,0),
		GPU_TEVOPERANDS(0,0,0),
		GPU_REPLACE, GPU_REPLACE,
		0xFFFFFFFF);
}

void bglTexImage(GPU_TEXUNIT unit, void* data, u32 width, u32 height, u32 param, GPU_TEXCOLOR colortype)
{
	u32 id = (unit==4) ? 2:(unit-1);
	
	bglState.Texture[id].Data = osConvertVirtToPhys(data);
	bglState.Texture[id].Width = width;
	bglState.Texture[id].Height = height;
	bglState.Texture[id].Parameters = param;
	bglState.Texture[id].ColorType = colortype;
	bglState.DirtyFlags |= 0x200;
}


void bglNumAttribs(u32 num)
{
	bglState.NumAttribBuffers = num;
	bglState.DirtyFlags |= 0x2;
}

void bglAttribBuffer(void* data)
{
	bglState.AttribBufferPtr = osConvertVirtToPhys(data);
	bglState.DirtyFlags |= 0x2;
}

void bglAttribType(u32 id, GPU_FORMATS datatype, u32 numcomps)
{
	bglState.AttribBuffer[id].NumComponents = numcomps;
	bglState.AttribBuffer[id].DataType = datatype;
	bglState.DirtyFlags |= 0x2;
}


void bglDrawArrays(GPU_Primitive_t type, u32 numvertices)
{
	_bglUpdateState();
	
	GPUCMD_AddMaskedWrite(GPUREG_PRIMITIVE_CONFIG, 2, type);
	GPUCMD_AddWrite(GPUREG_RESTART_PRIMITIVE, 1);
	GPUCMD_AddWrite(GPUREG_INDEXBUFFER_CONFIG, 0x80000000);
	GPUCMD_AddWrite(GPUREG_NUMVERTICES, numvertices);
	GPUCMD_AddWrite(GPUREG_VERTEX_OFFSET, 0);
	GPUCMD_AddMaskedWrite(GPUREG_GEOSTAGE_CONFIG2, 1, 1);
	GPUCMD_AddMaskedWrite(GPUREG_START_DRAW_FUNC0, 1, 0);
	GPUCMD_AddWrite(GPUREG_DRAWARRAYS, 1);
	GPUCMD_AddMaskedWrite(GPUREG_START_DRAW_FUNC0, 1, 1);
	GPUCMD_AddMaskedWrite(GPUREG_GEOSTAGE_CONFIG2, 1, 0);
	GPUCMD_AddWrite(GPUREG_VTX_FUNC, 1);
	
	bglState.DrawnSomething = true;
}


void bglFlush()
{
	if (bglState.DrawnSomething)
	{
		GPUCMD_AddWrite(GPUREG_FRAMEBUFFER_FLUSH, 1);
		GPUCMD_AddWrite(GPUREG_FRAMEBUFFER_INVALIDATE, 1);
		//GPUCMD_AddWrite(GPUREG_EARLYDEPTH_CLEAR, 1);
		
		bglState.DrawnSomething = false;
	}
	
	// GPUCMD_Split() will finalize the command buffer properly
	u32* buf; u32 size;
	GPUCMD_Split(&buf, &size);
	GX_ProcessCommandList(buf, size<<2, 2);
	
	bglCommandBufferPos ^= (bglCommandBufferSize >> 1);
	GPUCMD_SetBuffer(&bglCommandBuffer[bglCommandBufferPos], bglCommandBufferSize, 0);
}
