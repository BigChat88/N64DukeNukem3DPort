// "Build Engine & Tools" Copyright (c) 1993-1997 Ken Silverman
// Ken Silverman's official web site: "http://www.advsys.net/ken"
// See the included license file "BUILDLIC.TXT" for license info.
// This file has been modified from Ken Silverman's original release

/* DDOI - This file is an attempt to reimplement a_nasm.asm in C */
/* FCS: However did that work: This is far from perfect but you have my eternal respect !!! */

#include "platform.h"
#include "build.h"
#include "draw.h"

int32_t pixelsAllowed = 10000000000;

uint8_t  *transluc = NULL;

static int transrev = 0;


#define shrd(a,b,c) (((b)<<(32-(c))) | ((a)>>(c)))
#define shld(a,b,c) (((b)>>(32-(c))) | ((a)<<(c)))















/* ---------------  WALLS RENDERING METHOD (USED TO BE HIGHLY OPTIMIZED ASSEMBLY) ----------------------------*/
extern int32_t asm1;
extern intptr_t asm2;
extern uint8_t *asm3;
extern int32_t asm4;

static uint8_t machxbits_al;
static uint8_t bitsSetup;
static uint8_t * textureSetup;
void sethlinesizes(int32_t i1, int32_t _bits, uint8_t * textureAddress)
{
    machxbits_al = i1;
    bitsSetup = _bits;
    textureSetup = textureAddress;
} 



//FCS:   Draw ceiling/floors
//Draw a line from destination in the framebuffer to framebuffer-numPixels
void hlineasm4(int32_t numPixels, int32_t shade, uint32_t i4, uint32_t i5, uint8_t *dest){

    int32_t shifter = ((256-machxbits_al) & 0x1f);
    uint32_t source;

    uint8_t * texture = textureSetup;
    uint8_t bits = bitsSetup;
    // Globals copied to locals: the byte store to *dest may alias any of them,
    // which otherwise forces a reload from memory on every pixel.
    const uint8_t *palette;
    const uint32_t step5 = asm1;
    const uint32_t step4 = asm2;

    shade = shade & 0xffffff00;
    palette = globalpalwritten + shade;
    numPixels++;

	if (!RENDER_DRAW_CEILING_AND_FLOOR)
		return;

#ifdef N64
    // Writing a byte to a line that is not in the data cache first reads the
    // whole 16-byte line from RDRAM, only to overwrite it. The span covers
    // [dest - numPixels + 1, dest] entirely: its whole lines are created in
    // the cache without reading them (CACHE Create Dirty Exclusive; a dirty
    // line that was there is written back first).
    {
        uintptr_t line = ((uintptr_t)dest - numPixels + 1 + 15) & ~(uintptr_t)15;
        uintptr_t end = ((uintptr_t)dest + 1) & ~(uintptr_t)15;
        for (; line < end; line += 16)
            __asm__ volatile ("cache 0xD, 0(%0)" : : "r"(line) : "memory");
    }

    // The span is written right to left. Once dest is the last byte of an
    // aligned word, four texels are packed into one 32-bit store (big endian:
    // the leftmost pixel is the most significant byte).
#define HL_TEXEL(v) \
    (source = shld(i5 >> shifter, i4, bits), i5 -= step5, i4 -= step4, \
     (uint32_t)palette[texture[source]] << (v))
    while (numPixels && ((uintptr_t)(dest + 1) & 3)) {
        *dest-- = HL_TEXEL(0);
        numPixels--;
    }
    while (numPixels >= 4) {
        uint32_t word = HL_TEXEL(0);
        word |= HL_TEXEL(8);
        word |= HL_TEXEL(16);
        word |= HL_TEXEL(24);
        typedef uint32_t __attribute__((may_alias)) u32a;
        *(u32a *)(dest - 3) = word;
        dest -= 4;
        numPixels -= 4;
    }
    while (numPixels) {
        *dest-- = HL_TEXEL(0);
        numPixels--;
    }
#undef HL_TEXEL
    return;
#endif

    while (numPixels) {

	    source = i5 >> shifter;
	    source = shld(source,i4,bits);
	    source = texture[source];

		if (PIXEL_BUDGET())
			*dest = palette[source];

	    dest--;

	    i5 -= step5;
	    i4 -= step4;

	    numPixels--;

    }
}

static int32_t rmach_eax;
static int32_t rmach_ebx;
static int32_t rmach_ecx;
static int32_t rmach_edx;
static int32_t rmach_esi;
void setuprhlineasm4(int32_t i1, int32_t i2, int32_t i3, int32_t i4, int32_t i5, int32_t i6)
{
    rmach_eax = i1;
    rmach_ebx = i2;
    rmach_ecx = i3;
    rmach_edx = i4;
    rmach_esi = i5;
} 


void rhlineasm4(int32_t i1, uint8_t* texture, int32_t i3, uint32_t i4, uint32_t i5, int32_t dest)
{
    uint32_t ebp = dest - i1;
    uint32_t rmach6b = ebp-1;
    int32_t numPixels;
    
    if (i1 <= 0) return;

    numPixels = i1;
    do {
		
		

	    i3 = ((i3&0xffffff00)|(*texture));
	    i4 -= rmach_eax;
	    ebp = (((i4+rmach_eax) < i4) ? -1 : 0);
	    i5 -= rmach_ebx;
        
	    if ((i5 + rmach_ebx) < i5)
            texture -= (rmach_ecx+1);
	    else
            texture -= rmach_ecx;
        
	    ebp &= rmach_esi;
	    i1 = ((i1&0xffffff00)|(((uint8_t *)i3)[rmach_edx]));

		if (PIXEL_BUDGET())
			 ((uint8_t *)rmach6b)[numPixels] = (i1&0xff);

	    texture -= ebp;
	    numPixels--;
    } while (numPixels);
}

static int32_t rmmach_eax;
static int32_t rmmach_ebx;
static int32_t rmmach_ecx;
static int32_t rmmach_edx;
static int32_t setupTileHeight;
void setuprmhlineasm4(int32_t i1, int32_t i2, int32_t i3, int32_t i4, int32_t tileHeight, int32_t i6)
{
    rmmach_eax = i1;
    rmmach_ebx = i2;
    rmmach_ecx = i3;
    rmmach_edx = i4;
    setupTileHeight = tileHeight;
} 


//FCS: ????
void rmhlineasm4(int32_t i1, intptr_t shade, int32_t colorIndex, int32_t i4, int32_t i5, int32_t dest)
{
    uint32_t ebp = dest - i1;
    uint32_t rmach6b = ebp-1;
    int32_t numPixels;
    
    if (i1 <= 0)
        return;

    numPixels = i1;
    do {

	

	    colorIndex = ((colorIndex&0xffffff00)|(*((uint8_t *)shade)));
	    i4 -= rmmach_eax;
	    ebp = (((i4+rmmach_eax) < i4) ? -1 : 0);
	    i5 -= rmmach_ebx;
        
	    if ((i5 + rmmach_ebx) < i5)
            shade -= (rmmach_ecx+1);
	    else
            shade -= rmmach_ecx;
        
	    ebp &= setupTileHeight;
        
        //Check if this colorIndex is the transparent color (255).
	    if ((colorIndex&0xff) != 255) {
			if (PIXEL_BUDGET())
			{
				i1 = ((i1&0xffffff00)|(((uint8_t  *)colorIndex)[rmmach_edx]));
				((uint8_t  *)rmach6b)[numPixels] = (i1&0xff);
			}
	    }
        
	    shade -= ebp;
	    numPixels--;
        
    } while (numPixels);
} 


//Variable used to draw column.
//This is how much you have to skip in the framebuffer in order to be one pixel below.
static int32_t bytesperline;
void setBytesPerLine(int32_t _bytesperline)
{
    bytesperline = _bytesperline;
} 



static uint8_t  mach3_al;

//FCS:  RENDER TOP AND BOTTOM COLUMN
int32_t prevlineasm1(int32_t i1, uint8_t* palette, int32_t i3, int32_t i4, uint8_t  *source, uint8_t  *dest)
{


    if (i3 == 0)
    {
		if (!RENDER_DRAW_TOP_AND_BOTTOM_COLUMN)
            return 0;

	    i1 += i4;
        i4 = ((uint32_t)i4) >> mach3_al;
	    i4 = (i4&0xffffff00) | source[i4];

		if (PIXEL_BUDGET())
			*dest = palette[i4];

		

	    return i1;
    } else {
	    return vlineasm1(i1,palette,i3,i4,source,dest);
    }
}


//FCS: This is used to draw wall border vertical lines
int32_t vlineasm1(int32_t vince, uint8_t* palookupoffse, int32_t numPixels, int32_t vplce, uint8_t* texture, uint8_t* dest)
{
    uint32_t temp;
    const int shift = mach3_al;
    const int32_t pitch = bytesperline;

    if (!RENDER_DRAW_WALL_BORDERS)
		return vplce;

    numPixels++;
    while (numPixels)
    {
	    temp = ((uint32_t)vplce) >> shift;

	    temp = texture[temp];

		if (PIXEL_BUDGET())
			*dest = palookupoffse[temp];

		vplce += vince;
	    dest += pitch;
	    numPixels--;
    }
    return vplce;
} 


int32_t tvlineasm1(int32_t i1, uint8_t  * texture, int32_t numPixels, int32_t i4, uint8_t  *source, uint8_t  *dest)
{
    uint8_t shiftValue = (globalshiftval & 0x1f);
    const int32_t pitch = bytesperline;
    const uint8_t *table = transluc;
    const int reverse = transrev;
    
	numPixels++;
	while (numPixels)
	{
		uint32_t temp = i4;
		temp >>= shiftValue;
		temp = source[temp];

	    //255 is the index for transparent color index. Skip drawing this pixel. 
		if (temp != 255)
		{
			uint16_t colorIndex;
            
			colorIndex = texture[temp];
			colorIndex |= ((*dest)<<8);
            
			if (reverse) 
				colorIndex = ((colorIndex>>8)|(colorIndex<<8));
            
			if (PIXEL_BUDGET())
				*dest = table[colorIndex];
		}
        
		i4 += i1;
        
        //We are drawing a column ?!
		dest += pitch;
		numPixels--;
	}
	return i4;
} /* tvlineasm1 */


static uint8_t  tran2shr;
static uint32_t tran2pal_ebx;
static uint32_t tran2pal_ecx;
void setuptvlineasm2(int32_t i1, int32_t i2, int32_t i3)
{
	tran2shr = (i1&0x1f);
	tran2pal_ebx = i2;
	tran2pal_ecx = i3;
} /* */


void tvlineasm2(uint32_t i1, uint32_t i2, uintptr_t i3, uintptr_t i4, uint32_t i5, uintptr_t i6)
{
	uint32_t ebp = i1;
	uint32_t tran2inca = i2;
	uint32_t tran2incb = asm1;
	uintptr_t tran2bufa = i3;
	uintptr_t tran2bufb = i4;
	uintptr_t tran2edi = asm2;
	uintptr_t tran2edi1 = asm2 + 1;

	i6 -= asm2;

	do {
		
		i1 = i5 >> tran2shr;
		i2 = ebp >> tran2shr;
		i5 += tran2inca;
		ebp += tran2incb;
		i3 = ((uint8_t  *)tran2bufa)[i1];
		i4 = ((uint8_t  *)tran2bufb)[i2];
		if (i3 == 255) { // skipdraw1
			if (i4 != 255) { // skipdraw3
				uint16_t val;
				val = ((uint8_t  *)tran2pal_ecx)[i4];
				val |= (((uint8_t  *)i6)[tran2edi1]<<8);

				if (transrev) 
					val = ((val>>8)|(val<<8));

				if (PIXEL_BUDGET())
					((uint8_t  *)i6)[tran2edi1] = transluc[val];
			}
		} else if (i4 == 255) { // skipdraw2
			uint16_t val;
			val = ((uint8_t  *)tran2pal_ebx)[i3];
			val |= (((uint8_t  *)i6)[tran2edi]<<8);

			if (transrev) 
                val = ((val>>8)|(val<<8));

			if (PIXEL_BUDGET())
				((uint8_t  *)i6)[tran2edi] = transluc[val];
		} else {
			uint16_t l = ((uint8_t  *)i6)[tran2edi]<<8;
			uint16_t r = ((uint8_t  *)i6)[tran2edi1]<<8;
			l |= ((uint8_t  *)tran2pal_ebx)[i3];
			r |= ((uint8_t  *)tran2pal_ecx)[i4];
			if (transrev) {
				l = ((l>>8)|(l<<8));
				r = ((r>>8)|(r<<8));
			}
			if (PIXEL_BUDGET())
			{
				((uint8_t  *)i6)[tran2edi] = transluc[l];
				((uint8_t  *)i6)[tran2edi1] =transluc[r];
				PIXEL_SPEND();
			}
		}
		i6 += bytesperline;
	} while (i6 > i6 - bytesperline);
	asm1 = i5;
	asm2 = ebp;
} 



static uint8_t  machmv;
int32_t mvlineasm1(int32_t vince, uint8_t* palookupoffse, int32_t i3, int32_t vplce, uint8_t* texture, uint8_t  *dest)
{
    uint32_t temp;
    const int shift = machmv;
    const int32_t pitch = bytesperline;

    for(;i3>=0;i3--)
    {
		temp = ((uint32_t)vplce) >> shift;
	    temp = texture[temp];

	    if (temp != 255) 
		{
			if (PIXEL_BUDGET())
			*dest = palookupoffse[temp];
		}

	    vplce += vince;
	    dest += pitch;
    }
    return vplce;
}


void setupvlineasm(int32_t i1)
{
    mach3_al = (i1&0x1f);
}

//FCS This is used to fill the inside of a wall (so it draws VERTICAL column, always).
void vlineasm4(int32_t columnIndex, intptr_t framebuffer)
{

	if (!RENDER_DRAW_WALL_INSIDE)
		return ;

    {
        // Draws 4 adjacent columns of columnIndex rows (ylookup[columnIndex]
        // bytes) starting at framebuffer. All the per-column state lives in
        // locals: the byte stores may alias the globals, which would otherwise
        // force the compiler to reload them for every pixel.
        const int shift = mach3_al;
        const int32_t pitch = bytesperline;
        uint32_t p0 = vplce[0], p1 = vplce[1], p2 = vplce[2], p3 = vplce[3];
        const uint32_t v0 = vince[0], v1 = vince[1], v2 = vince[2], v3 = vince[3];
        const uint8_t *t0 = (const uint8_t *)bufplce[0], *t1 = (const uint8_t *)bufplce[1];
        const uint8_t *t2 = (const uint8_t *)bufplce[2], *t3 = (const uint8_t *)bufplce[3];
        const uint8_t *c0 = palookupoffse[0], *c1 = palookupoffse[1];
        const uint8_t *c2 = palookupoffse[2], *c3 = palookupoffse[3];
        uint8_t *dest = (uint8_t *)framebuffer;
        int32_t rows = ylookup[columnIndex] / pitch;

        if (rows <= 0)
            return;

        do {
            if (PIXEL_BUDGET())
            {
                dest[0] = c0[t0[p0 >> shift]];
                dest[1] = c1[t1[p1 >> shift]];
                dest[2] = c2[t2[p2 >> shift]];
                dest[3] = c3[t3[p3 >> shift]];
            }
            p0 += v0; p1 += v1; p2 += v2; p3 += v3;
            dest += pitch;
        } while (--rows);

        vplce[0] = p0; vplce[1] = p1; vplce[2] = p2; vplce[3] = p3;
    }
} 


void setupmvlineasm(int32_t i1)
{
    //Only keep 5 first bits
    machmv = (i1&0x1f);
} 


void mvlineasm4(int32_t column, intptr_t framebufferOffset)
{
    // Same as vlineasm4, but color 255 is transparent. Globals are copied to
    // locals for the same aliasing reason.
    const int shift = machmv;
    const int32_t pitch = bytesperline;
    uint32_t p[4], temp;
    const uint32_t v0 = vince[0], v1 = vince[1], v2 = vince[2], v3 = vince[3];
    const uint8_t *t0 = (const uint8_t *)bufplce[0], *t1 = (const uint8_t *)bufplce[1];
    const uint8_t *t2 = (const uint8_t *)bufplce[2], *t3 = (const uint8_t *)bufplce[3];
    const uint8_t *c0 = palookupoffse[0], *c1 = palookupoffse[1];
    const uint8_t *c2 = palookupoffse[2], *c3 = palookupoffse[3];
    uint8_t *dest = (uint8_t *)framebufferOffset;
    int32_t rows = ylookup[column] / pitch;

    p[0] = vplce[0]; p[1] = vplce[1]; p[2] = vplce[2]; p[3] = vplce[3];

    if (rows <= 0)
        return;

    do {

		if (!PIXEL_BUDGET_LEFT())
			break;

        temp = t0[p[0] >> shift]; if (temp != 255) dest[0] = c0[temp];
        temp = t1[p[1] >> shift]; if (temp != 255) dest[1] = c1[temp];
        temp = t2[p[2] >> shift]; if (temp != 255) dest[2] = c2[temp];
        temp = t3[p[3] >> shift]; if (temp != 255) dest[3] = c3[temp];
        p[0] += v0; p[1] += v1; p[2] += v2; p[3] += v3;
        dest += pitch;

    } while (--rows);

    vplce[0] = p[0]; vplce[1] = p[1]; vplce[2] = p[2]; vplce[3] = p[3];
} 
/* END ---------------  WALLS RENDERING METHOD (USED TO BE HIGHLY OPTIMIZED ASSEMBLY) ----------------------------*/





























/* ---------------  SPRITE RENDERING METHOD (USED TO BE HIGHLY OPTIMIZED ASSEMBLY) ----------------------------*/
static int32_t spal_eax;
static int32_t smach_eax;
static int32_t smach2_eax;
static int32_t smach5_eax;
static int32_t smach_ecx;
void setupspritevline(int32_t i1, int32_t i2, int32_t i3, int32_t i4, int32_t i5, int32_t i6)
{
    spal_eax = i1;
    smach_eax = (i5<<16);
    smach2_eax = (i5>>16)+i2;
    smach5_eax = smach2_eax + i4;
    smach_ecx = i3;
} 


void spritevline(int32_t i1, uint32_t i2, int32_t i3, uint32_t i4, uint8_t* source, uint8_t* dest)
{
    

setup:

    i2 += smach_eax;
    i1 = (i1&0xffffff00) | (*source&0xff);
    if ((i2 - smach_eax) > i2) 
		source += smach2_eax + 1;
    else 
		source += smach2_eax;

    while(1) {
        
        i1 = (i1&0xffffff00) | (((uint8_t  *)spal_eax)[i1]&0xff);
        
        if (PIXEL_BUDGET())
            *dest = i1;
        
        dest += bytesperline;

        i4 += smach_ecx;
        i4--;
        if (!((i4 - smach_ecx) > i4) && i4 != 0)
            goto setup;
        
        if (i4 == 0) 
            return;
        
        i2 += smach_eax;
        
        i1 = (i1&0xffffff00) | (*source&0xff);
        
        if ((i2 - smach_eax) > i2) 
            source += smach5_eax + 1;
        else 
            source += smach5_eax;
    }
}


static int32_t mspal_eax;
static int32_t msmach_eax;
static int32_t msmach2_eax;
static int32_t msmach5_eax;
static int32_t msmach_ecx;
void msetupspritevline(int32_t i1, int32_t i2, int32_t i3, int32_t i4, int32_t i5, int32_t i6)
{
    mspal_eax = i1;
    msmach_eax = (i5<<16);
    msmach2_eax = (i5>>16)+i2;
    msmach5_eax = smach2_eax + i4;
    msmach_ecx = i3;
} 


void mspritevline(int32_t colorIndex, int32_t i2, int32_t i3, int32_t i4, uint8_t  * source, uint8_t  * dest)
{
 
setup:
    i2 += smach_eax;
    
    colorIndex = (colorIndex&0xffffff00) | (*source&0xff);
    
    if ((i2 - smach_eax) > i2) 
        source += smach2_eax + 1;
    else 
        source += smach2_eax;

	while(1){
    
        //Skip transparent pixels (index=255)
        if ((colorIndex&0xff) != 255)
        {
            colorIndex = (colorIndex&0xffffff00) | (((uint8_t  *)spal_eax)[colorIndex]&0xff);
            
            if (PIXEL_BUDGET())
                *dest = colorIndex;
        }
   
        dest += bytesperline;
        i4 += smach_ecx;
        i4--;
    
        if (!((i4 - smach_ecx) > i4) && i4 != 0)
            goto setup;
   
        if (i4 == 0) 
            return;
    
        i2 += smach_eax;
    
        colorIndex = (colorIndex&0xffffff00) | (*source&0xff);
    
        if ((i2 - smach_eax) > i2) 
            source += smach5_eax + 1;
        else 
            source += smach5_eax;
    }
}


uint8_t * tspal;
uint32_t tsmach_eax1;
uint32_t adder;
uint32_t tsmach_eax3;
uint32_t tsmach_ecx;
void tsetupspritevline(uint8_t * palette, int32_t i2, int32_t i3, int32_t i4, int32_t i5)
{
	tspal = palette;
	tsmach_eax1 = i5 << 16;
	adder = (i5 >> 16) + i2;
	tsmach_eax3 = adder + i4;
	tsmach_ecx = i3;
} 


/*
 FCS: Draw a sprite vertical line of pixels.
 */
void DrawSpriteVerticalLine(int32_t i2, int32_t numPixels, uint32_t i4, uint8_t  * texture, uint8_t  * dest)
{
    uint8_t colorIndex;
    
	while (numPixels)
	{
		numPixels--;
        
		if (numPixels != 0)
		{
			
			i4 += tsmach_ecx;
            
			if (i4 < (i4 - tsmach_ecx)) 
                adder = tsmach_eax3;
            
			colorIndex = *texture;
            
			i2 += tsmach_eax1;
			if (i2 < (i2 - tsmach_eax1)) 
                texture++;
            
			texture += adder;
			
            //255 is the index of the transparent color: Do not draw it.
			if (colorIndex != 255)
			{
				uint16_t val;
				val = tspal[colorIndex];
				val |= (*dest)<<8;

				if (transrev) 
					val = ((val>>8)|(val<<8));

				colorIndex = transluc[val];

				if (PIXEL_BUDGET())
					*dest = colorIndex;
			}
            
            //Move down one pixel on the framebuffer
			dest += bytesperline;
		}

		
	}
} 
/* END---------------  SPRITE RENDERING METHOD (USED TO BE HIGHLY OPTIMIZED ASSEMBLY) ----------------------------*/
























/* ---------------  FLOOR/CEILING RENDERING METHOD (USED TO BE HIGHLY OPTIMIZED ASSEMBLY) ----------------------------*/

void settrans(int32_t type){
	transrev = type;
}

static uint8_t  * textureData;
static uint8_t  * mmach_asm3;
static int32_t mmach_asm1;
static int32_t mmach_asm2;

void mhline(uint8_t  * texture, int32_t i2, int32_t numPixels, int32_t i4, int32_t i5, uint8_t* dest)
{
    textureData = texture;
    mmach_asm3 = asm3;
    mmach_asm1 = asm1;
    mmach_asm2 = asm2;
    mhlineskipmodify(i2,numPixels>>16,i5,dest);
}


static uint8_t  mshift_al = 26;
static uint8_t  mshift_bl = 6;
void mhlineskipmodify( uint32_t i2, int32_t numPixels, int32_t i5, uint8_t* dest)
{
    uint32_t ebx;
    int32_t colorIndex;
    
    while (numPixels >= 0)
    {
	    ebx = i2 >> mshift_al;
	    ebx = shld (ebx, (uint32_t)i5, mshift_bl);
	    colorIndex = textureData[ebx];

        //Skip transparent color.
		if ((colorIndex&0xff) != 0xff){
            if (PIXEL_BUDGET())
				*dest = mmach_asm3[colorIndex];
        }
	    i2 += mmach_asm1;
	    i5 += mmach_asm2;
	    dest++;
	    numPixels--;

		
    }
}


void msethlineshift(int32_t i1, int32_t i2)
{
    i1 = 256-i1;
    mshift_al = (i1&0x1f);
    mshift_bl = (i2&0x1f);
} /* msethlineshift */


static uint8_t * tmach_eax;
static uint8_t * tmach_asm3;
static int32_t tmach_asm1;
static int32_t tmach_asm2;

void thline(uint8_t  * i1, int32_t i2, int32_t i3, int32_t i4, int32_t i5, uint8_t * i6)
{
    tmach_eax = i1;
    tmach_asm3 = asm3;
    tmach_asm1 = asm1;
    tmach_asm2 = asm2;
    thlineskipmodify(asm2,i2,i3,i4,i5,i6);
}

static uint8_t  tshift_al = 26;
static uint8_t  tshift_bl = 6;
void thlineskipmodify(int32_t i1, uint32_t i2, uint32_t i3, int32_t i4, int32_t i5, uint8_t * i6)
{
    uint32_t ebx;
    int counter = (i3>>16);
    while (counter >= 0)
    {
	    ebx = i2 >> tshift_al;
	    ebx = shld (ebx, (uint32_t)i5, tshift_bl);
	    i1 = tmach_eax[ebx];
	    if ((i1&0xff) != 0xff)
	    {
		    uint16_t val = tmach_asm3[i1];
		    val |= (*i6)<<8;

		    if (transrev) 
				val = ((val>>8)|(val<<8));

			if (PIXEL_BUDGET())
			 *i6 = transluc[val];
	    }

	    i2 += tmach_asm1;
	    i5 += tmach_asm2;
	    i6++;
	    counter--;

		
    }
} 


void tsethlineshift(int32_t i1, int32_t i2)
{
    i1 = 256-i1;
    tshift_al = (i1&0x1f);
    tshift_bl = (i2&0x1f);
}




static intptr_t slopemach_ebx;
static int32_t slopemach_ecx;
static int32_t slopemach_edx;
static uint8_t  slopemach_ah1;
static uint8_t  slopemach_ah2;
static float asm2_f;
typedef union { unsigned int i; float f; } bitwisef2i;
void setupslopevlin(int32_t i1, intptr_t i2, int32_t i3)
{
    bitwisef2i c;
    slopemach_ebx = i2;
    slopemach_ecx = i3;
    slopemach_edx = (1<<(i1&0x1f)) - 1;
    slopemach_edx <<= ((i1&0x1f00)>>8);
    slopemach_ah1 = 32-((i1&0x1f00)>>8);
    slopemach_ah2 = (slopemach_ah1 - (i1&0x1f)) & 0x1f;
    c.f = asm2_f = (float)asm1;
    asm2 = c.i;
}

extern int32_t reciptable[2048];
extern int32_t globalx3, globaly3;
extern int32_t fpuasm;
#define low32(a) ((a&0xffffffff))
#define high32(a) ((int)(((__int64)a&(__int64)0xffffffff00000000)>>32))

//FCS: Render RENDER_SLOPPED_CEILING_AND_FLOOR
#ifdef N64
/*
 * N64: the same maths as the port of the x86 assembly below, written as
 * plain C. The original kept the pixel count in the low byte of the u step
 * and the pixel in the low byte of the v step (x86 register tricks): in C
 * that cost byte merges and masks on every pixel. Here the steps are whole
 * (they differed in their lowest 8 bits of fraction, invisible), and the
 * pixel loop is two shifts, a mask, two table reads and a store.
 *
 * Draws i4 pixels up from i1: the texel at ((u >> ah2) & mask) + (v >> ah1),
 * through the shade table of its row (the palookup offsets at i3, going
 * down); u, v step linearly over 8 pixels between exact 1/z values.
 */
void slopevlin(intptr_t i1, uint32_t i2, int32_t i3, int32_t i4, int32_t i5, int32_t i6)
{
    const int32_t pitch = -slopemach_ecx;           // (slopemach_ecx is -bytesperline)
    const uint8_t *tex = (const uint8_t *)slopemach_ebx;
    const uint32_t umask = (uint32_t)slopemach_edx;
    const int ush = slopemach_ah2, vsh = slopemach_ah1;
    const int32_t gx3 = globalx3, gy3 = globaly3;
    const float da = asm2_f;
    const uint32_t *pal = (const uint32_t *)i3;
    uint8_t *dest = (uint8_t *)i1;
    uint32_t u = (uint32_t)i5 + (uint32_t)gx3 * (i2 << 3);
    uint32_t v = (uint32_t)i6 + (uint32_t)gy3 * (i2 << 3);
    float a = (float)(int32_t)asm3 + da;
    int32_t left = i4;

    if (!RENDER_SLOPPED_CEILING_AND_FLOOR)
        return;

    do {
        bitwisef2i c;
        uint32_t r, e, sign, du, dv;
        int n;

        // Fixed point 1/a from the float's exponent and a table (as the asm).
        c.f = a;
        r = c.i;
        sign = ((int32_t)r < 0) ? 0xffffffff : 0;
        e = ((r << 1) >> 24) - 2;
        r = (uint32_t)reciptable[(((r << 1) & 0xffe000) >> 11) / 4] >> (e & 0x1f);
        r ^= sign;

        du = (uint32_t)gx3 * (r - i2);
        dv = (uint32_t)gy3 * (r - i2);
        i2 = r;
        a += da;

        n = left < 8 ? left : 8;
        while (n--)
        {
            uint8_t t = tex[((u >> ush) & umask) + (v >> vsh)];
            *dest = ((const uint8_t *)(uintptr_t)*pal)[t];
            pal--;
            dest -= pitch;
            u += du;
            v += dv;
        }
        left -= 8;
    } while (left > 0);
}
#else
void slopevlin(intptr_t i1, uint32_t i2, int32_t i3, int32_t i4, int32_t i5, int32_t i6)
{
    bitwisef2i c;
    uint32_t ecx,eax,ebx,edx,esi,edi;
//This is so bad to cast asm3 to int then float :( !!!
    float a = (float)(int32_t) asm3 + asm2_f;
    i1 -= slopemach_ecx;
    esi = i5 + low32((__int64)globalx3 * (__int64)(i2<<3));
    edi = i6 + low32((__int64)globaly3 * (__int64)(i2<<3));
    ebx = i4;

	if (!RENDER_SLOPPED_CEILING_AND_FLOOR)
		return;

    do {
	    // -------------
	    // All this is calculating a fixed point approx. of 1/a
	    c.f = a;
	    fpuasm = eax = c.i;
	    edx = (((int32_t)eax) < 0) ? 0xffffffff : 0;
	    eax = eax << 1;
	    ecx = (eax>>24);	//  exponent
	    eax = ((eax&0xffe000)>>11);
	    ecx = ((ecx&0xffffff00)|((ecx-2)&0xff));
	    eax = reciptable[eax/4];
	    eax >>= (ecx&0x1f);
	    eax ^= edx;
	    // -------------
	    edx = i2;
	    i2 = eax;
	    eax -= edx;
	    ecx = low32((__int64)globalx3 * (__int64)eax);
	    eax = low32((__int64)globaly3 * (__int64)eax);
	    a += asm2_f;

	    asm4 = ebx;
	    ecx = ((ecx&0xffffff00)|(ebx&0xff));
	    if (ebx >= 8) ecx = ((ecx&0xffffff00)|8);

	    ebx = esi;
	    edx = edi;
	    while ((ecx&0xff))
	    {
		    ebx >>= slopemach_ah2;
		    esi += ecx;
		    edx >>= slopemach_ah1;
		    ebx &= slopemach_edx;
		    edi += eax;
		    i1 += slopemach_ecx;
		    edx = ((edx&0xffffff00)|((((uint8_t  *)(ebx+edx))[slopemach_ebx])));
		    ebx = *((uint32_t*)i3); // register trickery
		    i3 -= 4;
		    eax = ((eax&0xffffff00)|(*((uint8_t  *)(ebx+edx))));
		    ebx = esi;

			if (PIXEL_BUDGET())
				*((uint8_t  *)i1) = (eax&0xff);

		    edx = edi;
		    ecx = ((ecx&0xffffff00)|((ecx-1)&0xff));

			
	    }
	    ebx = asm4;
	    ebx -= 8;	// BITSOFPRECISIONPOW

		

    } while ((int32_t)ebx > 0);
}


#endif

/* END ---------------  FLOOR/CEILING RENDERING METHOD (USED TO BE HIGHLY OPTIMIZED ASSEMBLY) ----------------------------*/
