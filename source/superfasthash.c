// Copyright (c) 2010, Paul Hsieh
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
//   list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
// * Neither my name, Paul Hsieh, nor the names of any other contributors to the
//   code use may not be used to endorse or promote products derived from this
//   software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#include <3ds.h>

u32 SuperFastHash(const u8* data, u32 len) 
{
	u32 hash = len, tmp;

    len >>= 2;
    /* Main loop */
    for (; len > 0; len--) 
	{
        hash  += *(u16*)&data[0];
        tmp    = (*(u16*)&data[2] << 11) ^ hash;
        hash   = (hash << 16) ^ tmp;
        data  += 4;
        hash  += hash >> 11;
    }
	
    /* Force "avalanching" of final 127 bits */
    hash ^= hash << 3;
    hash += hash >> 5;
    hash ^= hash << 4;
    hash += hash >> 17;
    hash ^= hash << 25;
    hash += hash >> 6;
    return hash;
}

u32 SuperFastPalHash(const u8* data, u32 len) 
{
	u32 hash = len, tmp;

    len >>= 2;
	
	/* skip first halfword (palette entry 0, transparent) */
	tmp    = (*(u16*)&data[2] << 11) ^ hash;
	hash   = (hash << 16) ^ tmp;
	data  += 4;
	hash  += hash >> 11;
	len--;
	
    /* Main loop */
    for (; len > 0; len--) 
	{
        hash  += *(u16*)&data[0];
        tmp    = (*(u16*)&data[2] << 11) ^ hash;
        hash   = (hash << 16) ^ tmp;
        data  += 4;
        hash  += hash >> 11;
    }
	
    /* Force "avalanching" of final 127 bits */
    hash ^= hash << 3;
    hash += hash >> 5;
    hash ^= hash << 4;
    hash += hash >> 17;
    hash ^= hash << 25;
    hash += hash >> 6;
    return hash;
}
