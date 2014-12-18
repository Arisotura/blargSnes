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
	DVLB_s* Shader;
	
	
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
	
	bglCommandBufferSize = 0x40000;
	bglCommandBuffer = linearAlloc(bglCommandBufferSize * 4);
	
	GPU_Reset(NULL, bglCommandBuffer, bglCommandBufferSize);
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


// This viewport code taken from ctrulib, modified to allow setting the scissor coords without needing extra commands

// f32tof24() needs to be copied too, calling the ctrulib one doesn't work for whatever reason

u32 blarg_f32tof24(float f)
{
	if(!f)return 0;
	u32 v=*((u32*)&f);
	u8 s=v>>31;
	u32 exp=((v>>23)&0xFF)-0x40;
	u32 man=(v>>7)&0xFFFF;

	if(exp>=0)return man|(exp<<16)|(s<<23);
	else return s<<23;
}

u32 blarg_computeInvValue(u32 val)
{
	//usual values
	if(val==240)return 0x38111111;
	if(val==480)return 0x37111111;
	if(val==400)return 0x3747ae14;
	if (val == 256) return 0x38000000; // blargSNES specific
	
	//but let's not limit ourselves to the usual
	float fval=2.0/val;
	u32 tmp1,tmp2;
	u32 tmp3=*((u32*)&fval);
	tmp1=(tmp3<<9)>>9;
	tmp2=tmp3&(~0x80000000);
	if(tmp2)
	{
		tmp1=(tmp3<<9)>>9;
		int tmp=((tmp3<<1)>>24)-0x40;
		if(tmp<0)return ((tmp3>>31)<<30)<<1;
		else tmp2=tmp;
	}
	tmp3>>=31;
	return (tmp1|(tmp2<<23)|(tmp3<<30))<<1;
}

//takes PAs as arguments
void blarg_GPU_SetViewport()
{
	u32 param[0x4];
	float fw=(float)bglState.ViewportW;
	float fh=(float)bglState.ViewportH;

	// CHECKME: is that junk required here?
	GPUCMD_AddSingleParam(0x000F0111, 0x00000001);
	GPUCMD_AddSingleParam(0x000F0110, 0x00000001);

	u32 f116e=0x01000000|(((bglState.ViewportH-1)&0xFFF)<<12)|(bglState.ViewportW&0xFFF);

	param[0x0]=((u32)bglState.DepthBuffer)>>3;
	param[0x1]=((u32)bglState.ColorBuffer)>>3;
	param[0x2]=f116e;
	GPUCMD_Add(0x800F011C, param, 0x00000003);

	GPUCMD_AddSingleParam(0x000F006E, f116e);
	GPUCMD_AddSingleParam(0x000F0116, 0x00000003); //depth buffer format
	GPUCMD_AddSingleParam(0x000F0117, 0x00000002); //color buffer format
	GPUCMD_AddSingleParam(0x000F011B, 0x00000000); //?

	param[0x0]=blarg_f32tof24(fw/2);
	param[0x1]=blarg_computeInvValue(fw);
	param[0x2]=blarg_f32tof24(fh/2);
	param[0x3]=blarg_computeInvValue(fh);
	GPUCMD_Add(0x800F0041, param, 0x00000004);

	GPUCMD_AddSingleParam(0x000F0068, (bglState.ViewportY<<16)|(bglState.ViewportX&0xFFFF));

	param[0x0]=bglState.ScissorMode;
	param[0x1]=(bglState.ScissorY << 16) | (bglState.ScissorX & 0xFFFF);
	param[0x2]=((bglState.ScissorH-1)<<16)|((bglState.ScissorW-1)&0xFFFF);
	GPUCMD_Add(0x800F0065, param, 0x00000003);

	//enable depth buffer
	param[0x0]=0x0000000F;
	param[0x1]=0x0000000F;
	param[0x2]=0x00000002;
	param[0x3]=0x00000002;
	GPUCMD_Add(0x800F0112, param, 0x00000004);
}

// type: 0=vsh, 1=gsh
void blarg_DVLP_SendCode(DVLP_s* dvlp, int type)
{
	if(!dvlp)return;
	
	u32 offset = type ? 0 : 0x30;

	GPUCMD_AddSingleParam(0x000F029B+offset, 0x00000000);

	int i;
	for(i=0;i<dvlp->codeSize;i+=0x80)
		GPUCMD_Add(0x000F029C+offset, &dvlp->codeData[i], ((dvlp->codeSize-i)<0x80)?(dvlp->codeSize-i):0x80);

	GPUCMD_AddSingleParam(0x000F028F+offset, 0x00000001);
}

void blarg_DVLP_SendOpDesc(DVLP_s* dvlp, int type)
{
	if(!dvlp)return;
	
	u32 offset = type ? 0 : 0x30;

	GPUCMD_AddSingleParam(0x000F02A5+offset, 0x00000000);

	u32 param[0x20];

	int i;
	//TODO : should probably preprocess this
	for(i=0;i<dvlp->opdescSize;i++)
		param[i]=dvlp->opcdescData[i*2];

	GPUCMD_Add(0x000F02A6+offset, param, dvlp->opdescSize);
}

void blarg_DVLE_SendOutmap(DVLE_s* dvle)
{
	if(!dvle)return;
	
	u32 offset = dvle->type ? 0 : 0x30;

	u32 param[0x7]={0x1F1F1F1F,0x1F1F1F1F,0x1F1F1F1F,0x1F1F1F1F,
					0x1F1F1F1F,0x1F1F1F1F,0x1F1F1F1F};

	int i;
	u8 numAttr=0;
	u8 maxAttr=0;
	u8 attrMask=0;
	//TODO : should probably preprocess this
	for(i=0;i<dvle->outTableSize;i++)
	{
		u32* out=&param[dvle->outTableData[i].regID];
		
		if(*out==0x1F1F1F1F)numAttr++;

		//desc could include masking/swizzling info not currently taken into account
		//also TODO : map out other output register values
		switch(dvle->outTableData[i].type)
		{
			case RESULT_POSITION: *out=0x03020100; break;
			case RESULT_COLOR: *out=0x0B0A0908; break;
			case RESULT_TEXCOORD0: *out=0x1F1F0D0C; break;
			case RESULT_TEXCOORD1: *out=0x1F1F0F0E; break;
			case RESULT_TEXCOORD2: *out=0x1F1F1716; break;
		}

		attrMask|=1<<dvle->outTableData[i].regID;
		if(dvle->outTableData[i].regID+1>maxAttr)maxAttr=dvle->outTableData[i].regID+1;
	}

	//GPUCMD_AddSingleParam(0x000F0251, numAttr-1); //?
	//GPUCMD_AddSingleParam(0x000F024A, numAttr-1); //?
	GPUCMD_AddSingleParam(0x000F028D+offset, attrMask); //?
	GPUCMD_AddSingleParam(0x0001025E, numAttr-1); //?
	GPUCMD_AddSingleParam(0x000F004F, numAttr); //?
	GPUCMD_Add(0x800F0050, param, 0x00000007);
}

void blarg_DVLE_SendConstants(DVLE_s* dvle)
{
	if(!dvle)return;
	
	u32 offset = dvle->type ? 0 : 0x30;

	u32 param[4];
	u32 rev[3];
	u8* rev8=(u8*)rev;

	int i;
	DVLE_constEntry_s* cnst=dvle->constTableData;
	for(i=0;i<dvle->constTableSize;i++,cnst++)
	{
		memcpy(&rev8[0], &cnst->data[0], 3);
		memcpy(&rev8[3], &cnst->data[1], 3);
		memcpy(&rev8[6], &cnst->data[2], 3);
		memcpy(&rev8[9], &cnst->data[3], 3);

		param[0x0]=(cnst->header>>16)&0xFF;
		param[0x1]=rev[2];
		param[0x2]=rev[1];
		param[0x3]=rev[0];

		GPUCMD_Add(0x800F0290+offset, param, 0x00000004);
	}
}

// omg geometry shader
// thanks to smealum :D
void blarg_SHDR_UseProgram(DVLB_s* dvlb, u32 vsh_id, u32 gsh_id, u32 geo_stride, u32 attr_mask)
{
	// vertex shader
	
	DVLE_s* dvle = &dvlb->DVLE[vsh_id];
	int i;

	//?
	GPUCMD_AddSingleParam(0x00010229, 0x00000000);
	GPUCMD_AddSingleParam(0x00010244, 0x00000000);

	blarg_DVLP_SendCode(&dvlb->DVLP, dvle->type);
	blarg_DVLP_SendOpDesc(&dvlb->DVLP, dvle->type);
	blarg_DVLE_SendConstants(dvle);

	GPUCMD_AddSingleParam(0x00080229, 0x00000000);
	GPUCMD_AddSingleParam(0x000F02BA, 0x7FFF0000|(dvle->mainOffset&0xFFFF)); //set entrypoint

	GPUCMD_AddSingleParam(0x000F0252, 0x00000000); // should all be part of DVLE_SendOutmap ?

	//blarg_DVLE_SendOutmap(dvle);
	u32 num_attr = 0; u32 temp = attr_mask;
	for (i = 0; i < 16; i++)
	{
		if (!temp) break;
		if (temp & 1) num_attr++;
		temp >>= 1;
	}
	GPUCMD_AddSingleParam(0x000F0251, num_attr-1);
	GPUCMD_AddSingleParam(0x000F024A, num_attr-1);
	GPUCMD_AddSingleParam(0x000F02BD, attr_mask);

	//?
	GPUCMD_AddSingleParam(0x000F0064, 0x00000001);
	GPUCMD_AddSingleParam(0x000F006F, 0x00000703);
	
	// geometry shader
	
	dvle = &dvlb->DVLE[gsh_id];

	blarg_DVLP_SendCode(&dvlb->DVLP, dvle->type);
	blarg_DVLP_SendOpDesc(&dvlb->DVLP, dvle->type);
	blarg_DVLE_SendConstants(dvle);

	blarg_DVLE_SendOutmap(dvle);

	GPUCMD_AddSingleParam(0x00010229, 0x00000002);
	GPUCMD_AddSingleParam(0x00010244, 0x00000001); //not necessary ?

	GPUCMD_AddSingleParam(0x00090289, 0x08000000|(geo_stride-1));
	GPUCMD_AddSingleParam(0x000F028A, 0x7FFF0000|(dvle->mainOffset&0xFFFF)); //set entrypoint

	// GPUCMD_AddSingleParam(0x000F0064, 0x00000001); //not necessary ?
	// GPUCMD_AddSingleParam(0x000F006F, 0x01030703); //not necessary ?

	u32 param[] = {0x76543210, 0xFEDCBA98};
	GPUCMD_Add(0x800F028B, param, 0x00000002);
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
	
	if (dirty & ~0x2)
	{
		dirty |= 0x2;
			
		if (dirty & 0x1)
			blarg_SHDR_UseProgram(bglState.Shader, 0, 1, bglState.GeometryStride, bglState.ShaderAttrMask);
		
		if (dirty & 0x4)
			blarg_GPU_SetViewport();
		
		GPU_DepthRange(bglState.DepthMin, bglState.DepthMax);
		
		GPU_SetFaceCulling(bglState.CullMode);
		
		GPU_SetStencilTest(bglState.StencilTest, bglState.StencilFunc, bglState.StencilRef, bglState.StencilMask, bglState.StencilReplace);
		GPU_SetStencilOp(bglState.StencilOpSFail, bglState.StencilOpDFail, bglState.StencilOpPass);
		
		GPU_SetBlendingColor(bglState.BlendingColor.R, bglState.BlendingColor.G, bglState.BlendingColor.B, bglState.BlendingColor.A);
		
		GPU_SetDepthTestAndWriteMask(bglState.DepthTest, bglState.DepthFunc, bglState.ColorDepthMask);
		
		// start drawing? whatever that junk is
		GPUCMD_AddSingleParam(0x00010062, 0);
		GPUCMD_AddSingleParam(0x000F0118, 0);
		
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
	}
}


void bglGeometryShaderParams(u32 stride, u32 attrmask)
{
	// those depend on the shader used
	// TODO eventually embed this information in the shader?
	
	bglState.GeometryStride = stride;
	bglState.ShaderAttrMask = attrmask;
	bglState.DirtyFlags |= 0x5;
}

void bglUseShader(DVLB_s* shader)
{
	bglState.Shader = shader;
	bglState.DirtyFlags |= 0x5;
}

void bglUniform(u32 id, float* val)
{
	float pancake[4] = {val[3], val[2], val[1], val[0]};
	GPU_SetUniform(id, (u32*)pancake, 1);
}

void bglUniformMatrix(u32 id, float* val)
{
	float pancake[16] = {
		val[3], val[2], val[1], val[0],
		val[7], val[6], val[5], val[4],
		val[11], val[10], val[9], val[8],
		val[15], val[14], val[13], val[12]
	};
	GPU_SetUniform(id, (u32*)pancake, 4);
}


void bglOutputBuffers(void* color, void* depth)
{
	bglState.ColorBuffer = (void*)osConvertVirtToPhys((u32)color);
	bglState.DepthBuffer = (void*)osConvertVirtToPhys((u32)depth);
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
	
	bglState.Texture[id].Data = (void*)osConvertVirtToPhys((u32)data);
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
	bglState.AttribBufferPtr = (void*)osConvertVirtToPhys((u32)data);
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
	GPU_DrawArray(type, numvertices);
	bglState.DrawnSomething = true;
}


void bglFlush()
{
	if (bglState.DrawnSomething)
	{
		GPU_FinishDrawing();
		bglState.DrawnSomething = false;
	}
	
	GPUCMD_Finalize();
	GPUCMD_Run(NULL);
	GPUCMD_SetBuffer(bglCommandBuffer, bglCommandBufferSize, 0);
}
