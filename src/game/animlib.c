//-------------------------------------------------------------------------
/*
Copyright (C) 1996, 2003 - 3D Realms Entertainment

This file is part of Duke Nukem 3D version 1.5 - Atomic Edition

Duke Nukem 3D is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
aint32_t with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

Original Source: 1996 - Todd Replogle
Prepared for public release: 03/21/2003 - Charlie Wiederhold, 3D Realms
*/
//-------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#include "types.h"
#include "develop.h"
#include "util_lib.h"
//#include "_animlib.h"
#include "animlib.h"
#include "filesystem.h"   // kread, klseek (ANIM_SetStream)

//****************************************************************************
//
// GLOBALS
//
//****************************************************************************

//****************************************************************************
//
// LOCALS
//
//****************************************************************************
anim_t * anim=NULL;
static boolean Anim_Started = false;

// ANM files are little endian and their 16-bit words are not always aligned:
// read them byte by byte so the player also works on big endian CPUs that trap
// on unaligned accesses (the N64).
static uint16 anim_le16 (const byte *p)
   {
   return (uint16)(p[0] | (p[1] << 8));
   }

static uint32 anim_le32 (const byte *p)
   {
   return (uint32)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32)p[3] << 24));
   }

// Converts a large page descriptor copied from the file to native order.
static void anim_fix_lp (lp_descriptor *lp)
   {
   const byte *b = (const byte *)lp;
   lp_descriptor fixed;
   fixed.baseRecord = anim_le16(b);
   fixed.nRecords = anim_le16(b + 2);
   fixed.nBytes = anim_le16(b + 4);
   *lp = fixed;
   }

//****************************************************************************
//
//      CheckAnimStarted ()
//
//****************************************************************************

void CheckAnimStarted ( char  * funcname )
   {
   if (!Anim_Started)
      Error(EXIT_FAILURE, "ANIMLIB_%s: Anim has not been initialized\n",funcname);
   }
//****************************************************************************
//
//      findpage ()
//              - given a frame number return the large page number it resides in
//
//****************************************************************************

uint16 findpage (uint16 framenumber)
   {
   uint16 i;

   CheckAnimStarted ( "findpage" );
   for(i=0; i<anim->lpheader.nLps; i++)
      {
      if
         (
         anim->LpArray[i].baseRecord <= framenumber &&
         anim->LpArray[i].baseRecord + anim->LpArray[i].nRecords > framenumber
         )
         return(i);
      }
   return(i);
   }


//****************************************************************************
//
//      loadpage ()
//      - seek out and load in the large page specified
//
//****************************************************************************

static int32 anim_stream = -1;     // file the pages are read from (-1: in memory)

void ANIM_SetStream (int32 handle)
   {
   anim_stream = handle;
   }

void loadpage (uint16 pagenumber, uint16 *pagepointer)
   {
   int32 size;
   byte * buffer;

   CheckAnimStarted ( "loadpage" );
   buffer = anim->buffer;
   if (anim->curlpnum != pagenumber && anim_stream >= 0)
      {
      // Same as below, reading the page from the file.
      byte head[sizeof(lp_descriptor) + sizeof(uint16)];
      uint16 r;

      anim->curlpnum = pagenumber;
      klseek(anim_stream, 0xb00 + (pagenumber*0x10000), SEEK_SET);
      kread(anim_stream, head, sizeof(head));
      memcpy(&anim->curlp,head,sizeof(lp_descriptor));
      anim_fix_lp(&anim->curlp);
      size = anim->curlp.nBytes+(anim->curlp.nRecords*2);
      if (size > (int32)sizeof(anim->thepage))
         size = sizeof(anim->thepage);
      kread(anim_stream, pagepointer, size);
      for (r = 0; r < anim->curlp.nRecords; r++)
         pagepointer[r] = anim_le16((const byte *)&pagepointer[r]);
      return;
      }
   if (anim->curlpnum != pagenumber)
      {
      anim->curlpnum = pagenumber;
      buffer += 0xb00 + (pagenumber*0x10000);
      size = sizeof(lp_descriptor);
      memcpy(&anim->curlp,buffer,size);
      anim_fix_lp(&anim->curlp);
      buffer += size + sizeof(uint16);
      memcpy(pagepointer,buffer,anim->curlp.nBytes+(anim->curlp.nRecords*2));
      {
      // The page starts with the size of each record (16-bit, little endian).
      uint16 r;
      for (r = 0; r < anim->curlp.nRecords; r++)
         pagepointer[r] = anim_le16((const byte *)&pagepointer[r]);
      }
      }
   }


//****************************************************************************
//
//      CPlayRunSkipDump ()
//      - This version of the decompressor is here for portability to non PC's
//
//****************************************************************************

void CPlayRunSkipDump (byte *srcP, byte *dstP)
   {
   int8_t cnt;
   uint16 wordCnt;
   byte pixel;


nextOp:
   cnt = (int8_t ) *srcP++;
   if (cnt > 0)
      goto dump;
   if (cnt == 0)
      goto run;
   cnt -= 0x80;
   if (cnt == 0)
      goto longOp;
/* shortSkip */
   dstP += cnt;                    /* adding 7-bit count to 32-bit pointer */
   goto nextOp;
dump:
   do
      {
      *dstP++ = *srcP++;
      } while (--cnt);
   goto nextOp;
run:
   wordCnt = (byte)*srcP++;                /* 8-bit unsigned count */
   pixel = *srcP++;
   do
      {
      *dstP++ = pixel;
      } while (--wordCnt);

   goto nextOp;
longOp:
   wordCnt = anim_le16(srcP);
   srcP += sizeof(uint16);
   if ((int16)wordCnt <= 0)
      goto notLongSkip;       /* Do SIGNED test. */

/* longSkip. */
   dstP += wordCnt;
   goto nextOp;

notLongSkip:
   if (wordCnt == 0)
      goto stop;
   wordCnt -= 0x8000;              /* Remove sign bit. */
   if (wordCnt >= 0x4000)
      goto longRun;

/* longDump. */
   do
      {
      *dstP++ = *srcP++;
      } while (--wordCnt);
   goto nextOp;

longRun:
   wordCnt -= 0x4000;              /* Clear "longRun" bit. */
   pixel = *srcP++;
   do
      {
      *dstP++ = pixel;
      } while (--wordCnt);
   goto nextOp;

stop:   /* all done */
   ;
   }



//****************************************************************************
//
//      renderframe ()
//      - draw the frame sepcified from the large page in the buffer pointed to
//
//****************************************************************************

void renderframe (uint16 framenumber, uint16 *pagepointer)
{
   uint16 offset=0;
   uint16 i;
   uint16 destframe;
   byte *ppointer;

   CheckAnimStarted ( "renderframe" );
   destframe = framenumber - anim->curlp.baseRecord;

   for(i = 0; i < destframe; i++)
      offset += pagepointer[i];
      
   ppointer = (byte *)pagepointer;

   ppointer+=anim->curlp.nRecords*2+offset;
   if(ppointer[1])
      ppointer += (4 + (anim_le16(ppointer + 2) + (anim_le16(ppointer + 2) & 1)));
   else
      ppointer+=4;

   CPlayRunSkipDump (ppointer, anim->imagebuffer);
}


//****************************************************************************
//
//      drawframe ()
//      - high level frame draw routine
//
//****************************************************************************

void drawframe (uint16 framenumber)
   {
   CheckAnimStarted ( "drawframe" );
   loadpage(findpage(framenumber), anim->thepage);
   renderframe(framenumber, anim->thepage);
   }


//****************************************************************************
//
//      ANIM_LoadAnim ()
//
//****************************************************************************

void ANIM_LoadAnim (byte * buffer)
   {
   uint16 i;
   int32 size;

   if (!Anim_Started) Anim_Started = true;

   anim->buffer = buffer;
   anim->curlpnum = 0xffff;
   anim->currentframe = -1;
   size = sizeof(lpfileheader);
   memcpy(&anim->lpheader, buffer, size );
   // (only these header fields are used)
   anim->lpheader.nLps = anim_le16(buffer + 6);
   anim->lpheader.nRecords = anim_le32(buffer + 8);
   buffer += size+128;
   // load the color palette
   for (i = 0; i < 768; i += 3)
      {
      anim->pal[i+2] = *buffer++;
      anim->pal[i+1] = *buffer++;
      anim->pal[i] = *buffer++;
      buffer++;
      }
        // read in large page descriptors
   size = sizeof(anim->LpArray);
   memcpy(&anim->LpArray,buffer,size);
   for (i = 0; i < 256; i++)
      anim_fix_lp(&anim->LpArray[i]);
   }

//****************************************************************************
//
//      ANIM_FreeAnim ()
//
//****************************************************************************

void ANIM_FreeAnim ( void )
   {
   if (Anim_Started)
      {
//      SafeFree(anim);
      Anim_Started = false;
      }
   }

//****************************************************************************
//
//      ANIM_NumFrames ()
//
//****************************************************************************

int32 ANIM_NumFrames ( void )
   {
   CheckAnimStarted ( "NumFrames" );
   return anim->lpheader.nRecords;
   }

//****************************************************************************
//
//      ANIM_DrawFrame ()
//
//****************************************************************************

byte * ANIM_DrawFrame (int32 framenumber)
   {
   int32 cnt;

   CheckAnimStarted ( "DrawFrame" );
   if ((anim->currentframe != -1) && (anim->currentframe<=framenumber))
      {
      for (cnt = anim->currentframe; cnt < framenumber; cnt++)
          drawframe (cnt);
      }
   else
      {
      for (cnt = 0; cnt < framenumber; cnt++)
         drawframe (cnt);
      }
   anim->currentframe = framenumber;
   return anim->imagebuffer;
   }

//****************************************************************************
//
//      ANIM_GetPalette ()
//
//****************************************************************************

byte * ANIM_GetPalette ( void )
   {
   CheckAnimStarted ( "GetPalette" );
   return anim->pal;
   }
