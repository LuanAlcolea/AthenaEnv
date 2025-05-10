#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <malloc.h>
#include <math.h>
#include <fcntl.h>

#include <time.h>

#include <graphics.h>
#include <athena_math.h>
#include <dbgprintf.h>
#include <fntsys.h>

#include <owl_packet.h>

#include <libmpeg.h>

#define MAX_SIZE (1024 * 1024 * 10)

typedef struct {
	MPEGSequenceInfo* m_pInfo;
	void*             m_pData;
	packet_t          *m_XFerPck;
	int               m_TexAddr;
} InitCBParam;

static unsigned char* s_pMPEGData;
static unsigned char* s_pTransferPtr;
static unsigned int   s_MPEGDataSize;

static int   SetDMA(void*);
static void* InitCB(void*, MPEGSequenceInfo*);

InitCBParam lInfo;
int64_t lCurPTS;

void video_init() {
	MPEG_Initialize(SetDMA, NULL, InitCB, &lInfo, &lCurPTS);
	lInfo.m_TexAddr = graph_vram_allocate(0, 0, GS_PSM_32, GRAPH_ALIGN_BLOCK); // setup texture buffer address just after the framebuffer
}

void video_quit() {
	MPEG_Destroy();
}

int main(void) {
	int lFD = open("host:test.bin", O_RDONLY);
	size_t lSize;
	int64_t lPTS;

 	lSize = lseek(lFD, 0, SEEK_END);
 	lseek(lFD, 0, SEEK_SET);

	s_pMPEGData = ( unsigned char* )memalign(64, lSize = lSize > MAX_SIZE ? MAX_SIZE : lSize);

	read(lFD, s_pTransferPtr = s_pMPEGData, s_MPEGDataSize = lSize);

	close(lFD);

	while ( 1 ) { // during decoding scratchpad RAM from address 0x0000 to 0x3C00 is used by the decoder.
		if ( !MPEG_Picture(lInfo.m_pData, &lPTS)) {
	   		if (lInfo.m_pInfo->m_fEOF) break; // end of stream was detected (SetDMA function returned zero)
	   		else break; // MPEG sequence end code (0x000001B7) was detected
	  	}  

		dma_wait_fast(); // now transfer decoded picture data into texture area of GS RAM
		dma_channel_send_chain( DMA_CHANNEL_GIF, lInfo.m_XFerPck->data, lInfo.m_XFerPck->qwc, 0, 0);
	}  
}  

static int SetDMA(void* apUserData) {
	if ( s_pTransferPtr - s_pMPEGData >= s_MPEGDataSize ) return 0;

	dma_channel_wait(DMA_CHANNEL_toIPU, 0);
	dma_channel_send_normal(DMA_CHANNEL_toIPU, s_pTransferPtr, 2048 >> 4, 0, 0);
	s_pTransferPtr += 2048;

	return 1;
}  

static void* InitCB(void* apParam, MPEGSequenceInfo* apInfo) {
	int          lDataSize = apInfo->m_Width * apInfo->m_Height * 4;
	char*        retVal    = ( char* )memalign( 64, lDataSize);
	InitCBParam* lpParam   = ( InitCBParam* )apParam;
	int          lMBW      = ( apInfo->m_Width  ) >> 4;
	int          lMBH      = ( apInfo->m_Height ) >> 4;
	int          lTBW      = ( apInfo->m_Width + 63 ) >> 6;
	int          lTW       = draw_log2 ( apInfo->m_Width  );
	int          lTH       = draw_log2 ( apInfo->m_Height );
	int          lX, lY;
	char*        lpImg;
	qword_t*       q;

	lpParam->m_TexAddr >>= 6;

	lpParam->m_pData = lpImg = retVal;
	lpParam->m_pInfo = apInfo;
	SyncDCache(retVal, retVal + lDataSize);

	lpParam->m_XFerPck = packet_init((10 + 12 * lMBW * lMBH ) >> 1, PACKET_NORMAL);

	q = lpParam-> m_XFerPck->data;

	DMATAG_CNT(q, 3, 0, 0, 0);
	q++;
	PACK_GIFTAG(q,GIF_SET_TAG( 2, 0, 0, 0, 0, 1 ),GIF_REG_AD);
	q++;
	PACK_GIFTAG(q,GS_SET_TRXREG( 16, 16 ), GS_REG_TRXREG);
	q++;
	PACK_GIFTAG(q,GS_SET_BITBLTBUF( 0, 0, 0, lpParam->m_TexAddr, lTBW, GS_PSM_32 ), GS_REG_BITBLTBUF);
	q++;

	for ( lY = 0; lY < apInfo->m_Height; lY += 16 ) {
		for ( lX = 0; lX < apInfo->m_Width; lX += 16, lpImg  += 1024 ) {
			DMATAG_CNT(q, 4, 0, 0, 0);
			q++;
			PACK_GIFTAG(q,GIF_SET_TAG( 2, 0, 0, 0, 0, 1 ), GIF_REG_AD );
			q++;
			PACK_GIFTAG(q,GS_SET_TRXPOS( 0, 0, lX, lY, 0 ), GS_REG_TRXPOS );
			q++;
			PACK_GIFTAG(q,GS_SET_TRXDIR( 0 ), GS_REG_TRXDIR );
			q++;
			PACK_GIFTAG(q,GIF_SET_TAG( 64, 1, 0, 0, 2, 0), 0);
			q++;
			DMATAG_REF(q, 64, ( unsigned )lpImg, 0, 0, 0);
			q++;
		}  
	}  

	//DMATAG_END(q,0,0,0,0);
	//q++;

	lpParam-> m_XFerPck->qwc = q - lpParam-> m_XFerPck->data;

	// PACK_GIFTAG(q, GS_SET_TEX0( lpParam->m_TexAddr, lTBW, GS_PSM_32, lTW, lTH, 1, 1, 0, 0, 0, 0, 0 ), GS_REG_TEX0_1 );
	draw_image(NULL, 0, 0, 640, 512, 0, 0, apInfo->m_Width, apInfo->m_Height, 0x80808080);

	return retVal;
}  
