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

#include "snes.h"
#include "ppu.h"



#define COLMATH(n) ((colormath & (1<<n)) ? 1:0)
#define COLMATH_OBJ ((colormath & 0x80) ? 0xFF:(colormath&0x40))


void PPU_RenderTile_2bpp(u16 curtile, u16 tilepixels, u16* buffer, u16* pal, u32 alpha, int start, int end)
{
	u32 idx, colorval;
	
	if (curtile & 0x4000)
	{
		tilepixels >>= start;
		
		for (idx = 0; idx < end; idx++)
		{
			colorval = 0;
			if (tilepixels & 0x0001) colorval |= 0x01;
			if (tilepixels & 0x0100) colorval |= 0x02;
			tilepixels >>= 1;
			
			if (colorval)
				buffer[idx] = pal[colorval] | alpha;
		}
	}
	else
	{
		tilepixels <<= start;
		
		for (idx = 0; idx < end; idx++)
		{
			colorval = 0;
			if (tilepixels & 0x0080) colorval |= 0x01;
			if (tilepixels & 0x8000) colorval |= 0x02;
			tilepixels <<= 1;
			
			if (colorval)
				buffer[idx] = pal[colorval] | alpha;
		}
	}
}

void PPU_RenderTile_4bpp(u16 curtile, u32 tilepixels, u16* buffer, u16* pal, u32 alpha, int start, int end)
{
	u32 idx, colorval;
	
	if (curtile & 0x4000)
	{
		tilepixels >>= start;
		
		for (idx = 0; idx < end; idx++)
		{
			colorval = 0;
			if (tilepixels & 0x00000001) colorval |= 0x01;
			if (tilepixels & 0x00000100) colorval |= 0x02;
			if (tilepixels & 0x00010000) colorval |= 0x04;
			if (tilepixels & 0x01000000) colorval |= 0x08;
			tilepixels >>= 1;
			
			if (colorval)
				buffer[idx] = pal[colorval] | alpha;
		}
	}
	else
	{
		tilepixels <<= start;
		
		for (idx = 0; idx < end; idx++)
		{
			colorval = 0;
			if (tilepixels & 0x00000080) colorval |= 0x01;
			if (tilepixels & 0x00008000) colorval |= 0x02;
			if (tilepixels & 0x00800000) colorval |= 0x04;
			if (tilepixels & 0x80000000) colorval |= 0x08;
			tilepixels <<= 1;
			
			if (colorval)
				buffer[idx] = pal[colorval] | alpha;
		}
	}
}

void PPU_RenderTile_8bpp(u16 curtile, u32 tilepixels1, u32 tilepixels2, u16* buffer, u16* pal, u32 alpha, int start, int end)
{
	u32 idx, colorval;
	
	if (curtile & 0x4000)
	{
		tilepixels1 >>= start;
		tilepixels2 >>= start;
		
		for (idx = 0; idx < end; idx++)
		{
			colorval = 0;
			if (tilepixels1 & 0x00000001) colorval |= 0x01;
			if (tilepixels1 & 0x00000100) colorval |= 0x02;
			if (tilepixels1 & 0x00010000) colorval |= 0x04;
			if (tilepixels1 & 0x01000000) colorval |= 0x08;
			if (tilepixels2 & 0x00000001) colorval |= 0x10;
			if (tilepixels2 & 0x00000100) colorval |= 0x20;
			if (tilepixels2 & 0x00010000) colorval |= 0x40;
			if (tilepixels2 & 0x01000000) colorval |= 0x80;
			tilepixels1 >>= 1;
			tilepixels2 >>= 1;
			
			if (colorval)
				buffer[idx] = pal[colorval] | alpha;
		}
	}
	else
	{
		tilepixels1 <<= start;
		tilepixels2 <<= start;
		
		for (idx = 0; idx < end; idx++)
		{
			colorval = 0;
			if (tilepixels1 & 0x00000080) colorval |= 0x01;
			if (tilepixels1 & 0x00008000) colorval |= 0x02;
			if (tilepixels1 & 0x00800000) colorval |= 0x04;
			if (tilepixels1 & 0x80000000) colorval |= 0x08;
			if (tilepixels2 & 0x00000080) colorval |= 0x10;
			if (tilepixels2 & 0x00008000) colorval |= 0x20;
			if (tilepixels2 & 0x00800000) colorval |= 0x40;
			if (tilepixels2 & 0x80000000) colorval |= 0x80;
			tilepixels1 <<= 1;
			tilepixels2 <<= 1;
			
			if (colorval)
				buffer[idx] = pal[colorval] | alpha;
		}
	}
}

void PPU_RenderTile_OBJ(u16 attrib, u32 tilepixels, u16* buffer, u16 paloffset_prio)
{
	if (!tilepixels) return;
	u32 idx, colorval;
	
	if (attrib & 0x4000)
	{
		for (idx = 0; idx < 8; idx++)
		{
			colorval = 0;
			if (tilepixels & 0x00000001) colorval |= 0x01;
			if (tilepixels & 0x00000100) colorval |= 0x02;
			if (tilepixels & 0x00010000) colorval |= 0x04;
			if (tilepixels & 0x01000000) colorval |= 0x08;
			tilepixels >>= 1;
			
			if (colorval)
				buffer[idx] = colorval | paloffset_prio;
		}
	}
	else
	{
		for (idx = 0; idx < 8; idx++)
		{
			colorval = 0;
			if (tilepixels & 0x00000080) colorval |= 0x01;
			if (tilepixels & 0x00008000) colorval |= 0x02;
			if (tilepixels & 0x00800000) colorval |= 0x04;
			if (tilepixels & 0x80000000) colorval |= 0x08;
			tilepixels <<= 1;
			
			if (colorval)
				buffer[idx] = colorval | paloffset_prio;
		}
	}
}

inline void PPU_DeferTile(PPU_Background* bg, u16* buffer, u16 attrib, u16* pixels, u32 alpha, int start, int end)
{
	PPU_DeferredTile* deftile = &(bg->DeferredTiles[bg->NumDeferredTiles]);
	deftile->Dest = buffer;
	deftile->SrcPixels = pixels;
	deftile->Attrib = attrib;
	deftile->Alpha = (u8)alpha;
	deftile->Start = (u16)start;
	deftile->End = (u16)end;
	bg->NumDeferredTiles++;
}


void PPU_RenderBG_2bpp_8x8(PPU_Background* bg, u16* buffer, u32 line, u16* pal, u32 alpha, u32 window)
{
	u16* tileset = bg->Tileset;
	u16* tilemap = bg->Tilemap;
	u32 xoff;
	u32 tiley;
	u16 curtile;
	u32 tilepixels;
	s32 i;
	u32 idx;
	
	line += bg->YScroll;
	tiley = (line & 0x07);
	tilemap += (line & 0xF8) << 2;
	if (line & 0x100)
	{
		if (bg->Size & 0x2)
			tilemap += (bg->Size & 0x1) ? 2048 : 1024;
	}
	
	xoff = bg->XScroll;
	i = 0;
	
	PPU_WindowSegment* s = &PPU.Window[0];
	for (;;)
	{
		int remaining = s->EndOffset - i;
		u16 hidden = window ? (bg->WindowCombine & (1 << (s->WindowMask ^ bg->WindowMask))) : 0;
		
		if (hidden)
		{
			i += remaining;
			xoff += remaining;
		}
		else
		{
			u32 finalalpha = (PPU.ColorMath1 & s->ColorMath) ? 0:alpha;
			
			while (remaining > 0)
			{
				int start = xoff & 7;
				int end = 8 - start;
				if (remaining < end) end = remaining;
				remaining -= end;
				
				idx = (xoff & 0xF8) >> 3;
				if (xoff & 0x100)
				{
					if (bg->Size & 0x1)
						idx += 1024;
				}
				xoff += end;
				curtile = tilemap[idx];
				
				idx = (curtile & 0x3FF) << 3;
				if (curtile & 0x8000) 	idx += (7 - tiley);
				else					idx += tiley;
				
				if (curtile & 0x2000) // high prio
				{
					PPU_DeferTile(bg, &buffer[i], curtile, &tileset[idx], finalalpha, start, end);
				}
				else
				{
					tilepixels = tileset[idx];
					if (tilepixels)
						PPU_RenderTile_2bpp(curtile, tilepixels, &buffer[i], &pal[(curtile & 0x1C00) >> 8], finalalpha, start, end);
				}
				
				i += end;
			}
		}
		
		if (s->EndOffset >= 256) return;
		s++;
	}
}

void PPU_RenderBG_2bpp_16x16(PPU_Background* bg, u16* buffer, u32 line, u16* pal, u32 alpha, u32 window)
{
	u16* tileset = bg->Tileset;
	u16* tilemap = bg->Tilemap;
	u32 xoff;
	u32 tiley;
	u16 curtile;
	u32 tilepixels;
	s32 i;
	u32 idx;
	
	line += bg->YScroll;
	tiley = (line & 0x07);
	tilemap += (line & 0x1F0) << 1;
	if (line & 0x200)
	{
		if (bg->Size & 0x2)
			tilemap += (bg->Size & 0x1) ? 2048 : 1024;
	}
	
	xoff = bg->XScroll;
	i = 0;
	
	PPU_WindowSegment* s = &PPU.Window[0];
	for (;;)
	{
		int remaining = s->EndOffset - i;
		u16 hidden = window ? (bg->WindowCombine & (1 << (s->WindowMask ^ bg->WindowMask))) : 0;
		
		if (hidden)
		{
			i += remaining;
			xoff += remaining;
		}
		else
		{
			u32 finalalpha = (PPU.ColorMath1 & s->ColorMath) ? 0:alpha;
			
			while (remaining > 0)
			{
				int start = xoff & 15;
				int end = 16 - start;
				if (remaining < end) end = remaining;
				remaining -= end;
				
				idx = (xoff & 0x1F0) >> 4;
				if (xoff & 0x200)
				{
					if (bg->Size & 0x1)
						idx += 1024;
				}
				xoff += end;
				curtile = tilemap[idx];
				
				idx = (curtile & 0x3FF) << 3;
				if (curtile & 0x8000) 
				{
					idx += (7 - tiley);
					if (!(line & 0x08)) idx += 128;
				}
				else
				{
					idx += tiley;
					if (line & 0x08) idx += 128;
				}
				
				if (curtile & 0x4000) idx += 8;
				
				if (curtile & 0x2000) // high prio
				{
					if (start < 8)
					{
						int end1 = 8-start;
						if (end1 > end) end1 = end;
						PPU_DeferTile(bg, &buffer[i], curtile, &tileset[idx], finalalpha, start, end1);
						
						start = 0;
						end -= end1;
						i += end1;
					}
					else
						start -= 8;
					
					if (end > 0)
					{
						if (curtile & 0x4000) idx -= 8;
						else                  idx += 8;
						
						PPU_DeferTile(bg, &buffer[i], curtile, &tileset[idx], finalalpha, start, end);
						i += end;
					}
				}
				else
				{
					if (start < 8)
					{
						int end1 = 8-start;
						if (end1 > end) end1 = end;
						
						tilepixels = tileset[idx];
						if (tilepixels)
							PPU_RenderTile_2bpp(curtile, tilepixels, &buffer[i], &pal[(curtile & 0x1C00) >> 8], finalalpha, start, end1);
						
						start = 0;
						end -= end1;
						i += end1;
					}
					else
						start -= 8;
					
					if (end > 0)
					{
						if (curtile & 0x4000) idx -= 8;
						else                  idx += 8;
						
						tilepixels = tileset[idx];
						if (tilepixels)
							PPU_RenderTile_2bpp(curtile, tilepixels, &buffer[i], &pal[(curtile & 0x1C00) >> 8], finalalpha, start, end);
							
						i += end;
					}
				}
			}
		}
		
		if (s->EndOffset >= 256) return;
		s++;
	}
}

void PPU_RenderBG_4bpp_8x8(PPU_Background* bg, u16* buffer, u32 line, u16* pal, u32 alpha, u32 window)
{
	u16* tileset = bg->Tileset;
	u16* tilemap = bg->Tilemap;
	u32 xoff;
	u32 tiley;
	u16 curtile;
	u32 tilepixels;
	s32 i;
	u32 idx;
	
	line += bg->YScroll;
	tiley = (line & 0x07);
	tilemap += (line & 0xF8) << 2;
	if (line & 0x100)
	{
		if (bg->Size & 0x2)
			tilemap += (bg->Size & 0x1) ? 2048 : 1024;
	}
	
	xoff = bg->XScroll;
	i = 0;
	
	PPU_WindowSegment* s = &PPU.Window[0];
	for (;;)
	{
		int remaining = s->EndOffset - i;
		u16 hidden = window ? (bg->WindowCombine & (1 << (s->WindowMask ^ bg->WindowMask))) : 0;
		
		if (hidden)
		{
			i += remaining;
			xoff += remaining;
		}
		else
		{
			u32 finalalpha = (PPU.ColorMath1 & s->ColorMath) ? 0:alpha;
			
			while (remaining > 0)
			{
				int start = xoff & 7;
				int end = 8 - start;
				if (remaining < end) end = remaining;
				remaining -= end;
				
				idx = (xoff & 0xF8) >> 3;
				if (xoff & 0x100)
				{
					if (bg->Size & 0x1)
						idx += 1024;
				}
				xoff += end;
				curtile = tilemap[idx];
				
				idx = (curtile & 0x3FF) << 4;
				if (curtile & 0x8000) 	idx += (7 - tiley);
				else					idx += tiley;
				
				if (curtile & 0x2000) // high prio
				{
					PPU_DeferTile(bg, &buffer[i], curtile, &tileset[idx], finalalpha, start, end);
				}
				else
				{
					tilepixels = tileset[idx] | (tileset[idx+8] << 16);
					if (tilepixels)
						PPU_RenderTile_4bpp(curtile, tilepixels, &buffer[i], &pal[(curtile & 0x1C00) >> 6], finalalpha, start, end);
				}
				
				i += end;
			}
		}
		
		if (s->EndOffset >= 256) return;
		s++;
	}
}

void PPU_RenderBG_4bpp_16x16(PPU_Background* bg, u16* buffer, u32 line, u16* pal, u32 alpha, u32 window)
{
	u16* tileset = bg->Tileset;
	u16* tilemap = bg->Tilemap;
	u32 xoff;
	u32 tiley;
	u16 curtile;
	u32 tilepixels;
	s32 i;
	u32 idx;
	
	line += bg->YScroll;
	tiley = (line & 0x07);
	tilemap += (line & 0x1F0) << 1;
	if (line & 0x200)
	{
		if (bg->Size & 0x2)
			tilemap += (bg->Size & 0x1) ? 2048 : 1024;
	}
	
	xoff = bg->XScroll;
	i = 0;
	
	PPU_WindowSegment* s = &PPU.Window[0];
	for (;;)
	{
		int remaining = s->EndOffset - i;
		u16 hidden = window ? (bg->WindowCombine & (1 << (s->WindowMask ^ bg->WindowMask))) : 0;
		
		if (hidden)
		{
			i += remaining;
			xoff += remaining;
		}
		else
		{
			u32 finalalpha = (PPU.ColorMath1 & s->ColorMath) ? 0:alpha;
			
			while (remaining > 0)
			{
				int start = xoff & 15;
				int end = 16 - start;
				if (remaining < end) end = remaining;
				remaining -= end;
				
				idx = (xoff & 0x1F0) >> 4;
				if (xoff & 0x200)
				{
					if (bg->Size & 0x1)
						idx += 1024;
				}
				xoff += end;
				curtile = tilemap[idx];
				
				idx = (curtile & 0x3FF) << 4;
				if (curtile & 0x8000) 
				{
					idx += (7 - tiley);
					if (!(line & 0x08)) idx += 256;
				}
				else
				{
					idx += tiley;
					if (line & 0x08) idx += 256;
				}
				
				if (curtile & 0x4000) idx += 16;
				
				if (curtile & 0x2000) // high prio
				{
					if (start < 8)
					{
						int end1 = 8-start;
						if (end1 > end) end1 = end;
						PPU_DeferTile(bg, &buffer[i], curtile, &tileset[idx], finalalpha, start, end1);
						
						start = 0;
						end -= end1;
						i += end1;
					}
					else
						start -= 8;
					
					if (end > 0)
					{
						if (curtile & 0x4000) idx -= 16;
						else                  idx += 16;
						
						PPU_DeferTile(bg, &buffer[i], curtile, &tileset[idx], finalalpha, start, end);
						i += end;
					}
				}
				else
				{
					if (start < 8)
					{
						int end1 = 8-start;
						if (end1 > end) end1 = end;
						
						tilepixels = tileset[idx] | (tileset[idx+8] << 16);
						if (tilepixels)
							PPU_RenderTile_4bpp(curtile, tilepixels, &buffer[i], &pal[(curtile & 0x1C00) >> 6], finalalpha, start, end1);
						
						start = 0;
						end -= end1;
						i += end1;
					}
					else
						start -= 8;
					
					if (end > 0)
					{
						if (curtile & 0x4000) idx -= 16;
						else                  idx += 16;
						
						tilepixels = tileset[idx] | (tileset[idx+8] << 16);
						if (tilepixels)
							PPU_RenderTile_4bpp(curtile, tilepixels, &buffer[i], &pal[(curtile & 0x1C00) >> 6], finalalpha, start, end);
							
						i += end;
					}
				}
			}
		}
		
		if (s->EndOffset >= 256) return;
		s++;
	}
}

void PPU_RenderBG_8bpp_8x8(PPU_Background* bg, u16* buffer, u32 line, u16* pal, u32 alpha, u32 window)
{
	u16* tileset = bg->Tileset;
	u16* tilemap = bg->Tilemap;
	u32 xoff;
	u32 tiley;
	u16 curtile;
	u32 tilepixels1, tilepixels2;
	s32 i;
	u32 idx;
	
	line += bg->YScroll;
	tiley = (line & 0x07);
	tilemap += (line & 0xF8) << 2;
	if (line & 0x100)
	{
		if (bg->Size & 0x2)
			tilemap += (bg->Size & 0x1) ? 2048 : 1024;
	}
	
	xoff = bg->XScroll;
	i = 0;
	
	PPU_WindowSegment* s = &PPU.Window[0];
	for (;;)
	{
		int remaining = s->EndOffset - i;
		u16 hidden = window ? (bg->WindowCombine & (1 << (s->WindowMask ^ bg->WindowMask))) : 0;
		
		if (hidden)
		{
			i += remaining;
			xoff += remaining;
		}
		else
		{
			u32 finalalpha = (PPU.ColorMath1 & s->ColorMath) ? 0:alpha;
			
			while (remaining > 0)
			{
				int start = xoff & 7;
				int end = 8 - start;
				if (remaining < end) end = remaining;
				remaining -= end;
				
				idx = (xoff & 0xF8) >> 3;
				if (xoff & 0x100)
				{
					if (bg->Size & 0x1)
						idx += 1024;
				}
				xoff += end;
				curtile = tilemap[idx];
				
				idx = (curtile & 0x3FF) << 5;
				if (curtile & 0x8000) 	idx += (7 - tiley);
				else					idx += tiley;
				
				if (curtile & 0x2000) // high prio
				{
					PPU_DeferTile(bg, &buffer[i], curtile, &tileset[idx], finalalpha, start, end);
				}
				else
				{
					// TODO: palette offset for direct color
					
					tilepixels1 = tileset[idx] | (tileset[idx+8] << 16);
					tilepixels2 = tileset[idx+16] | (tileset[idx+24] << 16);
					
					if (tilepixels1|tilepixels2)
						PPU_RenderTile_8bpp(curtile, tilepixels1, tilepixels2, &buffer[i], pal, finalalpha, start, end);
				}
				
				i += end;
			}
		}
		
		if (s->EndOffset >= 256) return;
		s++;
	}
}

void PPU_RenderBG_8bpp_16x16(PPU_Background* bg, u16* buffer, u32 line, u16* pal, u32 alpha, u32 window)
{
	u16* tileset = bg->Tileset;
	u16* tilemap = bg->Tilemap;
	u32 xoff;
	u32 tiley;
	u16 curtile;
	u32 tilepixels1, tilepixels2;
	s32 i;
	u32 idx;
	
	line += bg->YScroll;
	tiley = (line & 0x07);
	tilemap += (line & 0x1F0) << 1;
	if (line & 0x200)
	{
		if (bg->Size & 0x2)
			tilemap += (bg->Size & 0x1) ? 2048 : 1024;
	}
	
	xoff = bg->XScroll;
	i = 0;
	
	PPU_WindowSegment* s = &PPU.Window[0];
	for (;;)
	{
		int remaining = s->EndOffset - i;
		u16 hidden = window ? (bg->WindowCombine & (1 << (s->WindowMask ^ bg->WindowMask))) : 0;
		
		if (hidden)
		{
			i += remaining;
			xoff += remaining;
		}
		else
		{
			u32 finalalpha = (PPU.ColorMath1 & s->ColorMath) ? 0:alpha;
			
			while (remaining > 0)
			{
				int start = xoff & 15;
				int end = 16 - start;
				if (remaining < end) end = remaining;
				remaining -= end;
				
				idx = (xoff & 0x1F0) >> 4;
				if (xoff & 0x200)
				{
					if (bg->Size & 0x1)
						idx += 1024;
				}
				xoff += end;
				curtile = tilemap[idx];
				
				idx = (curtile & 0x3FF) << 5;
				if (curtile & 0x8000) 
				{
					idx += (7 - tiley);
					if (!(line & 0x08)) idx += 512;
				}
				else
				{
					idx += tiley;
					if (line & 0x08) idx += 512;
				}
				
				if (curtile & 0x4000) idx += 32;
				
				if (curtile & 0x2000) // high prio
				{
					if (start < 8)
					{
						int end1 = 8-start;
						if (end1 > end) end1 = end;
						PPU_DeferTile(bg, &buffer[i], curtile, &tileset[idx], finalalpha, start, end1);
						
						start = 0;
						end -= end1;
						i += end1;
					}
					else
						start -= 8;
					
					if (end > 0)
					{
						if (curtile & 0x4000) idx -= 32;
						else                  idx += 32;
						
						PPU_DeferTile(bg, &buffer[i], curtile, &tileset[idx], finalalpha, start, end);
						i += end;
					}
				}
				else
				{
					// TODO: palette offset for direct color
					
					if (start < 8)
					{
						int end1 = 8-start;
						if (end1 > end) end1 = end;
						
						tilepixels1 = tileset[idx] | (tileset[idx+8] << 16);
						tilepixels2 = tileset[idx+16] | (tileset[idx+24] << 16);
						
						if (tilepixels1|tilepixels2)
							PPU_RenderTile_8bpp(curtile, tilepixels1, tilepixels2, &buffer[i], pal, finalalpha, start, end1);
						
						start = 0;
						end -= end1;
						i += end1;
					}
					else
						start -= 8;
					
					if (end > 0)
					{
						if (curtile & 0x4000) idx -= 32;
						else                  idx += 32;
						
						tilepixels1 = tileset[idx] | (tileset[idx+8] << 16);
						tilepixels2 = tileset[idx+16] | (tileset[idx+24] << 16);
						
						if (tilepixels1|tilepixels2)
							PPU_RenderTile_8bpp(curtile, tilepixels1, tilepixels2, &buffer[i], pal, finalalpha, start, end);
							
						i += end;
					}
				}
			}
		}
		
		if (s->EndOffset >= 256) return;
		s++;
	}
}


void PPU_RenderBG_Mode7(u16* buffer, u32 line, u16* pal, u32 alpha, u32 window)
{
	s32 x = (PPU.M7A * (PPU.M7XScroll-PPU.M7RefX)) + (PPU.M7B * (line+PPU.M7YScroll-PPU.M7RefY)) + (PPU.M7RefX << 8);
	s32 y = (PPU.M7C * (PPU.M7XScroll-PPU.M7RefX)) + (PPU.M7D * (line+PPU.M7YScroll-PPU.M7RefY)) + (PPU.M7RefY << 8);
	int i = 0;
	u32 tileidx;
	u8 colorval;
	
	PPU_WindowSegment* s = &PPU.Window[0];
	for (;;)
	{
		u16 hidden = window ? (PPU.BG[0].WindowCombine & (1 << (s->WindowMask ^ PPU.BG[0].WindowMask))) : 0;
		
		if (hidden)
		{
			int remaining = s->EndOffset - i;
			i += remaining;
			x += remaining*PPU.M7A;
			y += remaining*PPU.M7C;
		}
		else
		{
			u32 finalalpha = (PPU.ColorMath1 & s->ColorMath) ? 0:alpha;
			int end = s->EndOffset;
			
			while (i < end)
			{
				// TODO screen h/v flip
				
				if ((x|y) & 0xFFFC0000)
				{
					// wraparound
					if ((PPU.M7Sel & 0xC0) == 0x40)
					{
						// skip pixel (transparent)
						i++;
						x += PPU.M7A;
						y += PPU.M7C;
						continue;
					}
					else if ((PPU.M7Sel & 0xC0) == 0xC0)
					{
						// use tile 0
						tileidx = 0;
					}
					else
					{
						// ignore wraparound
						tileidx = ((x & 0x3F800) >> 10) + ((y & 0x3F800) >> 3);
						tileidx = PPU.VRAM[tileidx] << 7;
					}
				}
				else
				{
					tileidx = ((x & 0x3F800) >> 10) + ((y & 0x3F800) >> 3);
					tileidx = PPU.VRAM[tileidx] << 7;
				}
				
				tileidx += ((x & 0x700) >> 7) + ((y & 0x700) >> 4) + 1;
				colorval = PPU.VRAM[tileidx];
				
				if (colorval)
					buffer[i] = pal[colorval] | finalalpha;
				
				i++;
				x += PPU.M7A;
				y += PPU.M7C;
			}
		}
		
		if (s->EndOffset >= 256) return;
		s++;
	}
}


void PPU_RenderDeferredTiles_2bpp(PPU_Background* bg, u16* pal)
{
	PPU_DeferredTile* deftile = &(bg->DeferredTiles[0]);
	PPU_DeferredTile* end = &(bg->DeferredTiles[bg->NumDeferredTiles]);
	
	for (; deftile < end; deftile++)
	{
		u16 tilepixels = deftile->SrcPixels[0];
		if (!tilepixels) continue;
		
		u16 attrib = deftile->Attrib;
		PPU_RenderTile_2bpp(attrib, tilepixels, deftile->Dest, &pal[(attrib & 0x1C00) >> 8], deftile->Alpha, deftile->Start, deftile->End);
	}
	
	bg->NumDeferredTiles = 0;
}

void PPU_RenderDeferredTiles_4bpp(PPU_Background* bg, u16* pal)
{
	PPU_DeferredTile* deftile = &(bg->DeferredTiles[0]);
	PPU_DeferredTile* end = &(bg->DeferredTiles[bg->NumDeferredTiles]);
	
	for (; deftile < end; deftile++)
	{
		u32 tilepixels = deftile->SrcPixels[0] | (deftile->SrcPixels[8] << 16);
		if (!tilepixels) continue;
		
		u16 attrib = deftile->Attrib;
		PPU_RenderTile_4bpp(attrib, tilepixels, deftile->Dest, &pal[(attrib & 0x1C00) >> 6], deftile->Alpha, deftile->Start, deftile->End);
	}
	
	bg->NumDeferredTiles = 0;
}

void PPU_RenderDeferredTiles_8bpp(PPU_Background* bg, u16* pal)
{
	PPU_DeferredTile* deftile = &(bg->DeferredTiles[0]);
	PPU_DeferredTile* end = &(bg->DeferredTiles[bg->NumDeferredTiles]);
	
	for (; deftile < end; deftile++)
	{
		u32 tilepixels1 = deftile->SrcPixels[0] | (deftile->SrcPixels[8] << 16);
		u32 tilepixels2 = deftile->SrcPixels[16] | (deftile->SrcPixels[24] << 16);
		if (!(tilepixels1|tilepixels2)) continue;
		
		// TODO: pal for direct color
		
		u16 attrib = deftile->Attrib;
		PPU_RenderTile_8bpp(attrib, tilepixels1, tilepixels2, deftile->Dest, pal, deftile->Alpha, deftile->Start, deftile->End);
	}
	
	bg->NumDeferredTiles = 0;
}


int PPU_RenderOBJ(u8* oam, u32 oamextra, u32 ymask, u16* buffer, u32 line)
{
	u16* tileset = PPU.OBJTileset;
	s32 xoff;
	u16 attrib;
	u32 tilepixels;
	u32 idx;
	s32 i;
	s32 width = (s32)PPU.OBJWidth[(oamextra & 0x2) >> 1];
	u32 paloffset, prio;
	
	xoff = oam[0];
	if (oamextra & 0x1) // xpos bit8, sign bit
	{
		xoff = 0x100 - xoff;
		if (xoff >= width) return 0;
		i = -xoff;
	}
	else
		i = xoff;
		
	attrib = *(u16*)&oam[2];
	
	idx = (attrib & 0x01FF) << 4;
	
	if (attrib & 0x8000) line = ymask - line;
	idx += (line & 0x07) | ((line & 0x38) << 5);
	
	if (attrib & 0x4000)
		idx += ((width-1) & 0x38) << 1;
		
	width += i;
	if (width > 256) width = 256;
	
	paloffset = ((oam[3] & 0x0E) << 3);
	
	prio = oam[3] & 0x30;
	PPU.SpritesOnLine[prio >> 4] = 1;
	
	for (; i < width;)
	{
		// skip offscreen tiles
		if (i <= -8)
		{
			i += 8;
			idx += (attrib & 0x4000) ? -16:16;
			continue;
		}
		
		tilepixels = tileset[idx] | (tileset[idx+8] << 16);
		PPU_RenderTile_OBJ(attrib, tilepixels, &buffer[i], paloffset | (prio << 8));
		i += 8;
		idx += (attrib & 0x4000) ? -16:16;
	}
	
	return 1;
}

void PPU_PrerenderOBJs(u16* buf, s32 line)
{
	int i = PPU.FirstOBJ;
	i--;
	if (i < 0) i = 127;
	int last = i;
	int nrendered = 0;

	do
	{
		u8* oam = &PPU.OAM[i << 2];
		u8 oamextra = PPU.OAM[0x200 + (i >> 2)] >> ((i & 0x03) << 1);
		s32 oy = (s32)oam[1] + 1;
		s32 oh = (s32)PPU.OBJHeight[(oamextra & 0x2) >> 1];
		
		if (line >= oy && line < (oy+oh))
		{
			if (nrendered >= 32)
			{
				PPU.OBJOverflow |= 0x40;
				return;
			}
			nrendered += PPU_RenderOBJ(oam, oamextra, oh-1, buf, line-oy);
		}
		else if (oy >= 192)
		{
			oy -= 0x100;
			if ((oy+oh) > 1 && line < (oy+oh))
			{
				if (nrendered >= 32)
				{
					PPU.OBJOverflow |= 0x40;
					return;
				}
				nrendered += PPU_RenderOBJ(oam, oamextra, oh-1, buf, line-oy);
			}
		}

		i--;
		if (i < 0) i = 127;
	}
	while (i != last);
}

void PPU_RenderOBJs(u16* buf, u32 line, u32 prio, u32 colmathmask, u32 window)
{
	int i = 0;
	
	if (!PPU.SpritesOnLine[prio >> 12]) return;
	
	u16* srcbuf = &PPU.OBJBuffer[16];
	u16* pal = &PPU.Palette[128];
	
	PPU_WindowSegment* s = &PPU.Window[0];
	for (;;)
	{
		u16 hidden = window ? (PPU.OBJWindowCombine & (1 << (s->WindowMask ^ PPU.OBJWindowMask))) : 0;
		
		if (hidden)
		{
			i = s->EndOffset;
		}
		else
		{
			u32 finalalpha = (PPU.ColorMath1 & s->ColorMath) ? 0:1;
			
			while (i < s->EndOffset)
			{
				u16 val = srcbuf[i];
				if ((val & 0xFF00) == prio)
					buf[i] = pal[val & 0xFF] | ((val & colmathmask) ? finalalpha:0);
				i++;
			}
		}
		
		if (s->EndOffset >= 256) return;
		s++;
	}
}


// TODO maybe optimize? precompute that whenever the PPU mode is changed?
#define PPU_RENDERBG(depth, num, pal) \
	{ \
		if (PPU.Mode & (0x10<<num)) \
			PPU_RenderBG_##depth##_16x16(&PPU.BG[num], buf, line, &PPU.Palette[pal], COLMATH(num), screen&(0x100<<num)); \
		else \
			PPU_RenderBG_##depth##_8x8(&PPU.BG[num], buf, line, &PPU.Palette[pal], COLMATH(num), screen&(0x100<<num)); \
	}

void PPU_RenderMode0(u16* buf, u32 line, u16 screen, u8 colormath)
{
	if (screen & 0x08) PPU_RENDERBG(2bpp, 3, 96);
	if (screen & 0x04) PPU_RENDERBG(2bpp, 2, 64);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x0000, COLMATH_OBJ, (screen&0x1000));
	
	if (screen & 0x08) PPU_RenderDeferredTiles_2bpp(&PPU.BG[3], &PPU.Palette[96]);
	if (screen & 0x04) PPU_RenderDeferredTiles_2bpp(&PPU.BG[2], &PPU.Palette[64]);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x1000, COLMATH_OBJ, (screen&0x1000));
	
	if (screen & 0x02) PPU_RENDERBG(2bpp, 1, 32);
	if (screen & 0x01) PPU_RENDERBG(2bpp, 0, 0);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x2000, COLMATH_OBJ, (screen&0x1000));
	
	if (screen & 0x02) PPU_RenderDeferredTiles_2bpp(&PPU.BG[1], &PPU.Palette[32]);
	if (screen & 0x01) PPU_RenderDeferredTiles_2bpp(&PPU.BG[0], &PPU.Palette[0]);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x3000, COLMATH_OBJ, (screen&0x1000));
}

void PPU_RenderMode1(u16* buf, u32 line, u16 screen, u8 colormath)
{
	if (screen & 0x04) PPU_RENDERBG(2bpp, 2, 0);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x0000, COLMATH_OBJ, (screen&0x1000));
	
	if (screen & 0x04) 
	{
		if (!(PPU.Mode & 0x08))
			PPU_RenderDeferredTiles_2bpp(&PPU.BG[2], &PPU.Palette[0]);
	}
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x1000, COLMATH_OBJ, (screen&0x1000));
	
	if (screen & 0x02) PPU_RENDERBG(4bpp, 1, 0);
	if (screen & 0x01) PPU_RENDERBG(4bpp, 0, 0);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x2000, COLMATH_OBJ, (screen&0x1000));
	
	if (screen & 0x02) PPU_RenderDeferredTiles_4bpp(&PPU.BG[1], &PPU.Palette[0]);
	if (screen & 0x01) PPU_RenderDeferredTiles_4bpp(&PPU.BG[0], &PPU.Palette[0]);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x3000, COLMATH_OBJ, (screen&0x1000));
	
	if (screen & 0x04)
	{
		if (PPU.Mode & 0x08)
			PPU_RenderDeferredTiles_2bpp(&PPU.BG[2], &PPU.Palette[0]);
	}
}

// TODO: offset per tile, someday
void PPU_RenderMode2(u16* buf, u32 line, u16 screen, u8 colormath)
{
	if (screen & 0x02) PPU_RENDERBG(4bpp, 1, 0);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x0000, COLMATH_OBJ, (screen&0x1000));
	
	if (screen & 0x01) PPU_RENDERBG(4bpp, 0, 0);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x1000, COLMATH_OBJ, (screen&0x1000));
	
	if (screen & 0x02) PPU_RenderDeferredTiles_4bpp(&PPU.BG[1], &PPU.Palette[0]);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x2000, COLMATH_OBJ, (screen&0x1000));
	
	if (screen & 0x01) PPU_RenderDeferredTiles_4bpp(&PPU.BG[0], &PPU.Palette[0]);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x3000, COLMATH_OBJ, (screen&0x1000));
}

void PPU_RenderMode3(u16* buf, u32 line, u16 screen, u8 colormath)
{
	if (screen & 0x02) PPU_RENDERBG(4bpp, 1, 0);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x0000, COLMATH_OBJ, (screen&0x1000));
	
	if (screen & 0x01) PPU_RENDERBG(8bpp, 0, 0);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x1000, COLMATH_OBJ, (screen&0x1000));
	
	if (screen & 0x02) PPU_RenderDeferredTiles_4bpp(&PPU.BG[1], &PPU.Palette[0]);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x2000, COLMATH_OBJ, (screen&0x1000));
	
	if (screen & 0x01) PPU_RenderDeferredTiles_8bpp(&PPU.BG[0], &PPU.Palette[0]);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x3000, COLMATH_OBJ, (screen&0x1000));
}

// TODO: offset per tile, someday
void PPU_RenderMode4(u16* buf, u32 line, u16 screen, u8 colormath)
{
	if (screen & 0x02) PPU_RENDERBG(2bpp, 1, 0);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x0000, COLMATH_OBJ, (screen&0x1000));
	
	if (screen & 0x01) PPU_RENDERBG(8bpp, 0, 0);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x1000, COLMATH_OBJ, (screen&0x1000));
	
	if (screen & 0x02) PPU_RenderDeferredTiles_2bpp(&PPU.BG[1], &PPU.Palette[0]);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x2000, COLMATH_OBJ, (screen&0x1000));
	
	if (screen & 0x01) PPU_RenderDeferredTiles_8bpp(&PPU.BG[0], &PPU.Palette[0]);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x3000, COLMATH_OBJ, (screen&0x1000));
}

void PPU_RenderMode7(u16* buf, u32 line, u16 screen, u8 colormath)
{
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x0000, COLMATH_OBJ, (screen&0x1000));
	
	if (screen & 0x01) PPU_RenderBG_Mode7(buf, line, &PPU.Palette[0], COLMATH(0), (screen&0x100));
	
	if (screen & 0x10)
	{
		PPU_RenderOBJs(buf, line, 0x1000, COLMATH_OBJ, (screen&0x1000));
		PPU_RenderOBJs(buf, line, 0x2000, COLMATH_OBJ, (screen&0x1000));
		PPU_RenderOBJs(buf, line, 0x3000, COLMATH_OBJ, (screen&0x1000));
	}
}


void PPU_RenderScanline_Soft(u32 line)
{
	if ((!line) || PPU.ColorEffectDirty)
	{
		PPU_ColorEffectSection* s;
		if (!line)
		{
			PPU.CurColorEffect = &PPU.ColorEffectSections[0];
			s = PPU.CurColorEffect;
		}
		else
		{
			s = PPU.CurColorEffect;
			s->EndOffset = line;
			s++;
		}
		
		s->ColorMath = (PPU.ColorMath2 & 0x80);
		s->Brightness = PPU.CurBrightness;
		PPU.CurColorEffect = s;
		PPU.ColorEffectDirty = 0;
	}
	
	if (!PPU.CurBrightness) return;
	
	if (PPU.WindowDirty)
	{
		PPU_ComputeWindows();
		PPU.WindowDirty = 0;
	}
	
	int i;
	u16* mbuf = &PPU.MainBuffer[line << 8];
	u16* sbuf = &PPU.SubBuffer[line << 8];
	
	// main backdrop
	u32 backdrop = PPU.Palette[0];
	if (PPU.ColorMath2 & 0x20)
	{
		PPU_WindowSegment* s = &PPU.Window[0];
		i = 0;
		for (;;)
		{
			u32 alpha = (PPU.ColorMath1 & s->ColorMath) ? 0:1;
			while (i < s->EndOffset)
				mbuf[i++] = backdrop|alpha;
			
			if (s->EndOffset >= 256) break;
			s++;
		}
	}
	else
	{
		backdrop |= (backdrop << 16);
		for (i = 0; i < 256; i += 2)
			*(u32*)&mbuf[i] = backdrop;
	}
	
	// sub backdrop
	backdrop = PPU.SubBackdrop;
	backdrop |= (backdrop << 16);
	for (i = 0; i < 256; i += 2)
		*(u32*)&sbuf[i] = backdrop;
	
	for (i = 16; i < 272; i += 2)
		*(u32*)&PPU.OBJBuffer[i] = 0xFFFFFFFF;
		
	*(u32*)&PPU.SpritesOnLine[0] = 0;
	
	
	if ((PPU.MainScreen|PPU.SubScreen) & 0x10)
		PPU_PrerenderOBJs(&PPU.OBJBuffer[16], line);
		
	u8 colormathmain, colormathsub, rendersub;
	if ((PPU.ColorMath1 & 0x30) == 0x30) // color math completely disabled
	{
		colormathmain = 0;
		rendersub = 0;
	}
	else
	{
		colormathmain = PPU.ColorMath2 & 0x0F;
		if (PPU.ColorMath2 & 0x10) colormathmain |= 0x40;
		colormathsub = (PPU.ColorMath2 & 0x40) ? 0:0xFF;
		rendersub = (PPU.ColorMath1 & 0x02);
	}
	
	switch (PPU.Mode & 0x07)
	{
		case 0:
			if (rendersub)
				PPU_RenderMode0(sbuf, line, PPU.SubScreen, colormathsub);
			PPU_RenderMode0(mbuf, line, PPU.MainScreen, colormathmain);
			break;
			
		case 1:
			if (rendersub)
				PPU_RenderMode1(sbuf, line, PPU.SubScreen, colormathsub);
			PPU_RenderMode1(mbuf, line, PPU.MainScreen, colormathmain);
			break;
		
		case 2:
			if (rendersub)
				PPU_RenderMode2(sbuf, line, PPU.SubScreen, colormathsub);
			PPU_RenderMode2(mbuf, line, PPU.MainScreen, colormathmain);
			break;
			
		case 3:
			if (rendersub)
				PPU_RenderMode3(sbuf, line, PPU.SubScreen, colormathsub);
			PPU_RenderMode3(mbuf, line, PPU.MainScreen, colormathmain);
			break;
			
		case 4:
			if (rendersub)
				PPU_RenderMode4(sbuf, line, PPU.SubScreen, colormathsub);
			PPU_RenderMode4(mbuf, line, PPU.MainScreen, colormathmain);
			break;
			
		// TODO: mode 5/6 (hires)
		
		case 7:
			if (rendersub)
				PPU_RenderMode7(sbuf, line, PPU.SubScreen, colormathsub);
			PPU_RenderMode7(mbuf, line, PPU.MainScreen, colormathmain);
			break;
	}
	
	// that's all folks.
	// color math and master brightness will be done in hardware from now on
}


extern u32* gxCmdBuf;
extern void* vertexBuf;

extern DVLB_s* softRenderShader;

extern float snesProjMatrix[16];

extern float* screenVertices;
extern u32* gpuOut;
extern u32* gpuDOut;
extern u32* SNESFrame;
extern u16* MainScreenTex;
extern u16* SubScreenTex;

#define ADDVERTEX(x, y, s, t) \
	*vptr++ = x; \
	*vptr++ = y; \
	*vptr++ = s; \
	*vptr++ = t;
	

void PPU_VBlank_Soft()
{
	if (RenderState) 
	{
		gspWaitForP3D();
		RenderState = 0;
	}
	
	// copy new screen textures
	// SetDisplayTransfer with flags=2 converts linear graphics to the tiled format used for textures
	// since the two sets of buffers are contiguous, we can transfer them as one 256x512 texture
	GSPGPU_FlushDataCache(NULL, (u8*)PPU.MainBuffer, 256*512*2);
	GX_SetDisplayTransfer(gxCmdBuf, (u32*)PPU.MainBuffer, 0x02000100, (u32*)MainScreenTex, 0x02000100, 0x3302);
	
	
	PPU_ColorEffectSection* s = &PPU.ColorEffectSections[0];
	
	PPU.CurColorEffect->EndOffset = 240;
	int startoffset = 0;
	
	u16* vptr = (u16*)vertexBuf;
	
	GPU_SetShader(softRenderShader);
	GPU_SetViewport((u32*)osConvertVirtToPhys((u32)gpuDOut),(u32*)osConvertVirtToPhys((u32)SNESFrame),0,0,256,256);
	
	
	
	for (;;)
	{
		//bprintf("section %d %d %02X %d\n", startoffset, s->EndOffset, s->ColorMath, s->Brightness);
		
		GPU_DepthRange(-1.0f, 0.0f);
		GPU_SetFaceCulling(GPU_CULL_BACK_CCW);
		GPU_SetStencilTest(false, GPU_ALWAYS, 0x00, 0xFF, 0x00);
		GPU_SetStencilOp(GPU_KEEP, GPU_KEEP, GPU_KEEP);
		GPU_SetBlendingColor(0,0,0,0);
		GPU_SetDepthTestAndWriteMask(false, GPU_ALWAYS, GPU_WRITE_COLOR); // we don't care about depth testing in this pass
		
		GPUCMD_AddSingleParam(0x00010062, 0x00000000);
		GPUCMD_AddSingleParam(0x000F0118, 0x00000000);
		
		GPU_SetAlphaBlending(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
		GPU_SetAlphaTest(false, GPU_ALWAYS, 0x00);

		setUniformMatrix(0x20, snesProjMatrix);
		
		GPU_SetTextureEnable(GPU_TEXUNIT0|GPU_TEXUNIT1);
		
		// TEXTURE ENV STAGES
		// ---
		// blending operation: (Main.Color +- (Sub.Color * Main.Alpha)) * Sub.Alpha
		// Main.Alpha: 0 = no color math, 255 = color math
		// Sub.Alpha: 0 = div2, 1 = no div2
		// ---
		// STAGE 1: Out.Color = Sub.Color * Main.Alpha, Out.Alpha = Sub.Alpha + 0.5
		GPU_SetTexEnv(0, 
			GPU_TEVSOURCES(GPU_TEXTURE1, GPU_TEXTURE0, 0), 
			GPU_TEVSOURCES(GPU_TEXTURE1, GPU_CONSTANT, 0),
			GPU_TEVOPERANDS(0,2,0), 
			GPU_TEVOPERANDS(0,0,0), 
			GPU_MODULATE, 
			GPU_ADD, 
			0x80FFFFFF);
		
		if (s->ColorMath)
		{
			// COLOR SUBTRACT
			
			// STAGE 2: Out.Color = Main.Color - Prev.Color, Out.Alpha = Prev.Alpha + (1-Main.Alpha) (cancel out div2 when color math doesn't happen)
			GPU_SetTexEnv(1, 
				GPU_TEVSOURCES(GPU_TEXTURE0, GPU_PREVIOUS, 0), 
				GPU_TEVSOURCES(GPU_PREVIOUS, GPU_TEXTURE0, 0),
				GPU_TEVOPERANDS(0,0,0), 
				GPU_TEVOPERANDS(0,1,0), 
				GPU_SUBTRACT, 
				GPU_ADD, 
				0xFFFFFFFF);
			// STAGE 3: Out.Color = Prev.Color * Prev.Alpha, Out.Alpha = Prev.Alpha
			GPU_SetTexEnv(2, 
				GPU_TEVSOURCES(GPU_PREVIOUS, GPU_PREVIOUS, 0), 
				GPU_TEVSOURCES(GPU_PREVIOUS, 0, 0),
				GPU_TEVOPERANDS(0,2,0), 
				GPU_TEVOPERANDS(0,0,0), 
				GPU_MODULATE, 
				GPU_REPLACE, 
				0xFFFFFFFF);
			// STAGE 4: dummy (no need to double color intensity)
			GPU_SetDummyTexEnv(3);
		}
		else
		{
			// COLOR ADDITION
			
			// STAGE 2: Out.Color = Main.Color*0.5 + Prev.Color*0.5 (prevents overflow), Out.Alpha = Prev.Alpha + (1-Main.Alpha) (cancel out div2 when color math doesn't happen)
			GPU_SetTexEnv(1, 
				GPU_TEVSOURCES(GPU_TEXTURE0, GPU_PREVIOUS, GPU_CONSTANT), 
				GPU_TEVSOURCES(GPU_PREVIOUS, GPU_TEXTURE0, 0),
				GPU_TEVOPERANDS(0,0,0), 
				GPU_TEVOPERANDS(0,1,0), 
				GPU_INTERPOLATE,
				GPU_ADD, 
				0xFF808080);
			// STAGE 3: Out.Color = Prev.Color * Prev.Alpha, Out.Alpha = Prev.Alpha
			GPU_SetTexEnv(2, 
				GPU_TEVSOURCES(GPU_PREVIOUS, GPU_PREVIOUS, 0), 
				GPU_TEVSOURCES(GPU_PREVIOUS, 0, 0),
				GPU_TEVOPERANDS(0,2,0), 
				GPU_TEVOPERANDS(0,0,0), 
				GPU_MODULATE, 
				GPU_REPLACE, 
				0xFFFFFFFF);
			// STAGE 4: Out.Color = Prev.Color + Prev.Color (doubling color intensity), Out.Alpha = Const.Alpha
			GPU_SetTexEnv(3, 
				GPU_TEVSOURCES(GPU_PREVIOUS, GPU_PREVIOUS, 0), 
				GPU_TEVSOURCES(GPU_CONSTANT, 0, 0),
				GPU_TEVOPERANDS(0,0,0), 
				GPU_TEVOPERANDS(0,0,0), 
				GPU_ADD, 
				GPU_REPLACE, 
				0xFFFFFFFF);
		}
		
		// STAGE 5: master brightness - Out.Color = Prev.Color * Brightness, Out.Alpha = Const.Alpha
		GPU_SetTexEnv(4, 
			GPU_TEVSOURCES(GPU_PREVIOUS, GPU_CONSTANT, 0), 
			GPU_TEVSOURCES(GPU_CONSTANT, 0, 0),
			GPU_TEVOPERANDS(0,0,0), 
			GPU_TEVOPERANDS(0,0,0), 
			GPU_MODULATE, 
			GPU_REPLACE, 
			0xFF000000 | (s->Brightness*0x00010101));
		// STAGE 6: dummy
		GPU_SetDummyTexEnv(5);
			
		GPU_SetTexture(GPU_TEXUNIT0, (u32*)osConvertVirtToPhys((u32)MainScreenTex),256,256,0,GPU_RGBA5551);
		GPU_SetTexture(GPU_TEXUNIT1, (u32*)osConvertVirtToPhys((u32)SubScreenTex),256,256,0,GPU_RGBA5551);
		
		GPU_SetAttributeBuffers(2, (u32*)osConvertVirtToPhys((u32)vptr),
			GPU_ATTRIBFMT(0, 2, GPU_SHORT)|GPU_ATTRIBFMT(1, 2, GPU_SHORT),
			0xFFC, 0x10, 1, (u32[]){0x00000000}, (u64[]){0x10}, (u8[]){2});
		
		ADDVERTEX(0, startoffset,       0, 256-startoffset);
		ADDVERTEX(256, startoffset,     256, 256-startoffset);
		ADDVERTEX(256, s->EndOffset,    256, 256-s->EndOffset);
		ADDVERTEX(0, startoffset,       0, 256-startoffset);
		ADDVERTEX(256, s->EndOffset,    256, 256-s->EndOffset);
		ADDVERTEX(0, s->EndOffset,      0, 256-s->EndOffset);
		vptr = (u16*)((((u32)vptr) + 0xF) & ~0xF);
		
		GPU_DrawArray(GPU_TRIANGLES, 2*3);
		
		GPU_FinishDrawing();
		
		if (s->EndOffset == 240) break;
		
		startoffset = s->EndOffset;
		s++;
	}
	
	gspWaitForPPF();
}
