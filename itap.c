/******************************************************************************
 iTAP by @Shark (c)20/01/2026
 
 $ gcc itap.c -o itap -w (Ubuntu)

 Based on STAP - Split TAPes
 Author: TSM
 Adapted for Windows (and Linux too) by iAN CooG
 Tested with:
   MSVC 7.1 (WIN32)
   GCC 3.4.4 (MinGW/Msys, WIN32)
   GCC 3.2 (CygWin, *NIX emulation)
   GCC 4.0 (Linux)

 Started 19/05/06 - Last update 28/01/08
 
 This program splits Commodore TAP files into individual programs.
 It detects pilot tones to identify program boundaries and creates
 separate TAP files for each program with corrected headers.
******************************************************************************/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32

#include <conio.h>
#define CR 13

#else

#include <termios.h>
#include <unistd.h>
#include <limits.h>
#define CR 10
#define _MAX_PATH PATH_MAX
#define getch(x) nixgetch(x)

#endif

#define PROGVERSION "1.01"

// Global variables
char tapname[_MAX_PATH];        // Input TAP filename
char batchmode=0;               // Batch mode flag (no user interaction)
char listonly=0;                // List mode flag (only list blocks, don't split)
char addnames=0;                // Add program names to output files flag
char verbose=0;                 // Verbosity level (0-2)
unsigned char blocknames[100][20]; // Array to store program names
unsigned char tap_version;      // TAP file version (0, 1, or 2)

/*------------------------------------------------------------------------*/
#ifndef _WIN32
/**
 * nixgetch() - Get character from keyboard without echo (Unix/Linux version)
 * 
 * Returns: Character pressed
 */
int nixgetch( )
{
    struct termios oldt,newt;
    int ch;

    tcgetattr( STDIN_FILENO, &oldt );
    newt = oldt;
    newt.c_lflag &= ~( ICANON | ECHO );
    tcsetattr( STDIN_FILENO, TCSANOW, &newt );
    ch = getchar();
    tcsetattr( STDIN_FILENO, TCSANOW, &oldt );

    return ch;
}
#endif

/*------------------------------------------------------------------------*/
/**
 * ispilot() - Check if byte value is within pilot tone range
 * @byte: Pulse length value to check
 * 
 * Pilot tones are sequences of pulses with values between 40-60.
 * They indicate the start of a program on the tape.
 * 
 * Returns: 1 if pilot tone, 0 otherwise
 */
int ispilot(unsigned char byte)
{
    if (( byte>40 ) && ( byte<60 ))
    {
        return 1;
    }
    return 0;
}

/*------------------------------------------------------------------------*/
/**
 * islong() - Check if pulse is a long pulse
 * @pulse: Pulse length value
 * 
 * Long pulses are between 0x4a-0x64 (74-100 decimal)
 * 
 * Returns: 0x56 if long pulse, 0 otherwise
 */
int islong(unsigned char pulse)
{
    if ( (pulse>=0x4a)&&(pulse<=0x64) ) // clean with 0x56
    {
        return 0x56;
    }
    return 0;
}

/*------------------------------------------------------------------------*/
/**
 * ismedium() - Check if pulse is a medium pulse
 * @pulse: Pulse length value
 * 
 * Medium pulses are between 0x37-0x49 (55-73 decimal)
 * 
 * Returns: 0x42 if medium pulse, 0 otherwise
 */
int ismedium(unsigned char pulse)
{
    if ( (pulse>=0x37)&&(pulse<=0x49) ) // clean with 0x42
    {
        return 0x42;
    }
    return 0;
}

/*------------------------------------------------------------------------*/
/**
 * isshort() - Check if pulse is a short pulse
 * @pulse: Pulse length value
 * 
 * Short pulses are between 0x24-0x36 (36-54 decimal)
 * 
 * Returns: 0x30 if short pulse, 0 otherwise
 */
int isshort(unsigned char pulse)
{
    if ( (pulse>=0x24)&&(pulse<=0x36) ) // clean with 0x30
    {
        return 0x30;
    }
    return 0;
}

/*------------------------------------------------------------------------*/
/**
 * obtain_number() - Interactive menu to select block number
 * @max: Maximum block number
 * 
 * Allows user to navigate with + and - keys and confirm with Enter
 * 
 * Returns: Selected block number, or max+2 if ESC pressed
 */
unsigned char obtain_number(unsigned int max)
{
    unsigned char current=1,key=0;

    printf("\nChoose with <+> and <->, confirm with <Enter>\n");
    while (key!=CR)
    {
        printf("\rChoice: %02d - %-16s",current,blocknames[current-1]);
        key=(unsigned char)getch();
        if ( key == 0x1b )  // ESC key
        {
            return max+2;
        }
        if ( (key=='+')&&(current<(max-1)) )
        {
            current++;
        }
        if ( (key=='-')&&(current>1) )
        {
            current--;
        }
    }
    return current;
}

/*------------------------------------------------------------------------*/
/**
 * get_pulse() - Read one pulse length from TAP file
 * @file_inp: Input file pointer
 * 
 * TAP files encode pulses as:
 * - Values 1-255: Direct pulse length
 * - Value 0: Extended pulse (next 3 bytes contain length in little-endian)
 * 
 * Returns: Pulse length, or EOF on end of file
 */
int get_pulse(FILE *file_inp)
{
    unsigned char data;
    int pulse_length = 0,pos=0;
    unsigned char size[3];
    size_t res;

    res = fread(&data, 1, 1, file_inp);

    if (res == 0)
    {
        return EOF;
    }

    if (data == 0)  // Extended pulse format
    {
        if (tap_version == 0)
        {
            pulse_length = 0x100;
        }
        else
        {
            // Read 3-byte little-endian pulse length
            res = fread(size, 3, 1, file_inp);
            if (res == 0)
            {
                return EOF;
            }
            // Convert to pulse length (divided by 8)
            pulse_length = ((size[2] << 16) | (size[1] << 8) | size[0]) >> 3;
        }
        if(verbose>1)
        {
            if (pulse_length>0xff)
            {
                pos=ftell(file_inp);
                if(pulse_length==0x100)
                {
                    pos--;
                }
                else
                {
                    pos-=4;
                }
                printf("HIGHPULSE @ 0x%08x=0x%08x\n",(unsigned int)pos,pulse_length);
            }
        }
    }
    else
    {
        pulse_length = data;
    }

    return (int)pulse_length;
}

/*------------------------------------------------------------------------*/
/**
 * readbyte() - Read one data byte from TAP file
 * @file_inp: Input file pointer
 * @start: Start position for reading
 * @end: End position for reading
 * 
 * Reads a byte by decoding pulse sequences:
 * 1. Find sync pattern (long pulse followed by medium pulse)
 * 2. Read 8 bits (each bit is 2 pulses)
 * 3. Short-medium/long = bit 0, Medium/long-short = bit 1
 * 
 * Returns: Decoded byte value
 */
unsigned char readbyte(FILE *file_inp,
                       unsigned int start,
                       unsigned int end)
{
    int syncfound=0,cgot;
    unsigned int i=0,counter=0,times;
    unsigned char impulse,impulseprev;
    unsigned char byte=0;
    unsigned char bit=0;

    // Find sync pattern
    i=start;
    if( (cgot=get_pulse(file_inp))==EOF)
    {
        return 0;
    }

    impulseprev=(unsigned char)cgot;
    while ( !syncfound )
    {
        if( (cgot=get_pulse(file_inp))==EOF)
        {
            break;
        }
        impulse=(unsigned char)cgot;
        // Sync is: long pulse followed by medium pulse
        if ( islong( impulseprev ) && ismedium( impulse ) )
        {
            syncfound=1;
        }
        else
        {
            impulseprev=impulse;
        }
        i++;
    }

    if(!(syncfound))
    {
        if(verbose)
        {
            printf(" !!! SYNC NOT FOUND !!! \n");
        }
        return 0;
    }
    
    // Read 8 bits
    i=0;
    times=0;
    for (times=0;(times<8)&&(i<end);times++)
    {
        if( (cgot=get_pulse(file_inp))==EOF)
        {
            break;
        }
        impulseprev=(unsigned char)cgot;
        if( (cgot=get_pulse(file_inp))==EOF)
        {
            break;
        }
        impulse=(unsigned char)cgot;

        // Decode bit: short then medium/long = bit 0
        if ( isshort( impulseprev ) &&  (ismedium( impulse ) || islong( impulse )) )
        {
            bit=0;
        }
        // Decode bit: medium/long then short = bit 1
        if (( ismedium( impulseprev ) || islong( impulseprev )) &&  isshort( impulse ) )
        {
            bit=128;
            counter++;
        }
        byte=byte>>1;
        byte=byte|bit;
        i+=2;
    }

    // Read trailing pulses (parity/stop bits)
    if( (cgot=get_pulse(file_inp))==EOF)
    {
        return 0;
    }
    impulseprev=(unsigned char)cgot;
    if( (cgot=get_pulse(file_inp))==EOF)
    {
        return 0;
    }
    impulse=(unsigned char)cgot;

    return byte;
}

/*------------------------------------------------------------------------*/
/**
 * fixendtape() - Fix tape ending by removing trailing short pulses
 * @b: Buffer containing tape data
 * @len: Pointer to data length (will be updated)
 * 
 * Searches backwards from end of data to find the last non-short pulse
 * followed by a zero byte, and truncates the data there.
 */
void fixendtape(unsigned char *b, int *len)
{
    int i,l=*len,e=l-0x4000;
    i=l-4;
    if(b[i]!=0)
    {
        for( ;i>e;i--)
        {
            if(isshort(b[i]))
            {
               continue;
            }
            if(b[i]==0)
            {
                *len=i+4;
                break;
            }
        }
    }
    return;
}

/*------------------------------------------------------------------------*/
/**
 * isHdr() - Check if byte is a header marker
 * @byte: Byte value to check
 * 
 * In Commodore tape format, 0x89 indicates a header block
 * 
 * Returns: 1 if header marker, 0 otherwise
 */
int isHdr( unsigned char byte )
{
    if( byte==0x89 )
    {
        return 1;
    }
    return 0;
}

/*------------------------------------------------------------------------*/
/**
 * GetPrgName() - Extract program name from tape data
 * @start: Start position in file
 * @end: End position in file
 * @file_inp: Input file pointer
 * @blockname: Output buffer for program name (20 bytes)
 * 
 * MODIFIED VERSION: Replaces empty/NULL names with "NO-NAME"
 * 
 * Reads tape data to find header (0x89) and extracts the 16-character
 * program name that follows it. Cleans invalid characters, removes
 * trailing spaces, and replaces empty names with "NO-NAME".
 */
void GetPrgName(unsigned int start,
                unsigned int end,
                FILE *file_inp,
                unsigned char *blockname)
{
    unsigned char byte[16]={0};
    unsigned char name[20]={0};
    int i;
    fseek(file_inp, start, SEEK_SET);
    byte[0]=0;

    // Search for header marker (0x89)
    while ( !isHdr(byte[0]) && !feof(file_inp) )
    {
        byte[0]=readbyte(file_inp,start,end);
    }
    if(feof(file_inp))
    {
        if(verbose)
        {
            printf("\n!!! Premature end of file !!!");
        }
        printf("\n");
        return;
    }
    
    // Read 13 bytes of header data
    for(i=1;i<14 && !feof(file_inp);i++)
    {
        byte[i]=readbyte(file_inp,start,end);
    }
    
    // Read 16 bytes of program name
    for(i=0;i<16 && !feof(file_inp);i++)
    {
        name[i]=readbyte(file_inp,start,end);
        // Clean control characters
        if((name[i]>0) && (name[i]<0x20))
        {
            name[i]='_';
        }
        // Clean high-bit characters
        else if((name[i]>=0xa0) && (name[i]<0xff))
        {
            name[i]&=0x7f;
        }
    }
    name[16]=0;
    strcpy(blockname,name);

    // Remove trailing spaces
    for(i=15; (i>=0) && (blockname[i]) && (blockname[i]==0x20) ;i--)
    {
        blockname[i]=0;
    }

    // Replace invalid filename characters
    for(i=0;blockname[i];i++)
    {
        if( strchr("*<>?:|^",blockname[i])!=NULL )
        {
            blockname[i]='_';
        }
        else if(strchr(",\\/",blockname[i])!=NULL )
        {
            blockname[i]='.';
        }
        else if(blockname[i] == '\"')
        {
            blockname[i]='\'';
        }
        else if(blockname[i]>0x7f)
        {
            blockname[i]='_';
        }
    }

    // **NEW: Check if name is empty and replace with "NO-NAME"**
    // This checks if the name is NULL, empty, or contains only spaces/underscores
    int is_empty = 1;
    for(i=0; blockname[i]; i++)
    {
        // If we find any character that's not space, underscore, or null
        if(blockname[i] != 0x20 && blockname[i] != '_' && blockname[i] != 0)
        {
            is_empty = 0;
            break;
        }
    }

    // If name is empty or first character is null, replace with "NO-NAME"
    if(is_empty || blockname[0] == 0)
    {
        strcpy(blockname, "NO-NAME");
    }

    printf("%-16s",blockname);
    if(verbose)
    {
        printf(" type %02X from $%02X%02X to $%02X%02X", byte[9], byte[11],byte[10],byte[13],byte[12]);
    }
    printf("\n");
}

/*------------------------------------------------------------------------*/
/**
 * save() - Save a program block to a new TAP file
 * @start: Start position in original TAP file
 * @end: End position in original TAP file
 * @chr1: Block number (0-based)
 * @nameread: Original TAP filename
 * 
 * **THIS FUNCTION GENERATES THE NEW HEADER FOR EACH SPLIT TAP FILE**
 * 
 * Creates a new TAP file containing:
 * 1. TAP signature (12 bytes): "C64-TAPE-RAW"
 * 2. TAP version (1 byte): Version from original file
 * 3. Reserved bytes (3 bytes): 0x00, 0x00, 0x00
 * 4. Data size (4 bytes): Little-endian size of the extracted data
 * 5. Tape data: The actual program data from start to end
 */
void save( unsigned int start,
           unsigned int end,
           int chr1,
           char *nameread)
{
    char msg[] = "C64-TAPE-RAW";  // TAP file signature
    unsigned int l0,l1,l2,l3;
    FILE *file_out, *file_inp;
    char name[_MAX_PATH+8]={0};
    char *p,*b;
    unsigned int len ;

    // Construct output filename
    strcpy(name,nameread);
    name[_MAX_PATH-1]=0;
    p=strrchr(name,'.');
    if(p)
    {
        *p=0;  // Remove original extension
    }
    
    // Add suffix based on naming mode
    switch(addnames)
    {
    case 1:
        sprintf(name,"%s_%02d_%s",name,chr1+1,blocknames[chr1]);
        break;
    case 2:
        sprintf(name,"%02d_%s",chr1+1,blocknames[chr1]);
        break;
    case 3:
        sprintf(name,"%s",blocknames[chr1]);
        break;
    default:
        sprintf(name,"%s_%02d",name,chr1+1);
        break;
    }
    strcat(name,".tap");
    printf("%s\n",name);

    // Create output file
    file_out=fopen(name,"wb");
    
    // **WRITE NEW TAP HEADER - PART 1: Signature**
    fwrite(msg,1,sizeof(msg)-1,file_out);  // Write "C64-TAPE-RAW" (12 bytes)

    // Read data from original file
    file_inp=fopen(nameread, "rb");
    fseek(file_inp, start, SEEK_SET);
    len=end-start;
    b=malloc(len);
    if(b)
    {
        fread(b,len,1,file_inp);
        
        // Fix tape ending (remove trailing pulses)
        fixendtape(b,&len);
        
        // **PREPARE DATA SIZE FOR HEADER**
        // Calculate little-endian bytes for data size
        l3=(len    )&0xff;  // Byte 0 (LSB)
        l2=(len>> 8)&0xff;  // Byte 1
        l1=(len>>16)&0xff;  // Byte 2
        l0=(len>>24)&0xff;  // Byte 3 (MSB)
        
        // **WRITE NEW TAP HEADER - PART 2: Version and Size**
        putc(tap_version, file_out);  // Byte 12: TAP version (0, 1, or 2)
        putc(0, file_out);             // Byte 13: Reserved
        putc(0, file_out);             // Byte 14: Reserved
        putc(0, file_out);             // Byte 15: Reserved
        putc(l3, file_out);            // Byte 16: Data size LSB
        putc(l2, file_out);            // Byte 17: Data size byte 1
        putc(l1, file_out);            // Byte 18: Data size byte 2
        putc(l0, file_out);            // Byte 19: Data size MSB
        
        // **WRITE TAP DATA**
        fwrite(b,len,1,file_out);
        free(b);
    }
    fclose(file_out);
    fclose(file_inp);
}

/*------------------------------------------------------------------------*/
/**
 * create_cleaned_tap() - Create cleaned TAP file with validated programs
 * @tapname: Original TAP filename
 * @nblocks: Number of valid blocks/programs
 * @array_blocks: Array of block start positions
 * @file_inp: Input file pointer (original TAP)
 * 
 * Creates filename_cleaned.tap with:
 * - 20-byte TAP header (corrected size)
 * - All cleaned program data sequentially
 */
void create_cleaned_tap(char *tapname, 
                       int nblocks, 
                       unsigned int *array_blocks,
                       FILE *file_inp)
{
    FILE *cleaned_file;
    char cleaned_filename[_MAX_PATH];
    char *p;
    int i;
    unsigned char *block_data;
    unsigned int block_len;
    unsigned int total_len = 0;
    unsigned int l0, l1, l2, l3;
    char msg[] = "C64-TAPE-RAW";
    
    // ============================================================
    // STEP A: Build file name
    // ============================================================
    strcpy(cleaned_filename, tapname);
    cleaned_filename[_MAX_PATH-1] = 0;
    
    // Find and remove file extension
    p = strrchr(cleaned_filename, '.');
    if(p)
    {
        *p = 0;  // Remove file extension
    }
    
    // Añadir "_cleaned.tap"
    strcat(cleaned_filename, "_cleaned.tap");
    
    printf("\nCreating cleaned TAP file: %s\n", cleaned_filename);
    
    // ============================================================
    // Step B: Calc cleaned data size
    // ============================================================
    // First round: Calc total size
    for(i = 0; i < nblocks; i++)
    {
        block_len = array_blocks[i+1] - array_blocks[i];
        
        // Buffered
        block_data = malloc(block_len);
        if(!block_data)
        {
            printf("\nError: Cannot allocate memory for block %d\n", i+1);
            return;
        }
        
        // Read data block
        fseek(file_inp, array_blocks[i], SEEK_SET);
        fread(block_data, block_len, 1, file_inp);
        
        // Clean end tap (remove final pulses)
        fixendtape(block_data, &block_len);
        
        // Add total
        total_len += block_len;
        
        free(block_data);
    }
    
    // Statistics
    printf("  Original size: %u bytes\n", array_blocks[nblocks] - 20);
    printf("  Cleaned size:  %u bytes\n", total_len);
    printf("  Reduction:     %u bytes (%.1f%%)\n", 
           (array_blocks[nblocks] - 20) - total_len,
           100.0 * ((array_blocks[nblocks] - 20) - total_len) / (array_blocks[nblocks] - 20));
    printf("\n");
    
    // ============================================================
    // Step C: Create clan tap file
    // ============================================================
    cleaned_file = fopen(cleaned_filename, "wb");
    if(!cleaned_file)
    {
        printf("\nError: Cannot create cleaned file: %s\n", cleaned_filename);
        return;
    }
    
    // ============================================================
    // Step D: Write heaader (20 bytes)
    // ============================================================
    
    // Write sign TAP (12 bytes): "C64-TAPE-RAW"
    fwrite(msg, 1, sizeof(msg)-1, cleaned_file);
    
    // Write TAP version (1 byte)
    putc(tap_version, cleaned_file);
    
    // Write reserved bytes (3 bytes): 0x00, 0x00, 0x00
    putc(0, cleaned_file);
    putc(0, cleaned_file);
    putc(0, cleaned_file);
    
    // Write data size (4 bytes, little-endian)
    l3 = (total_len      ) & 0xff;  // LSB
    l2 = (total_len >>  8) & 0xff;
    l1 = (total_len >> 16) & 0xff;
    l0 = (total_len >> 24) & 0xff;  // MSB
    
    putc(l3, cleaned_file);  // Byte 16
    putc(l2, cleaned_file);  // Byte 17
    putc(l1, cleaned_file);  // Byte 18
    putc(l0, cleaned_file);  // Byte 19
    
    // ============================================================
    // Step E: Write cleaned data
    // ============================================================
    // Second round: write all the cleaned blocks
    for(i = 0; i < nblocks; i++)
    {
        block_len = array_blocks[i+1] - array_blocks[i];
        
        // Buffered
        block_data = malloc(block_len);
        if(!block_data)
        {
            printf("\nError: Cannot allocate memory for block %d\n", i+1);
            fclose(cleaned_file);
            return;
        }
        
        // Write block data (original TAP)
        fseek(file_inp, array_blocks[i], SEEK_SET);
        fread(block_data, block_len, 1, file_inp);
        
        // Clean end block
        fixendtape(block_data, &block_len);
        
        // Write cleaned block
        fwrite(block_data, block_len, 1, cleaned_file);
        
        // Show progress
        printf("  Block %02d (%s): %u bytes\n", 
               i+1, blocknames[i], block_len);
        
        free(block_data);
    }
    
    fclose(cleaned_file);
    
    printf("\nCleaned TAP file created successfully: %s\n", cleaned_filename);
    printf("  %d programs included\n", nblocks);
}

/*------------------------------------------------------------------------*/
/**
 * create_idx_file() - Create index file with program positions and names
 * @tapname: Original TAP filename
 * @nblocks: Number of blocks/programs
 * @array_blocks: Array of block start positions
 * 
 * Creates a .idx file with format:
 * ; Index file generated by Split Tap
 * 0x00000014 TESTATA         
 * 0x0002a7c5 SPACE TRAVEL    
 */
void create_idx_file(char *tapname, int nblocks, unsigned int *array_blocks)
{
    FILE *idx_file;
    char idx_filename[_MAX_PATH];
    char *p;
    int i;
    
    // Construct .idx filename from TAP filename
    strcpy(idx_filename, tapname);
    idx_filename[_MAX_PATH-1] = 0;
    
    // Find and replace extension with .idx
    p = strrchr(idx_filename, '.');
    if(p)
    {
        *p = 0;  // Remove extension
    }
    strcat(idx_filename, ".idx");
    
    // Open .idx file for writing
    idx_file = fopen(idx_filename, "w");
    if(!idx_file)
    {
        printf("\nError: Cannot create index file: %s\n", idx_filename);
        return;
    }
    
    // Write header comment
    fprintf(idx_file, "; Index file generated by Split Tap\n");
    /* fprintf(idx_file, "\n");                                 */
    
    // Write each program entry: position (hex) + name
    for(i = 0; i < nblocks; i++)
    {
        // Format: 0x%08X %-16s\n
        fprintf(idx_file, "0x%08X %-16s\n", 
                array_blocks[i],      // Start position in hex
                blocknames[i]);       // Program name
    }
    
    fclose(idx_file);
    
    // Print confirmation message
    printf("\nIndex file created: %s\n", idx_filename);
    printf("  %d programs indexed\n", nblocks);
}


/*------------------------------------------------------------------------*/
/**
 * filesize() - Get file size
 * @stream: File pointer
 * 
 * Returns: File size in bytes
 */
int filesize(FILE *stream)
{
   int curpos, length;

   curpos = ftell(stream);
   fseek(stream, 0L, SEEK_END);
   length = ftell(stream);
   fseek(stream, curpos, SEEK_SET);
   return length;
}

/*------------------------------------------------------------------------*/
/**
 * PrintBlocks() - Print block information
 * @i: Block index
 * @array_blocks: Array of block start positions
 * @file_inp: Input file pointer
 * 
 * Displays block number, size, and program name
 *
 * MODIFIED VERSION: Now shows hexadecimal start/end positions
 * 
 * Output format:
 * 01)    74565 [0x00000014-0x00012359] - PROGRAM NAME
 * 
 * Where:
 * - 01) = Block number
 * - 74565 = Size in bytes (decimal)
 * - [0x00000014-0x00012359] = Start and end positions in hexadecimal
 * - PROGRAM NAME = Name extracted from tape data
 */
void PrintBlocks( int i,
                  unsigned int *array_blocks,
                  FILE *file_inp)
{

/*    printf("%02d) %8d - ",i+1,array_blocks[i+1]-array_blocks[i]);       */
	// Print block number, size in decimal, and hex positions
    printf("%02d) %8d bytes, 0x%08X to 0x%08X - ",
           i+1,                               // Block number (1-based)
           array_blocks[i+1]-array_blocks[i], // Size in bytes
           array_blocks[i],                   // Start position (hex)
           array_blocks[i+1]-0x01);           // End position (hex)

    // Get and print program name
    GetPrgName( array_blocks[i],
                array_blocks[i+1],
                file_inp,
                blocknames[i] );
    return ;
}

/*------------------------------------------------------------------------*/
/**
 * Usage() - Print usage information
 */
void Usage(void)
{
    printf("\nUsage:\n iTAP <TAP name> [-b] [-l] [-i] [-c] [-n[x]] [-d[x]] [-h[x]] [-k[x]]\n");
    printf(" -b    batch mode, never ask any question\n");
    printf(" -l    list mode, view file list and exit\n");
    printf(" -i    create index file (.idx) with program positions and names\n");
    printf(" -c    create cleaned TAP file (remove small blocks, fix little issues)\n");
    printf(" -n[x] output filenames style. x can be from 0 to 3\n");
    printf("    0: tapname_progressive (default when -n omitted)\n");
    printf("    1: tapname_progressive_filename (equal to -n)\n");
    printf("    2: progressive_filename\n");
    printf("    3: filename\n");
    printf(" -d[x] print debug informations. x is the verboseness, can be from 0 to 2\n");
    printf("    0: no additional info (default when -d omitted)\n");
    printf("    1: info on every header, sync/eof messages (equal to -d)\n");
    printf("    2: debug messages\n");
    printf(" -h[x] Header minimum size (default 7000, try -h5000)\n");
    printf(" -k[x] Block minimum size (default 14000, try -k18000)\n");
    printf("\n");

    exit(1);
}

/*------------------------------------------------------------------------*/
/**
 * main() - Main program entry point
 * 
 * **PROGRAM FLOW:**
 * 1. Parse command line arguments
 * 2. Open and validate TAP file
 * 3. **SCAN FOR PILOT TONES** (lines 637-675)
 * 4. **BUILD BLOCK BOUNDARIES** (lines 688-726)
 * 5. Extract program names
 * 6. Allow user to merge blocks (interactive mode)
 * 7. **SAVE EACH BLOCK** with new header (lines 801-817)
 */
int main(int argc,char **argv)
{
    FILE *file_inp;
    FILE *hin;
    unsigned int fs;
    int i=0,val,batchmode=0;
    char msg1[] = "C64-TAPE-RAW";
    char  msg[] = "            ";
    char msg_join[]="\nDo you want to join 2 neighbour blocks (y/n)?\n";
    unsigned int l0,l1,l2,l3,start=0,pos_current;
    unsigned int data_len;
    int hdrminsize=7000,blockminsize=14000;  // Minimum sizes for detection
    char cleanmode = 0;    // Flag -c option

    // Structure to store pilot tone positions
    struct record_pilot
    {
      unsigned int start;  // Start position of pilot tone
      unsigned int end;    // End position of pilot tone
    } array_pilot[100];
    
    int pilot_tones=0;
    unsigned char byte,chr1,chr2;
    unsigned int count;
    int ok=0;
    unsigned int array_blocks[100];  // Array of block start positions
    int nblocks=0;
    char createidx = 0;   // Create index (idx)

    memset(array_pilot ,0,sizeof(array_pilot ));
    memset(array_blocks,0,sizeof(array_blocks));

    printf("\niTAP by @Shark (v.%s)\n",PROGVERSION);
    printf("Based on STAP by Carmine_TSM - Porting by iAN CooG\n");
    if (argc<2)
    {
        Usage();
    }

    // Parse command line arguments
    for(i=1;i<argc;i++)
    {
        if(argv[i][0] == '-')
        {
            switch ( argv[i][1]&0xdf )
            {
            case 'B':
                batchmode=1;
                break;

            case 'L':
                listonly=1;
                break;

            case 'I':           // Index
                createidx=1;
                break;

            case 'C':           // Clean
                cleanmode = 1;
                break;

            case 'N':
                addnames=1;
                if(argv[i][2])
                {
                   addnames=(argv[i][2]&0x03);
                }
                break;

            case 'D':
                verbose=1;
                if(argv[i][2])
                {
                   verbose=(argv[i][2]&0x03);
                }
                break;
            case 'H':
                hdrminsize=atoi(argv[i]+2);
                if(hdrminsize < 500 )
                   hdrminsize = 500;
                if(hdrminsize > 0xffff )
                   hdrminsize = 0xffff;
                printf("Using Header min size of %d\n",hdrminsize);
                break;
            case 'K':
                blockminsize=atoi(argv[i]+2);
                if(blockminsize < 500 )
                   blockminsize = 500;
                if(blockminsize > 0xffff )
                   blockminsize = 0xffff;
                printf("Using Block min size of %d\n",blockminsize);
                break;
            }
        }
        else
        {
            strcpy(tapname,argv[i]);
        }
    }

    if ( !*tapname )
    {
        Usage();
    }
    
    // Open TAP file
    if ( ((file_inp=fopen(tapname,"rb"))==NULL) )
    {
        printf("\nOpen error or File not found: %s.\n",tapname);
        exit(1);
    }
    
    // Validate TAP signature
    fseek(file_inp, 0, SEEK_SET);
    fread(msg,1,sizeof(msg)-1,file_inp);
    val=strcmp(msg1,msg);
    if (val)
    {
        printf("\n\nFile isn't a valid TAP!\n\n");
        exit(1);
    }
    
    // Read TAP version (byte 12)
    tap_version=(char)getc(file_inp);
    
    // Read data size from header (bytes 16-19, little-endian)
    fseek(file_inp, 16, SEEK_SET);
    l3=getc(file_inp);
    l2=getc(file_inp);
    l1=getc(file_inp);
    l0=getc(file_inp);
    data_len=(l0<<24)+(l1<<16)+(l2<<8)+l3;
    
    // Check if file size matches header
    fs=filesize(file_inp)-20;
    if ( data_len != fs )
    {
        if(!(batchmode || listonly))
        {
            printf("\nFile internal problem\n"
                   "Reported dimension 0x%08X instead of 0x%08X\n",
                   (unsigned int)data_len,
                   (unsigned int)fs );
            printf("Fix it? (Y/n)");
            ok=getch();
            printf("\n");
            if( (ok&0xdf)!='Y' )
            {
                exit(1);
            }
        }
        // Fix file size in header
        fclose(file_inp);
        hin=fopen(tapname, "r+b");
        if(!hin)
        {
            exit(1);
        }

        fseek(hin, 16, SEEK_SET);
        putc((fs    )&0xff,hin);
        putc((fs>> 8)&0xff,hin);
        putc((fs>>16)&0xff,hin);
        putc((fs>>24)&0xff,hin);
        if(!(batchmode || listonly))
            printf("Fixed.\n");
        fclose(hin);
        file_inp=fopen(tapname, "rb");
        fseek(file_inp, 20, SEEK_SET);
        data_len=fs;
    }

    // **CRITICAL SECTION: SCAN FOR PILOT TONES**
    // This section identifies where each program starts by detecting
    // long sequences of pilot tones (pulses with values 40-60)
    count=0; ok=0; pilot_tones=0; start=0;
    pos_current=20;  // Start after 20-byte header
    
    while (!feof(file_inp))
    {
        i=getc(file_inp);
        if(i==EOF)
        {
            break;
        }
        byte=(char)i;
        
        // Check if current byte is a pilot tone
        if (ispilot(byte))
        {
            if (!ok)  // Start of new pilot sequence
            {
                ok=1;
                count=1;
                start=pos_current;
            }
            else  // Continue pilot sequence
            {
                count++;
            }
        }
        else  // Non-pilot byte
        {
            if (ok)  // End of pilot sequence
            {
                ok=0;
                // If pilot sequence is long enough, record it
                if (count>hdrminsize)
                {
                    array_pilot[pilot_tones].start=start;
                    array_pilot[pilot_tones].end=pos_current-1;
                    pilot_tones++;
                }
                count=0;
            }
        }
        pos_current++;
    }

    // **BUILD BLOCK BOUNDARIES ARRAY**
    // Convert pilot tone positions to block boundaries
    array_blocks[0]=0;
    for (i=1;i<=pilot_tones;i++)
    {
        array_blocks[i]= array_pilot[i-1].start;
    }
    array_blocks[i]=data_len+20;  // End of file
    nblocks=pilot_tones+1;

    // Adjust first block to skip TAP header
    array_blocks[0]+=20;

    // **FILTER OUT SMALL BLOCKS**
    // Remove blocks that are smaller than minimum size
    for(i=0;i<=nblocks;i++)
    {
        if (array_blocks[i+1]-array_blocks[i] < blockminsize)
        {
            // Shift array to remove small block
            for (chr2=i+1;chr2<=nblocks;chr2++)
            {
                array_blocks[chr2]=array_blocks[chr2+1];
            }
            nblocks--;
            pilot_tones--;
            i--;
        }
    }

    // Print blocks list
    if(!listonly)
    {
        printf("\nBlocks list:\n");
    }
    else
    {
        printf("\n%s:\n",tapname);
    }

    for (i=0;i<nblocks;i++)
    {
        PrintBlocks(i,array_blocks,file_inp);
    }

    // ============================================================
    // Create index file if -i is active
    // ============================================================
    if(createidx)
    {
        create_idx_file(tapname, nblocks, array_blocks);
        
        // if list only, exit after create index file
        if(listonly)
        {
            return 0;
        }
    }
    // ============================================================

    if(listonly)
    {
        return 0;
    }
    
	// ============================================================
    // Clean - Create a cleaned TAP
    // ============================================================
    if(cleanmode)
    {
        create_cleaned_tap(tapname, nblocks, array_blocks, file_inp);
        return 0;  // Exit before clean file created
    }
    // ============================================================

    if (i<2)
    {
        printf("\nThere are no block to split.\n");
        exit(1);
    }
    
    // Interactive mode: allow user to merge blocks
    if(!batchmode)
    {
        printf(msg_join);
        ok=getch();
        while ( (nblocks>1) && (ok&0xdf)=='Y' )
        {
            printf("\nWhich is the first block?");

            chr1 = obtain_number(nblocks)-1;
            if (chr1<nblocks)
            {
                for (chr2=chr1+1;chr2<nblocks;chr2++)
                {
                    array_blocks[chr2]=array_blocks[chr2+1];
                }
                nblocks--;
                pilot_tones--;
            }
            printf("\nBlocks list:\n");
            for (i=0;i<nblocks;i++)
            {
                PrintBlocks(i,array_blocks,file_inp);
            }
            if(nblocks<2)
            {
                break;
            }
            printf(msg_join);
            ok=getch();
        }
    }

    printf("\nNow  %d blocks will be created with progressive names",nblocks);
    printf("\nAny file with the same name will be overwritten!");
    printf("\nTAP Version : %d",tap_version);

    if(!batchmode)
    {
        printf("\nPress Y to go on, any other key to cancel...\n");
        if ((getch()&0xdf)!='Y')
        {
            exit(1);
        }
    }
    else
    {
        printf("\n");
    }

    // **SAVE EACH BLOCK TO SEPARATE TAP FILE**
    // This calls save() for each block, which creates a new TAP file
    // with corrected header
    for (i=0;i<(pilot_tones+1);i++)
    {
        if(verbose>1)
        {
            printf("%-16s 0x%08x-0x%08x (0x%08x-0x%08x)\n",
                   blocknames[i],
                   (unsigned int)array_blocks[i],
                   (unsigned int)array_blocks[i+1],
                   (unsigned int)array_pilot[i].start,
                   (unsigned int)array_pilot[i].end);
        }
        // Save block with new TAP header
        save( array_blocks[i],
              array_blocks[i+1],
              i,
              tapname);
    }
    
    printf("\nOperation successfully completed.\n");
    return 0;
}
