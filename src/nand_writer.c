/* --------------------------------------------------------------------------
    FILE        : nandwriter.c                                                   
    PURPOSE     : NAND writer main program
    PROJECT     : DA8xx CCS NAND Flashing Utility (for use on DM355 EVM)
    AUTHOR      : Daniel Allred
    DESC        : CCS-based utility to flash the DM644x in preparation for 
                  NAND booting
 ----------------------------------------------------------------------------- */

#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "tistdtypes.h"
#include "device.h"
#include "debug.h"
#include "nandwriter.h"
#include "nand.h"
#include "device_nand.h"
#include "util.h"

/************************************************************
* Local Macro Declarations                                  *
************************************************************/

#define APP_START_BLK   5
#define NANDWIDTH_16
#define NANDStart       (0x62000000)
#define FBASE	        (0x62000000)

/************************************************************
* Local Function Declarations                               *
************************************************************/

static Uint32 nandwriter(void);
static Uint8 DEVICE_ASYNC_MEM_IsNandReadyPin(ASYNC_MEM_InfoHandle hAsyncMemInfo);
static Uint32 LOCAL_writeData(NAND_InfoHandle hNandInfo, Uint8 *srcBuf, Uint32 totalPageCnt);

/************************************************************
* Global Variable Definitions
************************************************************/

static Uint8* gNandTx;
static Uint8* gNandRx;
extern VUint32 __FAR__ DDRStart;
extern __FAR__ Uint32 EXTERNAL_RAM_START, EXTERNAL_RAM_END;

/************************************************************
* Global Function Definitions                               *
************************************************************/

void main( void )
{
    int status;

    /// Init memory alloc pointer
    UTIL_setCurrMemPtr(0);

    // Execute the NAND flashing
    status = nandwriter();

    if (status != E_PASS) DEBUG_printString("\n\nNAND flashing failed!\r\n");
    else DEBUG_printString( "\n\nNAND boot preparation was successful!\r\n" );
}


/************************************************************
* Local Function Definitions                                *
************************************************************/

static Uint32 nandwriter()
{
    FILE *fPtr;
    NAND_InfoHandle hNandInfo;
    Uint8 *aisPtr, fileName[256];
	Uint16 devID16[4];
    Uint32 status, numPagesAIS, i = 0, aisFileSize = 0,aisAllocSize = 0;
	VUint16 *addr_flash;
	ASYNC_MEM_InfoHandle dummy;
	Bool status_reached_zero = FALSE;

	addr_flash = (VUint16 *)(FBASE + DEVICE_NAND_CLE_OFFSET);
	for(i=0;i<512;i++){}
	*addr_flash = (VUint16)0xFF; //reset command

	i=0;
	do
	{
		status = DEVICE_ASYNC_MEM_IsNandReadyPin(dummy);
		if (status == 0) status_reached_zero = TRUE;
		i++;
		if (i >= 1000) break;
	}
	while( !status || (status_reached_zero == FALSE) );

	/// write getinfo command
	addr_flash = (VUint16 *)(FBASE + DEVICE_NAND_ALE_OFFSET);
	*addr_flash = (VUint16)NAND_ONFIRDIDADD;

	for(i=0;i<512;i++){}

	for(i=0;i<4;i++)
	{
		addr_flash = (VUint16 *)(FBASE);
		devID16[i] = *addr_flash;
	}

    // Initialize NAND Flash (NANDStart = 0x62000000)
    hNandInfo = NAND_open((Uint32)NANDStart, DEVICE_BUSWIDTH_16BIT ); 
    if (hNandInfo == NULL) return E_FAIL;

    // Erase the nand flash
    DEBUG_printString("Do you want to global erase NAND flash?");
    DEBUG_readString(fileName);
    fflush(stdin);
    if (strcmp(fileName,"y") == 0)
    {
        if (NAND_globalErase(hNandInfo) != E_PASS) DEBUG_printString("\tERROR: NAND global erase failed!");
    }

    // Read the file from host
    DEBUG_printString("Enter the binary AIS file name to flash (enter 'none' to skip) :\r\n");
    DEBUG_readString(fileName);
    fflush(stdin);

    if (strcmp(fileName,"none") != 0)
    {
        // Open an File from the hard drive
        fPtr = fopen(fileName, "rb");
        if(fPtr == NULL)
        {
            DEBUG_printString("\tERROR: File ");
            DEBUG_printString(fileName);
            DEBUG_printString(" open failed.\r\n");
            return E_FAIL;
        }

        // Read file size
        fseek(fPtr,0,SEEK_END);
        aisFileSize = ftell(fPtr);

        if (aisFileSize == 0)
        {
            fclose (fPtr);
            return E_FAIL;
        }

        numPagesAIS = 0;
        while ( (numPagesAIS * hNandInfo->dataBytesPerPage)  < aisFileSize ) numPagesAIS++;

        //We want to allocate an even number of pages.
        aisAllocSize = numPagesAIS * hNandInfo->dataBytesPerPage;

        // Setup pointer in RAM
        aisPtr = (Uint8 *) UTIL_allocMem(aisAllocSize);

        // Clear memory
        for (i=0; i<aisAllocSize; i++) aisPtr[i]=0xFF;

        // Go to start of file
        fseek(fPtr,0,SEEK_SET);

        // Read file data
        if (aisFileSize != fread(aisPtr, 1, aisFileSize, fPtr)) DEBUG_printString("\tWARNING: File Size mismatch.\r\n");

        // Close file
        fclose (fPtr);

        // Write the file data to the NAND flash
        if (LOCAL_writeData(hNandInfo, aisPtr, numPagesAIS) != E_PASS)
        {
            printf("\tERROR: Write failed.\r\n");
            return E_FAIL;
        }
    }

    return E_PASS;
}

// Generic function to write a UBL or Application header and the associated data
static Uint32 LOCAL_writeData(NAND_InfoHandle hNandInfo, Uint8 *srcBuf, Uint32 totalPageCnt)
{
    Uint32    i,numBlks,pageNum,pageCnt, blockNum = APP_START_BLK;
    Uint8     *dataPtr;

    gNandTx = (Uint8 *) UTIL_allocMem(NAND_MAX_PAGE_SIZE);
    gNandRx = (Uint8 *) UTIL_allocMem(NAND_MAX_PAGE_SIZE);

    for (i=0; i<NAND_MAX_PAGE_SIZE; i++)  
    {
        gNandTx[i]=0xff;
        gNandRx[i]=0xff;
    }  
  
    // Get total number of blocks needed
    numBlks = 0;
    while ( (numBlks*hNandInfo->pagesPerBlock)  < totalPageCnt ) numBlks++;
    DEBUG_printString("Number of blocks needed for data: ");
    DEBUG_printHexInt(numBlks);
    DEBUG_printString("\r\n");

    // Unprotect all blocks of the device
    if (NAND_unProtectBlocks(hNandInfo, blockNum, (hNandInfo->numBlocks-1)) != E_PASS)
    {
        blockNum++;
        DEBUG_printString("Unprotect failed.\r\n");
        return E_FAIL;
    }

  while (blockNum < hNandInfo->numBlocks)
  {
    // Find first good block
    while (NAND_badBlockCheck(hNandInfo,blockNum) != E_PASS) blockNum++;

    // Erase the current block
    NAND_eraseBlocks(hNandInfo,blockNum,1);

    // Start writing in page 0 of current block
    pageNum = 0;
    pageCnt = 0;

    // Setup data pointer
    dataPtr = srcBuf;

    // Start page writing loop
    do
    {
      DEBUG_printString((String)"Writing image data to block ");
      DEBUG_printHexInt(blockNum);
      DEBUG_printString((String)", page ");
      DEBUG_printHexInt(pageNum);
      DEBUG_printString((String)"\r\n");

      // Write the AIS image data to the NAND device
      if (NAND_writePage(hNandInfo, blockNum,  pageNum, dataPtr) != E_PASS)
      {
        DEBUG_printString("Write failed. Marking block as bad...\n");
        NAND_reset(hNandInfo);
        NAND_badBlockMark(hNandInfo,blockNum);
        dataPtr -=  pageNum * hNandInfo->dataBytesPerPage;
        blockNum++;
        continue;
      }
    
      UTIL_waitLoop(400);
    
      // Verify the page just written
      if (NAND_verifyPage(hNandInfo, blockNum, pageNum, dataPtr, gNandRx) != E_PASS)
      {
//        DEBUG_printString("Verify failed. Marking block as bad...\n");
//        NAND_reset(hNandInfo);
//        NAND_badBlockMark(hNandInfo,blockNum);
//        dataPtr -=  pageNum * hNandInfo->dataBytesPerPage;
//        pageCnt -= pageNum;
//        pageNum = 0;
//        blockNum++;
//        continue;
      }
    
      pageNum++;
      pageCnt++;
      dataPtr +=  hNandInfo->dataBytesPerPage;
  
      if (pageNum == hNandInfo->pagesPerBlock)
      {
        // A block transition needs to take place; go to next good block
        do
        {
          blockNum++;
        }
        while (NAND_badBlockCheck(hNandInfo,blockNum) != E_PASS);

        // Erase the current block
        NAND_eraseBlocks(hNandInfo,blockNum,1);

        pageNum = 0;
      }
    } while (pageCnt < totalPageCnt);

    NAND_protectBlocks(hNandInfo);
    break;
  }
  return E_PASS;
}

static Uint8 DEVICE_ASYNC_MEM_IsNandReadyPin(ASYNC_MEM_InfoHandle hAsyncMemInfo)
{
  return ((Uint8) ((AEMIF->NANDFSR & DEVICE_EMIF_NANDFSR_READY_MASK)>>DEVICE_EMIF_NANDFSR_READY_SHIFT));
}

/***********************************************************
* End file                                                 
************************************************************/


