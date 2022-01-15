/*
    Copyright 2014 StapleButter

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
	
	
	// bit0: shader changed
	// bit1: buffer attributes changed
	// bit2: viewport/scissor changed
	// the rest: anything else changed
	u32 DirtyFlags;
	
	bool DrawnSomething;
	
} bglState;

u32 bglCommandBufferSize; // this size in words
void* bglCommandBuffer;



void bglInit()
{
	// hah, lazy
	memset(&bglState, 0, sizeof(bglState));
	bglState.DirtyFlags = 0xFFFFFFFF;
	
	bglCommandBufferSize = 0x80000;
	bglCommandBuffer = linearAlloc(bglCommandBufferSize * 4);
	
	//GPU_Reset(NULL, bglCommandBuffer, bglCommandBufferSize);
	//GPUCMD_SetBuffer(bglCommandBuffer, bglCommandBufferSize, 0);
	
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
	
	/*if (dirty & ~0x2)
	{
		dirty |= 0x2;
			
		if (dirty & 0x1)
			shaderProgramUse(bglState.Shader);
		
		if (dirty & 0x4)
		{
			GPU_SetViewport(bglState.DepthBuffer, bglState.ColorBuffer, bglState.ViewportX, bglState.ViewportY, bglState.ViewportW, bglState.ViewportH);
			GPU_SetScissorTest(bglState.ScissorMode, bglState.ScissorX, bglState.ScissorY, bglState.ScissorW, bglState.ScissorH);
		}

		GPU_DepthMap(bglState.DepthMin, bglState.DepthMax);
		GPU_SetFaceCulling(bglState.CullMode);
		GPU_SetStencilTest(bglState.StencilTest, bglState.StencilFunc, bglState.StencilRef, bglState.StencilMask, bglState.StencilReplace);
		GPU_SetStencilOp(bglState.StencilOpSFail, bglState.StencilOpDFail, bglState.StencilOpPass);
		GPU_SetBlendingColor(bglState.BlendingColor.R, bglState.BlendingColor.G, bglState.BlendingColor.B, bglState.BlendingColor.A);
		GPU_SetDepthTestAndWriteMask(bglState.DepthTest, bglState.DepthFunc, bglState.ColorDepthMask);
		
		// start drawing? whatever that junk is
		GPUCMD_AddMaskedWrite(GPUREG_0062, 0x1, 0);
		GPUCMD_AddWrite(GPUREG_0118, 0);
		
		GPU_SetAlphaBlending(
			bglState.ColorBlendEquation, bglState.AlphaBlendEquation,
			bglState.ColorSrcFactor, bglState.ColorDstFactor,
			bglState.AlphaSrcFactor, bglState.AlphaDstFactor);
		GPU_SetAlphaTest(bglState.AlphaTest, bglState.AlphaFunc, bglState.AlphaRef);
		
		GPU_SetTextureEnable(bglState.TextureEnable);
		
		for (i = 0; i < 6; i++)
		{
			GPU_SetTexEnv(i,
				bglState.TextureEnv[i].RGBSources,
				bglState.TextureEnv[i].AlphaSources,
				bglState.TextureEnv[i].RGBOperands,
				bglState.TextureEnv[i].AlphaOperands,
				bglState.TextureEnv[i].RGBCombine,
				bglState.TextureEnv[i].AlphaCombine,
				bglState.TextureEnv[i].ConstantColor.Val);
		}
		
		for (i = 0; i < 3; i++)
		{
			u32 texunit = 1<<i;
			
			if (!(bglState.TextureEnable & texunit))
				continue;
			
			GPU_SetTexture(texunit,
				bglState.Texture[i].Data,
				bglState.Texture[i].Height,
				bglState.Texture[i].Width,
				bglState.Texture[i].Parameters,
				bglState.Texture[i].ColorType);
		}
	}
	
	// attribute buffers
	// this lacks flexiblity
	if (dirty & 0x2)
	{
		u64 attrib = 0;
		u64 permut = 0;
		
		for (i = 0; i < bglState.NumAttribBuffers; i++)
		{
			attrib |= GPU_ATTRIBFMT(i, bglState.AttribBuffer[i].NumComponents, bglState.AttribBuffer[i].DataType);
			permut |= (i << (i*4));
		}
		
		GPU_SetAttributeBuffers(bglState.NumAttribBuffers,
			bglState.AttribBufferPtr,
			attrib, 
			0xFFC, // whatever that junk is-- may not be right in all cases
			permut,
			1,
			(u32[]){0}, // buffer offsets
			(u64[]){permut},
			(u8[]){bglState.NumAttribBuffers});
	}*/
}


void bglUseShader(shaderProgram_s* shader)
{
	bglState.Shader = shader;
	bglState.DirtyFlags |= 0x5;
}


void bglUniform(GPU_SHADER_TYPE type, u32 id, float* val)
{
	float pancake[4] = {val[3], val[2], val[1], val[0]};
	//GPU_SetFloatUniform(type, id, (u32*)pancake, 1);
}

void bglUniformMatrix(GPU_SHADER_TYPE type, u32 id, float* val)
{
	float pancake[16] = {
		val[3], val[2], val[1], val[0],
		val[7], val[6], val[5], val[4],
		val[11], val[10], val[9], val[8],
		val[15], val[14], val[13], val[12]
	};
	//GPU_SetFloatUniform(type, id, (u32*)pancake, 4);
}


void bglOutputBuffers(void* color, void* depth)
{
	bglState.ColorBuffer = (void*)osConvertVirtToPhys(color);
	bglState.DepthBuffer = (void*)osConvertVirtToPhys(depth);
	bglState.DirtyFlags |= 0x4;
}

void bglViewport(u32 x, u32 y, u32 w, u32 h)
{
	bglState.ViewportX = x;
	bglState.ViewportY = y;
	bglState.ViewportW = w;
	bglState.ViewportH = h;
	bglState.DirtyFlags |= 0x4;
}

void bglScissorMode(GPU_SCISSORMODE mode)
{
	bglState.ScissorMode = mode;
	bglState.DirtyFlags |= 0x4;
}

void bglScissor(u32 x, u32 y, u32 w, u32 h)
{
	bglState.ScissorX = x;
	bglState.ScissorY = y;
	bglState.ScissorW = w;
	bglState.ScissorH = h;
	bglState.DirtyFlags |= 0x4;
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
	bglState.DirtyFlags |= 0x20;
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
	
	bglState.Texture[id].Data = (void*)osConvertVirtToPhys(data);
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
	bglState.AttribBufferPtr = (void*)osConvertVirtToPhys(data);
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
	//GPU_DrawArray(type, numvertices);
	bglState.DrawnSomething = true;
}


void bglFlush()
{
	if (bglState.DrawnSomething)
	{
		//GPU_FinishDrawing();
		bglState.DrawnSomething = false;
	}
	
	/*GPUCMD_Finalize();
	GPUCMD_Run(NULL);
	GPUCMD_SetBuffer(bglCommandBuffer, bglCommandBufferSize, 0);*/
}
