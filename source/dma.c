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

#include "snes.h"
#include "ppu.h"


u8 DMA_Chans[8*16];
u8 DMA_HDMAFlag;
u8 DMA_HDMACurFlag;
u8 DMA_HDMAEnded;

u8 HDMA_Pause[8];


u8 DMA_Read8(u32 addr)
{
	u8 ret = (addr > 0x7F) ? SNES_Status->LastBusVal : DMA_Chans[addr];
	return ret;
}

u16 DMA_Read16(u32 addr)
{
	u16 ret = (addr > 0x7F) ? (SNES_Status->LastBusVal|(SNES_Status->LastBusVal<<8)) : *(u16*)&DMA_Chans[addr];
	return ret;
}

void DMA_Write8(u32 addr, u8 val)
{
	if (addr < 0x80)
		DMA_Chans[addr] = val;
}

void DMA_Write16(u32 addr, u16 val)
{
	if (addr < 0x80)
	{
		*(u16*)&DMA_Chans[addr] = val;
	}
}

void DMA_Enable(u8 flag)
{
	int c;
	for (c = 0; c < 8; c++)
	{
		if (!(flag & (1 << c)))
			continue;
		
		u8* chan = &DMA_Chans[c << 4];
		u8 params = chan[0];
		
		u16 maddrinc;
		switch (params & 0x18)
		{
			case 0x00: maddrinc = 1; break;
			case 0x10: maddrinc = -1; break;
			default: maddrinc = 0; break;
		}
		
		u8 paddrinc = params & 0x07;
		
		u8 ppuaddr = chan[1];
		u16 memaddr = *(u16*)&chan[2];
		u32 membank = chan[4] << 16;
		u32 bytecount = *(u16*)&chan[5];
		if (!bytecount) bytecount = 0x10000;
		
		//bprintf("DMA%d %d %06X %s 21%02X m:%d p:%d\n", c, bytecount, memaddr|membank, (params&0x80)?"<-":"->", ppuaddr, maddrinc, paddrinc);
		
		u8 scheck = params & 0x9F;
		if (scheck == 0x00 || scheck == 0x02)
		{
			if (ppuaddr == 0x04)
			{
				while (bytecount > 1)
				{
					if (PPU.OAMAddr >= 0x200)
					{
						*(u16*)&PPU.OAM[PPU.OAMAddr & 0x21F] = SNES_Read16(membank|memaddr);
					}
					else
					{
						*(u16*)&PPU.OAM[PPU.OAMAddr] = SNES_Read16(membank|memaddr);
					}
					memaddr += maddrinc<<1;
					bytecount -= 2;
					PPU.OAMAddr += 2;
					PPU.OAMAddr &= ~0x400;
				}
			}
			else if (ppuaddr == 0x22)
			{
				while (bytecount > 1)
				{
					PPU_SetColor(PPU.CGRAMAddr >> 1, SNES_Read16(membank|memaddr));
					memaddr += maddrinc<<1;
					bytecount -= 2;
					PPU.CGRAMAddr += 2;
					PPU.CGRAMAddr &= ~0x200;
				}
			}
		}
		else if (scheck == 0x01)
		{
			if (ppuaddr == 0x18)
			{
				while (bytecount > 1)
				{
					u16 newval = SNES_Read16(membank|memaddr);
					u32 newaddr = PPU_TranslateVRAMAddress(PPU.VRAMAddr);
					if (newval != *(u16*)&PPU.VRAM[newaddr])
					{
						if(PPU.HardwareRenderer)
							PPU_ConvertVRAM16(newaddr, newval);
						*(u16*)&PPU.VRAM[newaddr] = newval;
					}
					memaddr += maddrinc<<1;
					bytecount -= 2;
					PPU.VRAMAddr += PPU.VRAMStep;
					
				}
			}
		}
		
		if (bytecount > 0)
		{
			if (params & 0x80)
			{
				for (;;)
				{
					switch (paddrinc)
					{
						case 0:
							SNES_Write8(membank|memaddr, PPU_Read8(ppuaddr));
							memaddr += maddrinc; bytecount--;
							break;
						case 1:
							SNES_Write8(membank|memaddr, PPU_Read8(ppuaddr));
							memaddr += maddrinc; bytecount--; if (!bytecount) break;
							SNES_Write8(membank|memaddr, PPU_Read8(ppuaddr+1));
							memaddr += maddrinc; bytecount--;
							break;
						case 2:
						case 6:
							SNES_Write8(membank|memaddr, PPU_Read8(ppuaddr));
							memaddr += maddrinc; bytecount--; if (!bytecount) break;
							SNES_Write8(membank|memaddr, PPU_Read8(ppuaddr));
							memaddr += maddrinc; bytecount--;
							break;
						case 3:
						case 7:
							SNES_Write8(membank|memaddr, PPU_Read8(ppuaddr));
							memaddr += maddrinc; bytecount--; if (!bytecount) break;
							SNES_Write8(membank|memaddr, PPU_Read8(ppuaddr));
							memaddr += maddrinc; bytecount--; if (!bytecount) break;
							SNES_Write8(membank|memaddr, PPU_Read8(ppuaddr+1));
							memaddr += maddrinc; bytecount--; if (!bytecount) break;
							SNES_Write8(membank|memaddr, PPU_Read8(ppuaddr+1));
							memaddr += maddrinc; bytecount--;
							break;
						case 4:
							SNES_Write8(membank|memaddr, PPU_Read8(ppuaddr));
							memaddr += maddrinc; bytecount--; if (!bytecount) break;
							SNES_Write8(membank|memaddr, PPU_Read8(ppuaddr+1));
							memaddr += maddrinc; bytecount--; if (!bytecount) break;
							SNES_Write8(membank|memaddr, PPU_Read8(ppuaddr+2));
							memaddr += maddrinc; bytecount--; if (!bytecount) break;
							SNES_Write8(membank|memaddr, PPU_Read8(ppuaddr+3));
							memaddr += maddrinc; bytecount--;
							break;
						case 5:
							SNES_Write8(membank|memaddr, PPU_Read8(ppuaddr));
							memaddr += maddrinc; bytecount--; if (!bytecount) break;
							SNES_Write8(membank|memaddr, PPU_Read8(ppuaddr+1));
							memaddr += maddrinc; bytecount--; if (!bytecount) break;
							SNES_Write8(membank|memaddr, PPU_Read8(ppuaddr));
							memaddr += maddrinc; bytecount--; if (!bytecount) break;
							SNES_Write8(membank|memaddr, PPU_Read8(ppuaddr+1));
							memaddr += maddrinc; bytecount--;
							break;
					}
					
					if (!bytecount) break;
				}
			}
			else
			{
				for (;;)
				{
					switch (paddrinc)
					{
						case 0:
							PPU_Write8(ppuaddr, SNES_Read8(membank|memaddr));
							memaddr += maddrinc; bytecount--;
							break;
						case 1:
							PPU_Write8(ppuaddr, SNES_Read8(membank|memaddr));
							memaddr += maddrinc; bytecount--; if (!bytecount) break;
							PPU_Write8(ppuaddr+1, SNES_Read8(membank|memaddr));
							memaddr += maddrinc; bytecount--;
							break;
						case 2:
						case 6:
							PPU_Write8(ppuaddr, SNES_Read8(membank|memaddr));
							memaddr += maddrinc; bytecount--; if (!bytecount) break;
							PPU_Write8(ppuaddr, SNES_Read8(membank|memaddr));
							memaddr += maddrinc; bytecount--;
							break;
						case 3:
						case 7:
							PPU_Write8(ppuaddr, SNES_Read8(membank|memaddr));
							memaddr += maddrinc; bytecount--; if (!bytecount) break;
							PPU_Write8(ppuaddr, SNES_Read8(membank|memaddr));
							memaddr += maddrinc; bytecount--; if (!bytecount) break;
							PPU_Write8(ppuaddr+1, SNES_Read8(membank|memaddr));
							memaddr += maddrinc; bytecount--; if (!bytecount) break;
							PPU_Write8(ppuaddr+1, SNES_Read8(membank|memaddr));
							memaddr += maddrinc; bytecount--;
							break;
						case 4:
							PPU_Write8(ppuaddr, SNES_Read8(membank|memaddr));
							memaddr += maddrinc; bytecount--; if (!bytecount) break;
							PPU_Write8(ppuaddr+1, SNES_Read8(membank|memaddr));
							memaddr += maddrinc; bytecount--; if (!bytecount) break;
							PPU_Write8(ppuaddr+2, SNES_Read8(membank|memaddr));
							memaddr += maddrinc; bytecount--; if (!bytecount) break;
							PPU_Write8(ppuaddr+3, SNES_Read8(membank|memaddr));
							memaddr += maddrinc; bytecount--;
							break;
						case 5:
							PPU_Write8(ppuaddr, SNES_Read8(membank|memaddr));
							memaddr += maddrinc; bytecount--; if (!bytecount) break;
							PPU_Write8(ppuaddr+1, SNES_Read8(membank|memaddr));
							memaddr += maddrinc; bytecount--; if (!bytecount) break;
							PPU_Write8(ppuaddr, SNES_Read8(membank|memaddr));
							memaddr += maddrinc; bytecount--; if (!bytecount) break;
							PPU_Write8(ppuaddr+1, SNES_Read8(membank|memaddr));
							memaddr += maddrinc; bytecount--;
							break;
					}
					
					if (!bytecount) break;
				}
			}
		}
		
		*(u16*)&chan[2] = memaddr;
		*(u16*)&chan[5] = 0;
	}
}

void DMA_ReloadHDMA()
{
	register u8 flag = DMA_HDMACurFlag;
	register u8 ended = DMA_HDMAEnded;
	flag = DMA_HDMAFlag;
	if (flag)
	{
		int c;
		for (c = 0; c < 8; c++)
		{
			if (!(flag & (1 << c)))
				continue;
			
			u8* chan = &DMA_Chans[c << 4];
			
			// reload table address
			u16 tableaddr = *(u16*)&chan[2];
			u32 tablebank = chan[4] << 16;
			
			// load first repeatflag
			chan[10] = SNES_Read8(tablebank|tableaddr);
			if(!chan[10])
			{
				if(chan[0] & 0x40)
				{
					u16 memaddr = SNES_Read16(tableaddr|tablebank);
					*(u16*)&chan[5] = memaddr;
					tableaddr++;
				}
				*(u16*)&chan[8] = tableaddr + 1;
				flag &= ~(1 << c);
				ended |= (1 << c);
				HDMA_Pause[c] = 1;
				continue;
			}

			tableaddr++;
			HDMA_Pause[c] = 0;

			if (chan[0] & 0x40)
			{
				u16 memaddr = SNES_Read16(tableaddr|tablebank);
				*(u16*)&chan[5] = memaddr;
				
				tableaddr += 2;
			}
			
			*(u16*)&chan[8] = tableaddr;
		}
	}
}

const u8 hdma_sizes[8] = {1, 2, 2, 4, 4, 4, 2, 4};

void DMA_DoHDMA()
{	
	register u8 flag = DMA_HDMACurFlag;
	register u8 ended = DMA_HDMAEnded;
	flag &= ~ended;
	if (flag)
	{
		int c;
		for (c = 0; c < 8; c++)
		{
			if (!(flag & (1 << c)))
				continue;
			
			u8* chan = &DMA_Chans[c << 4];
			
			u16 tableaddr = *(u16*)&chan[8];
			u32 tablebank = chan[4] << 16;
			
			u8 repeatflag = chan[10];
			if (repeatflag == 0)
					continue;
			
			if (!HDMA_Pause[c])
			{
				u8 params = chan[0];
				
				u8 paddrinc = params & 0x07;
				
				u8 ppuaddr = chan[1];
				
				u16 memaddr;
				u32 membank;
				
				if (params & 0x40)
				{
					memaddr = *(u16*)&chan[5];
					membank = chan[7] << 16;
				}
				else
				{
					memaddr = tableaddr;
					membank = tablebank;
				}
				
				if (params & 0x80)
				{
					switch (paddrinc)
					{
						case 0:
							SNES_Write8(membank|memaddr, PPU_Read8(ppuaddr));
							break;
						case 1:
							SNES_Write8(membank|memaddr, PPU_Read8(ppuaddr));
							memaddr++;
							SNES_Write8(membank|memaddr, PPU_Read8(ppuaddr+1));
							break;
						case 2:
						case 6:
							SNES_Write8(membank|memaddr, PPU_Read8(ppuaddr));
							memaddr++;
							SNES_Write8(membank|memaddr, PPU_Read8(ppuaddr));
							break;
						case 3:
						case 7:
							SNES_Write8(membank|memaddr, PPU_Read8(ppuaddr));
							memaddr++;
							SNES_Write8(membank|memaddr, PPU_Read8(ppuaddr));
							memaddr++;
							SNES_Write8(membank|memaddr, PPU_Read8(ppuaddr+1));
							memaddr++;
							SNES_Write8(membank|memaddr, PPU_Read8(ppuaddr+1));
							break;
						case 4:
							SNES_Write8(membank|memaddr, PPU_Read8(ppuaddr));
							memaddr++;
							SNES_Write8(membank|memaddr, PPU_Read8(ppuaddr+1));
							memaddr++;
							SNES_Write8(membank|memaddr, PPU_Read8(ppuaddr+2));
							memaddr++;
							SNES_Write8(membank|memaddr, PPU_Read8(ppuaddr+3));
							break;
						case 5:
							SNES_Write8(membank|memaddr, PPU_Read8(ppuaddr));
							memaddr++;
							SNES_Write8(membank|memaddr, PPU_Read8(ppuaddr+1));
							memaddr++;
							SNES_Write8(membank|memaddr, PPU_Read8(ppuaddr));
							memaddr++;
							SNES_Write8(membank|memaddr, PPU_Read8(ppuaddr+1));
							break;
					}
				}
				else
				{
					switch (paddrinc)
					{
						case 0:
							PPU_Write8(ppuaddr, SNES_Read8(membank|memaddr));
							break;
						case 1:
							PPU_Write8(ppuaddr, SNES_Read8(membank|memaddr));
							memaddr++;
							PPU_Write8(ppuaddr+1, SNES_Read8(membank|memaddr));
							break;
						case 2:
						case 6:
							PPU_Write8(ppuaddr, SNES_Read8(membank|memaddr));
							memaddr++;
							PPU_Write8(ppuaddr, SNES_Read8(membank|memaddr));
							break;
						case 3:
						case 7:
							PPU_Write8(ppuaddr, SNES_Read8(membank|memaddr));
							memaddr++;
							PPU_Write8(ppuaddr, SNES_Read8(membank|memaddr));
							memaddr++;
							PPU_Write8(ppuaddr+1, SNES_Read8(membank|memaddr));
							memaddr++;
							PPU_Write8(ppuaddr+1, SNES_Read8(membank|memaddr));
							break;
						case 4:
							PPU_Write8(ppuaddr, SNES_Read8(membank|memaddr));
							memaddr++;
							PPU_Write8(ppuaddr+1, SNES_Read8(membank|memaddr));
							memaddr++;
							PPU_Write8(ppuaddr+2, SNES_Read8(membank|memaddr));
							memaddr++;
							PPU_Write8(ppuaddr+3, SNES_Read8(membank|memaddr));
							break;
						case 5:
							PPU_Write8(ppuaddr, SNES_Read8(membank|memaddr));
							memaddr++;
							PPU_Write8(ppuaddr+1, SNES_Read8(membank|memaddr));
							memaddr++;
							PPU_Write8(ppuaddr, SNES_Read8(membank|memaddr));
							memaddr++;
							PPU_Write8(ppuaddr+1, SNES_Read8(membank|memaddr));
							break;
					}
				}
				
				if (!(params & 0x40))
					tableaddr += hdma_sizes[paddrinc];
				else
				{
					memaddr++;
					*(u16*)&chan[5] = memaddr;
				}
			}
			
			repeatflag--;
			if (!(repeatflag & 0x80))
			{
				if (!repeatflag)
				{
					chan[10] = SNES_Read8(tablebank|tableaddr);
					if(!chan[10])
					{
						if(chan[0] & 0x40)
						{
							u16 memaddr = SNES_Read16(tableaddr|tablebank);
							*(u16*)&chan[5] = memaddr;
							tableaddr++;
						}
						*(u16*)&chan[8] = tableaddr + 1;
						
						flag &= ~(1 << c);
						ended |= (1 << c);
						HDMA_Pause[c] = 1;
						continue;
					}

					tableaddr++;
					HDMA_Pause[c] = 0;
					
					if (chan[0] & 0x40)
					{
						u16 maddr = SNES_Read16(tableaddr|tablebank);
						*(u16*)&chan[5] = maddr;
						tableaddr += 2;
					}
				}
				else
				{
					chan[10] = repeatflag;
					HDMA_Pause[c] = 1;
				}
			}
			else
			{
				if (repeatflag == 0x80)
				{
					chan[10] = SNES_Read8(tablebank|tableaddr);
					if(!chan[10])
					{
						if(chan[0] & 0x40)
						{
							u16 memaddr = SNES_Read16(tableaddr|tablebank);
							*(u16*)&chan[5] = memaddr;
							tableaddr++;
						}
						*(u16*)&chan[8] = tableaddr + 1;
						
						flag &= ~(1 << c);
						ended |= (1 << c);
						HDMA_Pause[c] = 1;
						continue;
					}

					tableaddr++;
					HDMA_Pause[c] = 0;
					
					if (chan[0] & 0x40)
					{
						u16 maddr = SNES_Read16(tableaddr|tablebank);
						*(u16*)&chan[5] = maddr;
						tableaddr += 2;
					}
					
				}
				else
					chan[10] = repeatflag;
			}
			
			*(u16*)&chan[8] = tableaddr;
		}
	}
}
