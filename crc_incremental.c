#include <stdio.h>
#include <string.h>

typedef unsigned char U8;
typedef unsigned int U32;

/* Number of bits in a byte. */
#define NUM_BITS_PER_BYTE   8

/* Number of distinct values in a byte. */
#define NUM_BYTE_VALUES     256

/* Mask to isolate the bits in a byte value. */
#define BYTE_MASK           (NUM_BYTE_VALUES - 1)

/*
    "normal" poly:      0x04C11DB7
    "reversed" poly:    0xEDB88320
*/

#define CRC_BIT_WIDTH   32
#define CRC_BYTE_WIDTH   ((CRC_BIT_WIDTH + NUM_BITS_PER_BYTE - 1) /   \
    NUM_BITS_PER_BYTE)
#define CRC_POLY    0xEDB88320

#define COUNT_BIT_WIDTH 16

#if COUNT_BIT_WIDTH < 32
#define COUNT_MASK  ((1 <<  COUNT_BIT_WIDTH) - 1)
#else
#define COUNT_MASK  0xffffffff
#endif

/* Indexed by a byte value. */
typedef U32 CrcByteTable[NUM_BYTE_VALUES];

/* Indexed by bit position of a "set" bit. */
typedef U32 CrcBitTable[CRC_BIT_WIDTH];

/* Provides CRC adjustment given current low byte of CRC. */
static CrcByteTable crcDataTable;

/* Compute new CRC given current CRC and incoming data byte. */
#define CALC_CRC_BYTE(crcIn, b) \
        (crcDataTable[((crcIn) ^ (b)) & BYTE_MASK] ^ ((crcIn) >>  \
            NUM_BITS_PER_BYTE))

/* Indexed by the bit position of a "set" bit in a count of zeros. */
static CrcBitTable crcZbitTables[COUNT_BIT_WIDTH];

/* Maps full CRC one byte at a time into new CRC for a known, fixed input. */
typedef CrcByteTable CrcFullMap[CRC_BYTE_WIDTH];

/* 
    Map CRC to next CRC given a run of 1492 zeros (1500 byte packet less
    8 bytes of adjusted header). 
*/
CrcFullMap zRun1492;

U32
calcCrcBuf(U32 crcIn, void *buf, U32 bufSize)
{
    U8 *p;
    U32 crc;

    crc = crcIn;
    p = (U8 *) buf;

    while (bufSize--)
    {
        crc = CALC_CRC_BYTE(crc, *p);
        p++;
    }
    return crc;
}


U32
calcCrcZeros(U32 crcIn, U32 numZeros)
{
    U32 c;
    U32 crcMask;
    U32 z;
    U32 zerosMask;
    U32 crc;
    U32 crcXor;

    if (crcIn == 0)
    {
        return 0;
    }

    crc = crcIn;
    zerosMask = 1;
    for (z = 0; z < COUNT_BIT_WIDTH && numZeros >= zerosMask; z++)
    {
        if (numZeros & zerosMask)
        {
            crcXor = 0;
            crcMask = 1;
            for (c = 0; c < CRC_BIT_WIDTH; c++)
            {
                if (crc & crcMask)
                {
                    crcXor ^= crcZbitTables[z][c];
                }
                crcMask <<= 1;
            }
            crc = crcXor;
        }
        zerosMask <<= 1;
    }
    return crc;
}


U32
calcCrcFullMap(U32 crcIn, CrcFullMap map)
{
    U32 crc;
    U32 b;

    crc = 0;
    for (b = 0; b < CRC_BYTE_WIDTH; b++)
    {
        crc ^= map[b][(U8) crcIn];
        crcIn >>= NUM_BITS_PER_BYTE;
    }
    return crc;
}


void
setupFullZeroMap(CrcFullMap map, U32 numZeros)
{
    U32 crc;
    U32 i;
    U32 b;

    for (i = 0; i < NUM_BYTE_VALUES; i++)
    {
        for (b = 0; b < CRC_BYTE_WIDTH; b++)
        {
            crc = i << (b * NUM_BITS_PER_BYTE);
            map[b][i] = calcCrcZeros(crc, numZeros);
        }
    }
}


void
setup(void)
{
    U32 crc;
    U32 i;
    U32 c;
    U32 z;

    /* Setup data maps for byte-at-a-time CRC calculation over a buffer .*/
    for (i = 0; i < NUM_BYTE_VALUES; i++)
    {
        int bit;
        crc = i;
        for (bit = 0; bit < NUM_BITS_PER_BYTE; bit++)
        {
            if (crc & 1)
            {
                crc = (crc >> 1) ^ CRC_POLY;
            }
            else
            {
                crc >>= 1;
            }
        }
        crcDataTable[i] = crc;
    }

    /* Setup crcZbitTables for handling runs of zeros. */
    for (c = 0; c < CRC_BIT_WIDTH; c++)
    {
        crc = (1 << c);
        U32 numZeros = 0;

        for (z = 0; z < COUNT_BIT_WIDTH; z++)
        {
            U32 maxZeros = (1 << z);

            while (numZeros < maxZeros)
            {
                crc = CALC_CRC_BYTE(crc, 0x00);
                numZeros++;
            }
            crcZbitTables[z][c] = crc;
        }
    }

    /* 
        Setup zRun1492 for mapping current CRC into final CRC given a run
        of 1492 zeros.
    */
    setupFullZeroMap(zRun1492, 1492);
}


void
printIncrCrcTables(void)
{
    U32 c;
    U32 z;
    for (z = 0; z < COUNT_BIT_WIDTH; z++)
    {
        for (c = 0; c < CRC_BIT_WIDTH; c++)
        {
            printf("crcZbitTables[%2u][%2u] = 0x%08X\n",
                z, c, crcZbitTables[z][c]);
        }
    }
}


void
testCalcCrcZeros(void)
{
    U32 i;
    U32 c;
    U32 c0;
    U32 crc;
    U32 z;
    U32 numZeros;

    printf("testCalcCrcZeros\n");
    c0 = 1;
    for (c = 0; c < CRC_BIT_WIDTH; c++)
    {
        numZeros = 1;
        for (z = 0; z < COUNT_BIT_WIDTH; z++)
        {
            crc = c0;
            for (i = 0; i < numZeros; i++)
            {
                U32 fastCrc = calcCrcZeros(c0, i);
                if (crc != fastCrc)
                {
                    printf("Error: crc(0x%08X) != fastCrc(0x%08X)\n",
                        crc, fastCrc);
                    return;
                }
                crc = CALC_CRC_BYTE(crc, 0x00);
            }
            numZeros <<= 1;
        }
        c0 <<= 1;
    }
}


void
testCalcCrcFullMap(void)
{
    U32 c;
    U32 c0;
    U32 crc;

    printf("testCalcCrcFullMap\n");
    c0 = 1;
    for (c = 0; c < CRC_BIT_WIDTH; c++)
    {
        U32 fullMapCrc;
        crc = calcCrcZeros(c0, 1492);
        fullMapCrc = calcCrcFullMap(c0, zRun1492);

        if (crc != fullMapCrc)
        {
            printf("Error: crc(0x%08X) != fullMapCrc(0x%08X)\n",
                crc, fullMapCrc);
            return;
        }
        c0 <<= 1;
    }
}


void
testIncrFile(char *testPath)
{
    FILE *fp;
    U32 bufSize;
    U32 dataSize;
#define HEADER_SIZE   8
#define MAX_BUF_SIZE  1500
    U8 oldBuf[MAX_BUF_SIZE];
    U8 newBuf[MAX_BUF_SIZE];

    U32 numTried = 0;
    U32 minSize = 0;
    U32 maxSize = 0;
    U32 numFullMapped = 0;

    U32 c0 = 0x12345678;
    U32 oldCrc;
    U32 newCrc;
    U32 oldHeaderCrc;
    U32 newHeaderCrc;
    U32 xorCrc;
    U32 fastNewCrc;

    printf("testIncrFile(%s)\n", testPath);
    fp = fopen(testPath, "rb");
    if (!fp)
    {
        printf("Failed to open %s\n", testPath);
        return;
    }

    for (;;)
    {
        if (fread(&bufSize, sizeof(bufSize), 1, fp) != 1)
        {
            break;
        }

        if (bufSize > MAX_BUF_SIZE)
        {
            bufSize = MAX_BUF_SIZE;
        }
        else if (bufSize < HEADER_SIZE)
        {
            bufSize = HEADER_SIZE;
        }
        if (fread(oldBuf, bufSize, 1, fp) != 1)
        {
            break;
        }

        if (numTried == 0)
        {
            minSize = maxSize = bufSize;
        }
        else
        {
            if (bufSize < minSize)
            {
                minSize = bufSize;
            }
            if (bufSize > maxSize)
            {
                maxSize = bufSize;
            }
        }
        numTried++;
        memcpy(newBuf, oldBuf, bufSize);

        newBuf[1] ^= 0x55;
        newBuf[2] += 0x73;
        newBuf[3] += 0x1d;
        newBuf[4] += newBuf[0];

        oldCrc = calcCrcBuf(c0, oldBuf, bufSize);
        newCrc = calcCrcBuf(c0, newBuf, bufSize);

        dataSize = bufSize - HEADER_SIZE;
        oldHeaderCrc = calcCrcBuf(c0, oldBuf, HEADER_SIZE);
        newHeaderCrc = calcCrcBuf(c0, newBuf, HEADER_SIZE);
        xorCrc = calcCrcZeros(oldHeaderCrc ^ newHeaderCrc, dataSize);
        fastNewCrc = oldCrc ^ xorCrc;

        if (fastNewCrc != newCrc)
        {
            printf("calcCrcZeros: oldCrc=0x%08X newCrc=0x%08X "
                "(oldCrc ^ newCrc)=0x%08X xorCrc=0x%08X\n",
                oldCrc, newCrc, (oldCrc ^ newCrc), xorCrc);
        }
        if (dataSize == 1492)
        {
            numFullMapped++;
            xorCrc = calcCrcFullMap(oldHeaderCrc ^ newHeaderCrc, zRun1492);
            fastNewCrc = oldCrc ^ xorCrc;

            if (fastNewCrc != newCrc)
            {
                printf("calcCrcFullMap: oldCrc=0x%08X newCrc=0x%08X "
                    "(oldCrc ^ newCrc)=0x%08X xorCrc=0x%08X\n",
                    oldCrc, newCrc, (oldCrc ^ newCrc), xorCrc);
            }
        }
    }
    printf("Tested %u buffers, minSize = %u, maxSize = %u, fullMapped = %u\n",
        numTried, minSize, maxSize, numFullMapped);
    fclose(fp);
}


void
printIntro(void)
{
    FILE *fp;

    printf("crc_incremental [DATAFILE]\n\n");
    fp = fopen("README.txt", "r");
    if (fp)
    {
        int ch;
        while ((ch = fgetc(fp)) >= 0)
        {
            printf("%c", ch);
        }
        fclose(fp);
    }
    else
    {
        printf("Missing README.txt; skipping introduction.\n");
    }
}


int
main(int argc, char *argv[])
{
    printIntro();
    setup();

    if (argc == 2)
    {
        testIncrFile(argv[1]);
    }
    else
    {
        testCalcCrcZeros();
        testCalcCrcFullMap();
    }

    printf("Done.\n");
    return 0;
}
