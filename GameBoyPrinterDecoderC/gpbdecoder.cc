#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <getopt.h>
#include <sys/types.h>

#include <stdlib.h>

#include "gameboy_printer_protocol.h"
#include "gbp_pkt.h"
#include "gbp_tiles.h"
#include "gbp_bmp.h"


/* The official name of this program (e.g., no 'g' prefix).  */
#define PROGRAM_NAME "gpbdecoder"

//static bool verbose_flag = false;

// Input/Output file
const char * ifilename = NULL;
const char * ofilename = NULL;
FILE * ifilePtr = NULL;
char ofilenameBuf[255] = {0};
char ofilenameExt[50]  = {0};

// Pallet
const char * palletParameter = NULL;
uint32_t palletColor[4] = {0};



void gbpdecoder_gotByte(const uint8_t byte);


uint8_t pktCounter = 0; // Dev Varible
//////
gbp_pkt_t gbp_pktBuff = {GBP_REC_NONE, 0};
uint8_t gbp_pktbuff[GBP_PKT_PAYLOAD_BUFF_SIZE_IN_BYTE] = {0};
uint8_t gbp_pktbuffSize = 0;
gbp_pkt_tileAcc_t tileBuff = {0};
gbp_tile_t gbp_tiles = {0};
gbp_bmp_t  gbp_bmp = {0};
//////

/*******************************************************************************
 * Test Vectors Variables
*******************************************************************************/
const uint8_t testVector[] = {
  //#include "2020-08-02_GameboyPocketCameraJP.txt" // Single Image
  //#include "2020-08-02_PokemonSpeciallPicachuEdition.txt" // Mult-page Image
  #include "./test/2020-08-10_Pokemon_trading_card_compressiontest.txt" // Compression
};


/*******************************************************************************
 * Utilites
*******************************************************************************/

const char *gbpCommand_toStr(int val)
{
  switch (val)
  {
    case GBP_COMMAND_INIT    : return "INIT";
    case GBP_COMMAND_PRINT   : return "PRNT";
    case GBP_COMMAND_DATA    : return "DATA";
    case GBP_COMMAND_BREAK   : return "BREK";
    case GBP_COMMAND_INQUIRY : return "INQY";
    default: return "?";
  }
}

static void filenameExtractPathAndExtention(const char *fname,
                        char *pathBuff, int pathSize,
                        char *extBuff, int extSize)
{
    // Minimal Filename Extraction Of Path And Extention
    // Brian Khuu 2021
    int i = 0;
    int end = 0;
    int exti = 0;
    for (end = 0; fname[end] != '\0' ; end++)
    {
        if ((fname[end] == '/')||(fname[end] == '\\'))
          exti = 0;
        else if (fname[end] == '.')
          exti = end;
    }
    if (exti == 0)
        exti = end;

    // Copy PathName
    if (pathBuff)
    {
      for (i = 0; i < (pathSize-1) ; i++)
      {
          if (!(i < exti))
              break;
          pathBuff[i] = fname[i];
      }
      pathBuff[i] = '\0';
    }

    // Copy Extention
    if (extBuff)
    {
      for (i = 0; i < (extSize-1) ; i++)
      {
          if (!(i < (end-exti-1)))
            break;
          extBuff[i] = fname[exti + i + 1];
      }
      extBuff[i] = '\0';
    }
}

int palletColorParse(uint32_t *palletColor, const int palletColorSize, const char * parameterStr)
{
  // Parse web color hexes for pallet (e.g. `0xFFAD63, ...`) (e.g. `#FFAD63, ...`)
  // This was created for the cdecoder in https://github.com/mofosyne/arduino-gameboy-printer-emulator
  // https://gist.github.com/mofosyne/b1fc240b64c520c0bf3541a029e3dcc3
  // Brian Khuu 2021
  if (!parameterStr)
    return 0;
  int palletCounter = 0;
  int nibIndex = 0;
  uint32_t pallet = 0;
  char prevChar = 0;
  for ( ; (*(parameterStr)) != '\0' ; parameterStr++)
  {
    const char ch = *parameterStr;
    // Search for start of #XXXXXX, we are looking for 4 pallets
    if (nibIndex == 0)
    {
      if ((ch == '#') || ((prevChar == '0')&&(ch == 'x')))
      {
        pallet = 0;
        nibIndex = 3*2; // [R, G, B]
        prevChar = 0;
        continue;
      }
      prevChar = ch;
      continue;
    }
    nibIndex--;
    // Parse Nibble
    char nib = -1;
    if (('0' <= ch) && (ch <= '9'))
      nib = ch - '0';
    else if (('a' <= ch) && (ch <= 'f'))
      nib = ch - 'a' + 10;
    else if (('A' <= ch) && (ch <= 'F'))
      nib = ch - 'A' + 10;
    else
    {
      nibIndex = 0;
      palletColor[palletCounter] = pallet;
      palletCounter++;
      if (palletCounter >= palletColorSize)
        break;
      continue;
    }
    // Pallet
    pallet |= nib << (nibIndex*4);
    if (nibIndex == 0)
    {
      palletColor[palletCounter] = pallet;
      palletCounter++;
      if (palletCounter >= palletColorSize)
        break;
    }
  }
  return palletCounter;
}

/*******************************************************************************
 * Main Test Routine
*******************************************************************************/
int
main (int argc, char **argv)
{
  (void) argc;
  (void) argv;

  int c;
  static struct option const long_options[] =
  {
    /* These options set a flag. */
    //{"verbose", no_argument, &verbose_flag, 1},
    //{"brief",   no_argument, &verbose_flag, 0},
    /* These options don’t set a flag.
        We distinguish them by their indices. */
    {"input", required_argument, NULL, 'i'},
    {"output", required_argument, NULL, 'o'},
    {"verbose", no_argument, NULL, 'v'},
    {NULL, 0, NULL, 0}
  };

  while ((c = getopt_long (argc, argv, "o:i:p:", long_options, NULL))
         != -1)
  {
    switch (c)
    {
        case 'i':
          ifilename = optarg;
          break;

        case 'o':
          ofilename = optarg;
          break;

        case 'p':
          palletParameter = optarg;
          break;
    }
  }

  /* Input File */
  if (ifilename)
  {
    ifilePtr = fopen(ifilename, "r+");
    if (ifilePtr == NULL)
    {
      printf ("file not found\n");
      return 0;
    }
    printf ("file input `%s' open\n", ifilename);
  }
  else
  {
    // Input file not found, use stdin
    ifilePtr = stdin;
    printf ("file input stdin\n");
  }

  /* Ouput File */
  if (!ofilename)
  {
    // Default output filename if not defined
    if (ifilename)
    {
      // Use input filename (We will strip out any extention and add our own anyway)
      ofilename = ifilename;
    }
    else
    {
      ofilename = "gbpOut.bmp";
    }
  }
  filenameExtractPathAndExtention(ofilename, ofilenameBuf, sizeof(ofilenameBuf), ofilenameExt, sizeof(ofilenameExt));
  printf("file requested output `%s' (%s)\n", ofilenameBuf, ofilenameExt);

  /* Custom Pallet */
  if (palletColorParse(palletColor, sizeof(palletColor)/sizeof(palletColor[0]), palletParameter) == 0)
  {
    palletColor[0] = 0xFFFFFF;
    palletColor[1] = 0xAAAAAA;
    palletColor[2] = 0x555555;
    palletColor[3] = 0x000000;
  }

  printf("0x%06X, 0x%06X, 0x%06X, 0x%06X | %s\n", palletColor[0], palletColor[1], palletColor[2], palletColor[3], palletParameter);

  /////////////////////////////////////////////////////////
  gbp_pkt_init(&gbp_pktBuff);

  char ch = 0;
  bool skipLine = false;
  int  lowNibFound = 0;
  uint8_t byte = 0;
  unsigned int bytec = 0;
  while ((ch = fgetc(ifilePtr)) != EOF)
  {
    // Skip Comments
    if (ch == '/')
    {
      // Might be `//` or `/*`
      skipLine = true;
      continue;
    }
    else if (skipLine)
    {
      // Discarding line
      if (ch == '\n')
        skipLine = false;
      continue;
    }

    // Parse Nibble
    char nib = -1;
    if (('0' <= ch) && (ch <= '9'))
      nib = ch - '0';
    else if (('a' <= ch) && (ch <= 'f'))
      nib = ch - 'a' + 10;
    else if (('A' <= ch) && (ch <= 'F'))
      nib = ch - 'A' + 10;

    /* Parse As Byte */
    bool byteFound = false;
    // Hex Parse Edge Cases
    if (lowNibFound)
    {
      // '0x' found. Ignore
      if ((byte == 0) && (ch == 'x'))
        lowNibFound = false;
      // Not a hex digit pair. Ignore
      if (nib == -1)
        lowNibFound = false;
    }
    // Hex Byte Parsing
    if (nib != -1)
    {
      if (!lowNibFound)
      {
        lowNibFound = true;
        byte = nib << 4;
      }
      else
      {
        lowNibFound = false;
        byte |= nib << 0;
        byteFound = true;
      }
    }

    // Byte Was Found, decoding...
    if (byteFound)
    {
      bytec++;
      gbpdecoder_gotByte(byte);
    }
  }

#if 0
  gbp_bmp_rendertest(&gbp_bmp,
    (uint16_t)(GBP_TILE_PIXEL_WIDTH * GBP_TILES_PER_LINE),
    (uint16_t)(GBP_TILE_PIXEL_HEIGHT*gbp_tiles.tileRowOffset));
#endif

  return 0;
}


void gbpdecoder_gotByte(const uint8_t byte)
{
  if (gbp_pkt_processByte(&gbp_pktBuff, byte, gbp_pktbuff, &gbp_pktbuffSize, sizeof(gbp_pktbuff)))
  {
    if (gbp_pktBuff.received == GBP_REC_GOT_PACKET)
    {
      pktCounter++;
#if 1
      printf("// %s | compression: %1u, dlength: %3u, printerID: 0x%02X, status: %u | %d | ",
          gbpCommand_toStr(gbp_pktBuff.command),
          (unsigned) gbp_pktBuff.compression,
          (unsigned) gbp_pktBuff.dataLength,
          (unsigned) gbp_pktBuff.printerID,
          (unsigned) gbp_pktBuff.status,
          (unsigned) pktCounter
        );
      for (int i = 0 ; i < gbp_pktbuffSize ; i++)
      {
        printf("%02X ", gbp_pktbuff[i]);
      }
      printf("\r\n");
#endif
      if (gbp_pktBuff.command == GBP_COMMAND_PRINT)
      {
        gbp_tiles_print(&gbp_tiles,
            gbp_pktbuff[GBP_PRINT_INSTRUCT_INDEX_NUM_OF_SHEETS],
            gbp_pktbuff[GBP_PRINT_INSTRUCT_INDEX_NUM_OF_LINEFEED],
            gbp_pktbuff[GBP_PRINT_INSTRUCT_INDEX_PALETTE_VALUE],
            gbp_pktbuff[GBP_PRINT_INSTRUCT_INDEX_PRINT_DENSITY]);

        if ((gbp_pktbuff[GBP_PRINT_INSTRUCT_INDEX_NUM_OF_LINEFEED]&0xF) != 0)
        {
          // if lower margin is zero, then new pic
          gbp_bmp_render(&gbp_bmp,
            (char *)    ofilenameBuf,
            (uint8_t *) &gbp_tiles.bmpLineBuffer,
            (uint16_t)  (GBP_TILE_PIXEL_WIDTH * GBP_TILES_PER_LINE),
            (uint16_t)  (GBP_TILE_PIXEL_HEIGHT*gbp_tiles.tileRowOffset),
            (uint32_t*) palletColor);
          gbp_tiles_reset(&gbp_tiles);
        }

#if 0   // per Print Buffer Decoded (Post-Pallet-Harmonisation)
        for (int j = 0; j < (GBP_TILE_PIXEL_HEIGHT * gbp_tiles.tileRowOffset); j++)
        {
          for (int i = 0; i < (GBP_TILE_PIXEL_WIDTH * GBP_TILES_PER_LINE); i++)
          {
            const int pixel = gbp_tiles.bmpLineBuffer[j][i];
            int b = 0;
            switch (pixel)
            {
              case 3: b = 0; break;
              case 2: b = 64; break;
              case 1: b = 130; break;
              case 0: b = 255; break;
            }
            printf("\x1B[48;2;%d;%d;%dm \x1B[0m", b, b, b);
          }
          printf("\r\n");
        }
#endif
      }
    }
    else
    {
      // Support compression payload
      while (gbp_pkt_decompressor(&gbp_pktBuff, gbp_pktbuff, gbp_pktbuffSize, &tileBuff))
      {
        if (gbp_pkt_tileAccu_tileReadyCheck(&tileBuff))
        {
          // Got tile
#if 0     // Output Tile As Hex For Debugging purpose
          for (int i = 0 ; i < GBP_TILE_SIZE_IN_BYTE ; i++)
          {
            printf("%02X ", tileBuff.tile[i]);
          }
          printf("\r\n");
#endif
          if (gbp_tiles_line_decoder(&gbp_tiles, tileBuff.tile))
          {
            // Line Obtained
#if 0       // Per Line Decoded (Pre Pallet Harmonisation)
            for (int j = 0; j < GBP_TILE_PIXEL_HEIGHT; j++)
            {
              for (int i = 0; i < (GBP_TILE_PIXEL_WIDTH * GBP_TILES_PER_LINE); i++)
              {
                int pixel = gbp_tiles.bmpLineBuffer[j+(gbp_tiles.tileRowOffset-1)*8][i];
                int b = 0;
                switch (pixel)
                {
                  case 0: b = 0; break;
                  case 1: b = 64; break;
                  case 2: b = 130; break;
                  case 3: b = 255; break;
                }
                printf("\x1B[48;2;%d;%d;%dm \x1B[0m", b, b, b);
              }
              printf("\r\n");
            }
#endif
          }
        }
      }
    }
  }
}
