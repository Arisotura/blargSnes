/*
    Copyright 2014-2015 StapleButter

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
	
	void* ColorBuffer;
	void* DepthBuffer;
	u32 BufferW, BufferH;
	
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
		void* Data;
		u16 Width, Height;
		u32 Parameters;
		GPU_TEXCOLOR ColorType;
		
	} Texture[3];
	
	
	u8 NumAttribBuffers;
	void* AttribBufferPtr;
	struct
	{
		u8 NumComponents;
		GPU_FORMATS DataType;
		
	} AttribBuffer[16];
	
	
	u32 DirtyFlags;
	
	bool DrawnSomething;
	
} bglState;

u32 bglCommandBufferSize; // this size in words
void* bglCommandBuffer;


// dirty flags
#define DIRTY_SHADERS			0x00000001
#define DIRTY_ATTRIBTYPES		0x00000002 // attribute buffer types
#define DIRTY_OUTBUFFERS		0x00000004 // output buffers
#define DIRTY_VIEWPORT			0x00000008
#define DIRTY_SCISSOR			0x00000010
#define DIRTY_STENCILTEST		0x00000020
#define DIRTY_DEPTHRANGE		0x00000040
#define DIRTY_DEPTHTEST			0x00000080 // depth test & depth/color write mask (reg 0x0107)
#define DIRTY_ALPHABLEND		0x00000100
#define DIRTY_ALPHATEST			0x00000200
#define DIRTY_TEXENABLE			0x00000400
#define DIRTY_CULLING			0x00000800
#define DIRTY_BLENDCOLOR		0x00001000
#define DIRTY_TEXENV(n)			(0x00800000<<n)
#define DIRTY_TEXUNITS(n)		(0x20000000<<n)
#define DIRTY_ALL				0xFFFFFFFF // better take it to the laundry at this rate



void bglInit()
{
	// hah, lazy
	memset(&bglState, 0, sizeof(bglState));
	bglState.DirtyFlags = DIRTY_ALL;
	
	bglCommandBufferSize = 0x80000;
	bglCommandBuffer = linearAlloc(bglCommandBufferSize * 4);
	
	//GPU_Reset(NULL, bglCommandBuffer, bglCommandBufferSize); // blarg?
	GPUCMD_SetBuffer(bglCommandBuffer, bglCommandBufferSize, 0);
	
	// sane defaults
	bglDepthRange(-1.0f, 0.0f);
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
	u32 temp[39];
	
	u32 dirty = bglState.DirtyFlags;
	if (!dirty) return;
	bglState.DirtyFlags = 0;
	
	if (dirty & DIRTY_SHADERS)
	{
		shaderProgramUse(bglState.Shader);
	}
	
	if (dirty & DIRTY_OUTBUFFERS)
	{
		GPUCMD_AddWrite(GPUREG_FRAMEBUFFER_FLUSH, 0x00000001);
		GPUCMD_AddWrite(GPUREG_FRAMEBUFFER_INVALIDATE, 0x00000001);
		
		temp[0] = (u32)bglState.DepthBuffer >> 3;
		temp[1] = (u32)bglState.ColorBuffer >> 3;
		temp[2] = 0x01000000 | (((bglState.BufferH-1)&0xFFF)<<12) | (bglState.BufferW&0xFFF);
		GPUCMD_AddIncrementalWrites(GPUREG_DEPTHBUFFER_LOC, temp, 3);
		
		GPUCMD_AddWrite(GPUREG_RENDERBUF_DIM, temp[2]);
		GPUCMD_AddWrite(GPUREG_DEPTHBUFFER_FORMAT, 0x00000003); // depth 24 stencil 8
		GPUCMD_AddWrite(GPUREG_COLORBUFFER_FORMAT, 0x00000002); // 32bit RGBA8
		GPUCMD_AddWrite(GPUREG_FRAMEBUFFER_BLOCK32, 0x00000000);
		
		temp[0] = 0x0000000F;
		temp[1] = 0x0000000F;
		temp[2] = 0x00000002;
		temp[3] = 0x00000002;
		GPUCMD_AddIncrementalWrites(GPUREG_COLORBUFFER_READ, temp, 4);
	}
	
	if (dirty & DIRTY_VIEWPORT)
	{
		float fw = (float)bglState.ViewportW;
		float fh = (float)bglState.ViewportH;
		
		temp[0] = f32tof24(fw/2);
		temp[1] = f32tof31(2.0f / fw) << 1;
		temp[2] = f32tof24(fh/2);
		temp[3] = f32tof31(2.0f / fh) << 1;
		GPUCMD_AddIncrementalWrites(GPUREG_VIEWPORT_WIDTH, temp, 4);

		GPUCMD_AddWrite(GPUREG_VIEWPORT_XY, (bglState.ViewportY<<16)|(bglState.ViewportX&0xFFFF));
	}
	
	if (dirty & DIRTY_SCISSOR)
	{
		temp[0] = bglState.ScissorMode;
		temp[1] = (bglState.ScissorY<<16)|(bglState.ScissorX&0xFFFF);
		temp[2] = ((bglState.ScissorH-1)<<16)|((bglState.ScissorW-1)&0xFFFF);
		GPUCMD_AddIncrementalWrites(GPUREG_SCISSORTEST_MODE, temp, 3);
	}
	
	if (dirty & DIRTY_DEPTHRANGE)
	{
		GPUCMD_AddWrite(GPUREG_DEPTHMAP_ENABLE, 0x00000001);
		GPUCMD_AddWrite(GPUREG_DEPTHMAP_SCALE, f32tof24(bglState.DepthMin));
		GPUCMD_AddWrite(GPUREG_DEPTHMAP_OFFSET, f32tof24(bglState.DepthMax));
	}
	
	if (dirty & DIRTY_CULLING)
	{
		GPUCMD_AddWrite(GPUREG_FACECULLING_CONFIG, bglState.CullMode);
	}
	
	if (dirty & DIRTY_STENCILTEST)
	{
		// TODO misnamed shit
		GPUCMD_AddWrite(GPUREG_STENCIL_TEST, (bglState.StencilTest&1) |
		                                     ((bglState.StencilFunc&7)<<4) |
											 (bglState.StencilReplace<<8) | // write mask
											 (bglState.StencilRef<<16) |
											 (bglState.StencilMask<<24)); // input mask

		GPUCMD_AddWrite(GPUREG_STENCIL_OP, bglState.StencilOpSFail | 
		                                       (bglState.StencilOpDFail << 4) | 
											   (bglState.StencilOpPass << 8));
	}
	
	if (dirty & DIRTY_BLENDCOLOR)
	{
		GPUCMD_AddWrite(GPUREG_BLEND_COLOR, bglState.BlendingColor.R | 
		                                    (bglState.BlendingColor.G << 8) | 
											(bglState.BlendingColor.B << 16) | 
											(bglState.BlendingColor.A << 24));
	}
	
	if (dirty & DIRTY_DEPTHTEST)
	{
		GPUCMD_AddWrite(GPUREG_DEPTH_COLOR_MASK, (bglState.DepthTest&1) |
		                                         ((bglState.DepthFunc&7)<<4) |
												 (bglState.ColorDepthMask<<8));
												 
		// early Z-test crap
		// TODO: check if this is required
		GPUCMD_AddMaskedWrite(GPUREG_EARLYDEPTH_TEST1, 0x1, 0);
		GPUCMD_AddWrite(GPUREG_EARLYDEPTH_TEST2, 0);
	}
	
	if (dirty & DIRTY_ALPHABLEND)
	{
		GPUCMD_AddWrite(GPUREG_BLEND_FUNC, bglState.ColorBlendEquation | 
		                                     (bglState.AlphaBlendEquation<<8) | 
											 (bglState.ColorSrcFactor<<16) | 
											 (bglState.ColorDstFactor<<20) | 
											 (bglState.AlphaSrcFactor<<24) | 
											 (bglState.AlphaDstFactor<<28));
		
		GPUCMD_AddMaskedWrite(GPUREG_COLOR_OPERATION, 0x2, 0x00000100);
	}
	
	if (dirty & DIRTY_ALPHATEST)
	{
		GPUCMD_AddWrite(GPUREG_FRAGOP_ALPHA_TEST, (bglState.AlphaTest&1) |
		                                         ((bglState.AlphaFunc&7)<<4) |
												 (bglState.AlphaRef<<8));
	}
	
	if (dirty & DIRTY_TEXENABLE)
	{
		GPUCMD_AddWrite(GPUREG_TEXUNIT_CONFIG, 0x00011000|bglState.TextureEnable); // enables texture units
	}
	
	const u8 tevreg[] = {
		GPUREG_TEXENV0_SOURCE,
		GPUREG_TEXENV1_SOURCE,
		GPUREG_TEXENV2_SOURCE,
		GPUREG_TEXENV3_SOURCE,
		GPUREG_TEXENV4_SOURCE,
		GPUREG_TEXENV5_SOURCE
	};
	for (i = 0; i < 6; i++)
	{
		if (dirty & DIRTY_TEXENV(i))
		{
			temp[0] = (bglState.TextureEnv[i].AlphaSources<<16) | (bglState.TextureEnv[i].RGBSources);
			temp[1] = (bglState.TextureEnv[i].AlphaOperands<<12) | (bglState.TextureEnv[i].RGBOperands);
			temp[2] = (bglState.TextureEnv[i].AlphaCombine<<16) | (bglState.TextureEnv[i].RGBCombine);
			temp[3] = bglState.TextureEnv[i].ConstantColor.Val;
			temp[4] = 0x00000000; // ?

			GPUCMD_AddIncrementalWrites(tevreg[i], temp, 5);
		}
	}
	
	if (dirty & DIRTY_TEXUNITS(0))
	{
		GPUCMD_AddWrite(GPUREG_TEXUNIT0_TYPE, bglState.Texture[0].ColorType);
		GPUCMD_AddWrite(GPUREG_TEXUNIT0_ADDR1, ((u32)bglState.Texture[0].Data)>>3);
		GPUCMD_AddWrite(GPUREG_TEXUNIT0_DIM, (bglState.Texture[0].Width<<16)|bglState.Texture[0].Height);
		GPUCMD_AddWrite(GPUREG_TEXUNIT0_PARAM, bglState.Texture[0].Parameters);
	}
	if (dirty & DIRTY_TEXUNITS(1))
	{
		GPUCMD_AddWrite(GPUREG_TEXUNIT1_TYPE, bglState.Texture[1].ColorType);
		GPUCMD_AddWrite(GPUREG_TEXUNIT1_ADDR, ((u32)bglState.Texture[1].Data)>>3);
		GPUCMD_AddWrite(GPUREG_TEXUNIT1_DIM, (bglState.Texture[1].Width<<16)|bglState.Texture[1].Height);
		GPUCMD_AddWrite(GPUREG_TEXUNIT1_PARAM, bglState.Texture[1].Parameters);
	}
	if (dirty & DIRTY_TEXUNITS(2))
	{
		GPUCMD_AddWrite(GPUREG_TEXUNIT2_TYPE, bglState.Texture[2].ColorType);
		GPUCMD_AddWrite(GPUREG_TEXUNIT2_ADDR, ((u32)bglState.Texture[2].Data)>>3);
		GPUCMD_AddWrite(GPUREG_TEXUNIT2_DIM, (bglState.Texture[2].Width<<16)|bglState.Texture[2].Height);
		GPUCMD_AddWrite(GPUREG_TEXUNIT2_PARAM, bglState.Texture[2].Parameters);
	}
	
	const u8 attribsize[4] = {1,1,2,4};
	if (dirty & DIRTY_ATTRIBTYPES)
	{
		// implementation for one buffer with multiple attributes interleaved
		// works well enough for what we do
		
		u64 attrib = 0;
		u64 permut = 0;
		u32 stride = 0;
		
		for (i = 0; i < bglState.NumAttribBuffers; i++)
		{
			attrib |= GPU_ATTRIBFMT(i, bglState.AttribBuffer[i].NumComponents, bglState.AttribBuffer[i].DataType);
			permut |= (i << (i*4));
			stride += bglState.AttribBuffer[i].NumComponents * attribsize[bglState.AttribBuffer[i].DataType];
		}
		
		memset(temp, 0, 39*4);
		temp[0] = (u32)bglState.AttribBufferPtr >> 3;
		temp[1] = (u32)attrib;
		temp[2] = ((bglState.NumAttribBuffers-1)<<28) | (0xFFC<<16) | ((attrib>>32)&0xFFFF);
		
		temp[3] = 0;
		temp[4] = (u32)permut;
		temp[5] = (bglState.NumAttribBuffers<<28) | ((stride&0xFFF)<<16) | ((permut>>32)&0xFFFF);
		
		GPUCMD_AddIncrementalWrites(GPUREG_ATTRIBBUFFERS_LOC, temp, 39);

		GPUCMD_AddMaskedWrite(GPUREG_VSH_INPUTBUFFER_CONFIG, 0xB, 0xA0000000|(bglState.NumAttribBuffers-1));
		GPUCMD_AddWrite(GPUREG_VSH_NUM_ATTR, (bglState.NumAttribBuffers-1));

		GPUCMD_AddIncrementalWrites(GPUREG_VSH_ATTRIBUTES_PERMUTATION_LOW, ((u32[]){(u32)permut, (permut>>32)&0xFFFF}), 2);
	}
}


void bglUseShader(shaderProgram_s* shader)
{
	if (shader == bglState.Shader) 
		return;
	
	bglState.Shader = shader;
	bglState.DirtyFlags |= DIRTY_SHADERS;
}


// TODO: defer uniforms like the rest? never had a problem with doing it this way
// 96 possible float uniforms
void _bglUniform(GPU_SHADER_TYPE type, u32 id, u32* val, u32 size)
{
	int regOffset=(type==GPU_GEOMETRY_SHADER)?(-0x30):(0x0);

	GPUCMD_AddWrite(GPUREG_VSH_FLOATUNIFORM_CONFIG+regOffset, 0x80000000|id);
	GPUCMD_AddWrites(GPUREG_VSH_FLOATUNIFORM_DATA+regOffset, val, size*4);
}

void bglUniform(GPU_SHADER_TYPE type, u32 id, float* val)
{
	float pancake[4] = {val[3], val[2], val[1], val[0]};
	_bglUniform(type, id, (u32*)pancake, 1);
}

void bglUniformMatrix(GPU_SHADER_TYPE type, u32 id, float* val)
{
	float pancake[16] = {
		val[3], val[2], val[1], val[0],
		val[7], val[6], val[5], val[4],
		val[11], val[10], val[9], val[8],
		val[15], val[14], val[13], val[12]
	};
	_bglUniform(type, id, (u32*)pancake, 4);
}


void bglOutputBuffers(void* color, void* depth, u32 w, u32 h)
{
	color = (void*)osConvertVirtToPhys(color);
	depth = (void*)osConvertVirtToPhys(depth);
	
	if (color == bglState.ColorBuffer && 
		depth == bglState.DepthBuffer &&
		w == bglState.BufferW &&
		h == bglState.BufferH)
		return;
	
	bglState.ColorBuffer = color;
	bglState.DepthBuffer = depth;
	bglState.BufferW = w;
	bglState.BufferH = h;
	bglState.DirtyFlags |= DIRTY_OUTBUFFERS;
}

void bglViewport(u32 x, u32 y, u32 w, u32 h)
{
	if (x == bglState.ViewportX && 
		y == bglState.ViewportY && 
		w == bglState.ViewportW && 
		h == bglState.ViewportH)
		return;
	
	bglState.ViewportX = x;
	bglState.ViewportY = y;
	bglState.ViewportW = w;
	bglState.ViewportH = h;
	bglState.DirtyFlags |= DIRTY_VIEWPORT;
}

void bglScissorMode(GPU_SCISSORMODE mode)
{
	if (mode == bglState.ScissorMode)
		return;
	
	bglState.ScissorMode = mode;
	bglState.DirtyFlags |= DIRTY_SCISSOR;
}

void bglScissor(u32 x, u32 y, u32 w, u32 h)
{
	if (x == bglState.ViewportX && 
		y == bglState.ViewportY && 
		w == bglState.ViewportW && 
		h == bglState.ViewportH)
		return;
	
	bglState.ScissorX = x;
	bglState.ScissorY = y;
	bglState.ScissorW = w;
	bglState.ScissorH = h;
	bglState.DirtyFlags |= DIRTY_SCISSOR;
}


void bglDepthRange(float min, float max)
{
	if (min == bglState.DepthMin && 
		max == bglState.DepthMax)
		return;
	
	bglState.DepthMin = min;
	bglState.DepthMax = max;
	bglState.DirtyFlags |= DIRTY_DEPTHRANGE;
}

void bglEnableDepthTest(bool enable)
{
	if (enable == bglState.DepthTest)
		return;
	
	bglState.DepthTest = enable;
	bglState.DirtyFlags |= DIRTY_DEPTHTEST;
}

void bglDepthFunc(GPU_TESTFUNC func)
{
	if (func == bglState.DepthFunc)
		return;
	
	bglState.DepthFunc = func;
	bglState.DirtyFlags |= DIRTY_DEPTHTEST;
}


void bglFaceCulling(GPU_CULLMODE mode)
{
	if (mode == bglState.CullMode)
		return;
	
	bglState.CullMode = mode;
	bglState.DirtyFlags |= DIRTY_CULLING;
}


void bglEnableStencilTest(bool enable)
{
	if (enable == bglState.StencilTest)
		return;
	
	bglState.StencilTest = enable;
	bglState.DirtyFlags |= DIRTY_STENCILTEST;
}

void bglStencilFunc(GPU_TESTFUNC func, u32 ref, u32 mask, u32 replace)
{
	if (func == bglState.StencilFunc && 
		ref == bglState.StencilRef && 
		mask == bglState.StencilMask && 
		replace == bglState.StencilReplace)
		return;
	
	bglState.StencilFunc = func;
	bglState.StencilRef = ref;
	bglState.StencilMask = mask;
	bglState.StencilReplace = replace;
	bglState.DirtyFlags |= DIRTY_STENCILTEST;
}

void bglStencilOp(GPU_STENCILOP sfail, GPU_STENCILOP dfail, GPU_STENCILOP pass)
{
	if (sfail == bglState.StencilOpSFail &&
		dfail == bglState.StencilOpDFail &&
		pass == bglState.StencilOpPass)
		return;
		
	bglState.StencilOpSFail = sfail;
	bglState.StencilOpDFail = dfail;
	bglState.StencilOpPass = pass;
	bglState.DirtyFlags |= DIRTY_STENCILTEST;
}


void bglColorDepthMask(GPU_WRITEMASK mask)
{
	if (mask == bglState.ColorDepthMask)
		return;
	
	bglState.ColorDepthMask = mask;
	bglState.DirtyFlags |= DIRTY_DEPTHTEST;
}


void bglEnableAlphaTest(bool enable)
{
	if (enable == bglState.AlphaTest)
		return;
	
	bglState.AlphaTest = enable;
	bglState.DirtyFlags |= DIRTY_ALPHATEST;
}

void bglAlphaFunc(GPU_TESTFUNC func, u32 ref)
{
	if (func == bglState.AlphaFunc && 
		ref == bglState.AlphaRef)
		return;
		
	bglState.AlphaFunc = func;
	bglState.AlphaRef = ref;
	bglState.DirtyFlags |= DIRTY_ALPHATEST;
}


void bglBlendColor(u32 r, u32 g, u32 b, u32 a)
{
	if (r == bglState.BlendingColor.R &&
		g == bglState.BlendingColor.G &&
		b == bglState.BlendingColor.B &&
		a == bglState.BlendingColor.A)
		return;
		
	bglState.BlendingColor.R = r;
	bglState.BlendingColor.G = g;
	bglState.BlendingColor.B = b;
	bglState.BlendingColor.A = a;
	bglState.DirtyFlags |= DIRTY_BLENDCOLOR;
}

void bglBlendEquation(GPU_BLENDEQUATION coloreq, GPU_BLENDEQUATION alphaeq)
{
	if (coloreq == bglState.ColorBlendEquation &&
		alphaeq == bglState.AlphaBlendEquation)
		return;
		
	bglState.ColorBlendEquation = coloreq;
	bglState.AlphaBlendEquation = alphaeq;
	bglState.DirtyFlags |= DIRTY_ALPHABLEND;
}

void bglBlendFunc(GPU_BLENDFACTOR colorsrc, GPU_BLENDFACTOR colordst, GPU_BLENDFACTOR alphasrc, GPU_BLENDFACTOR alphadst)
{
	if (colorsrc == bglState.ColorSrcFactor &&
		colordst == bglState.ColorDstFactor &&
		alphasrc == bglState.AlphaSrcFactor &&
		alphadst == bglState.AlphaDstFactor)
		return;
		
	bglState.ColorSrcFactor = colorsrc;
	bglState.ColorDstFactor = colordst;
	bglState.AlphaSrcFactor = alphasrc;
	bglState.AlphaDstFactor = alphadst;
	bglState.DirtyFlags |= DIRTY_ALPHABLEND;
}


void bglEnableTextures(GPU_TEXUNIT units)
{
	if (units == bglState.TextureEnable)
		return;
	
	bglState.TextureEnable = units;
	bglState.DirtyFlags |= DIRTY_TEXENABLE;
}

void bglTexEnv(u32 id, u32 colorsrc, u32 alphasrc, u32 colorop, u32 alphaop, GPU_COMBINEFUNC colorcomb, GPU_COMBINEFUNC alphacomb, u32 constcol)
{
	// TODO: maybe optimize those big checks?
	if (colorsrc == bglState.TextureEnv[id].RGBSources &&
		alphasrc == bglState.TextureEnv[id].AlphaSources &&
		colorop == bglState.TextureEnv[id].RGBOperands &&
		alphaop == bglState.TextureEnv[id].AlphaOperands &&
		colorcomb == bglState.TextureEnv[id].RGBCombine &&
		alphacomb == bglState.TextureEnv[id].AlphaCombine &&
		constcol == bglState.TextureEnv[id].ConstantColor.Val)
		return;
		
	bglState.TextureEnv[id].RGBSources = colorsrc;
	bglState.TextureEnv[id].AlphaSources = alphasrc;
	bglState.TextureEnv[id].RGBOperands = colorop;
	bglState.TextureEnv[id].AlphaOperands = alphaop;
	bglState.TextureEnv[id].RGBCombine = colorcomb;
	bglState.TextureEnv[id].AlphaCombine = alphacomb;
	bglState.TextureEnv[id].ConstantColor.Val = constcol;
	bglState.DirtyFlags |= DIRTY_TEXENV(id);
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
	data = (void*)osConvertVirtToPhys(data);
	
	if (data == bglState.Texture[id].Data &&
		width == bglState.Texture[id].Width &&
		height == bglState.Texture[id].Height &&
		param == bglState.Texture[id].Parameters &&
		colortype == bglState.Texture[id].ColorType)
		return;
	
	bglState.Texture[id].Data = data;
	bglState.Texture[id].Width = width;
	bglState.Texture[id].Height = height;
	bglState.Texture[id].Parameters = param;
	bglState.Texture[id].ColorType = colortype;
	bglState.DirtyFlags |= DIRTY_TEXUNITS(id);
}


void bglNumAttribs(u32 num)
{
	if (num == bglState.NumAttribBuffers)
		return;
	
	bglState.NumAttribBuffers = num;
	bglState.DirtyFlags |= DIRTY_ATTRIBTYPES;
}

void bglAttribBuffer(void* data)
{
	data = (void*)osConvertVirtToPhys(data);
	
	if (data == bglState.AttribBufferPtr)
		return;
	
	bglState.AttribBufferPtr = data;
	bglState.DirtyFlags |= DIRTY_ATTRIBTYPES;
}

void bglAttribType(u32 id, GPU_FORMATS datatype, u32 numcomps)
{
	if (datatype == bglState.AttribBuffer[id].DataType &&
		numcomps == bglState.AttribBuffer[id].NumComponents)
		return;
		
	bglState.AttribBuffer[id].NumComponents = numcomps;
	bglState.AttribBuffer[id].DataType = datatype;
	bglState.DirtyFlags |= DIRTY_ATTRIBTYPES;
}


void bglDrawArrays(GPU_Primitive_t type, u32 numvertices)
{
	_bglUpdateState();
	
	GPUCMD_AddMaskedWrite(GPUREG_PRIMITIVE_CONFIG, 2, type);
	GPUCMD_AddMaskedWrite(GPUREG_RESTART_PRIMITIVE, 2, 0x00000001);
	
	GPUCMD_AddWrite(GPUREG_INDEXBUFFER_CONFIG, 0x80000000);
	
	GPUCMD_AddWrite(GPUREG_NUMVERTICES, numvertices);
	GPUCMD_AddWrite(GPUREG_VERTEX_OFFSET, 0);

	GPUCMD_AddMaskedWrite(GPUREG_GEOSTAGE_CONFIG2, 1, 0x00000001);

	GPUCMD_AddMaskedWrite(GPUREG_START_DRAW_FUNC0, 1, 0x00000000);
	GPUCMD_AddWrite(GPUREG_DRAWARRAYS, 0x00000001);
	GPUCMD_AddMaskedWrite(GPUREG_START_DRAW_FUNC0, 1, 0x00000001);
	GPUCMD_AddWrite(GPUREG_VTX_FUNC, 0x00000001);
	
	bglState.DrawnSomething = true;
}


void bglFlush()
{
	if (bglState.DrawnSomething)
	{
		GPUCMD_AddWrite(GPUREG_FRAMEBUFFER_FLUSH, 0x00000001);
		GPUCMD_AddWrite(GPUREG_FRAMEBUFFER_INVALIDATE, 0x00000001);
		GPUCMD_AddWrite(GPUREG_EARLYDEPTH_CLEAR, 0x00000001);
		
		bglState.DrawnSomething = false;
	}
	
	GPUCMD_Finalize();
	GPUCMD_Run();
	GPUCMD_SetBuffer(bglCommandBuffer, bglCommandBufferSize, 0);
}
