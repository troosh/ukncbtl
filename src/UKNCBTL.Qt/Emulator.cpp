// Emulator.cpp

#include "stdafx.h"
#include "main.h"
#include "mainwindow.h"
#include "Emulator.h"
#include "emubase/Emubase.h"
#include "qsoundout.h"
#include <QTime>
#include <QFile>


//////////////////////////////////////////////////////////////////////


CMotherboard* g_pBoard = NULL;
QSoundOut * g_sound = NULL;

BOOL g_okEmulatorInitialized = FALSE;
BOOL g_okEmulatorRunning = FALSE;

WORD m_wEmulatorCPUBreakpoint = 0177777;
WORD m_wEmulatorPPUBreakpoint = 0177777;

BOOL m_okEmulatorSound = FALSE;

long m_nFrameCount = 0;
QTime m_emulatorTime;
int m_nTickCount = 0;
DWORD m_dwEmulatorUptime = 0;  // UKNC uptime, seconds, from turn on or reset, increments every 25 frames
long m_nUptimeFrameCount = 0;

BYTE* g_pEmulatorRam[3];  // RAM values - for change tracking
BYTE* g_pEmulatorChangedRam[3];  // RAM change flags
WORD g_wEmulatorCpuPC = 0177777;      // Current PC value
WORD g_wEmulatorPrevCpuPC = 0177777;  // Previous PC value
WORD g_wEmulatorPpuPC = 0177777;      // Current PC value
WORD g_wEmulatorPrevPpuPC = 0177777;  // Previous PC value

const int KEYEVENT_QUEUE_SIZE = 32;
WORD m_EmulatorKeyQueue[KEYEVENT_QUEUE_SIZE];
int m_EmulatorKeyQueueTop = 0;
int m_EmulatorKeyQueueBottom = 0;
int m_EmulatorKeyQueueCount = 0;


void CALLBACK Emulator_FeedDAC(unsigned short l, unsigned short r);


//////////////////////////////////////////////////////////////////////

BOOL Emulator_Init()
{
    ASSERT(g_pBoard == NULL);

    CProcessor::Init();

    g_pBoard = new CMotherboard();

    BYTE buffer[32768];

    // Load ROM file
    memset(buffer, 0, 32768);
    QFile romFile(":/uknc_rom.bin");
    if (!romFile.open(QIODevice::ReadOnly))
    {
        AlertWarning(_T("Failed to load ROM file."));
        return false;
    }
    qint64 bytesRead = romFile.read((char*)buffer, 32256);
    ASSERT(bytesRead == 32256);
    romFile.close();

    g_pBoard->LoadROM(buffer);

    g_pBoard->Reset();

    g_sound = new QSoundOut();
    if (m_okEmulatorSound)
    {
        g_pBoard->SetSoundGenCallback(Emulator_FeedDAC);
    }

    m_nUptimeFrameCount = 0;
    m_dwEmulatorUptime = 0;

    // Allocate memory for old RAM values
    for (int i = 0; i < 3; i++)
    {
        g_pEmulatorRam[i] = (BYTE*) ::malloc(65536);  memset(g_pEmulatorRam[i], 0, 65536);
        g_pEmulatorChangedRam[i] = (BYTE*) ::malloc(65536);  memset(g_pEmulatorChangedRam[i], 0, 65536);
    }

    g_okEmulatorInitialized = TRUE;
    return TRUE;
}

void Emulator_Done()
{
    ASSERT(g_pBoard != NULL);

    CProcessor::Done();

    g_pBoard->SetSoundGenCallback(NULL);
    if(g_sound)
    {
        delete g_sound;
        g_sound = NULL;
    }

    delete g_pBoard;
    g_pBoard = NULL;

    // Free memory used for old RAM values
    for (int i = 0; i < 3; i++)
    {
        ::free(g_pEmulatorRam[i]);
        ::free(g_pEmulatorChangedRam[i]);
    }

    g_okEmulatorInitialized = FALSE;
}

void Emulator_Start()
{
    g_okEmulatorRunning = TRUE;

    m_nFrameCount = 0;
    m_emulatorTime.restart();
    m_nTickCount = 0;
}
void Emulator_Stop()
{
    g_okEmulatorRunning = FALSE;
    m_wEmulatorCPUBreakpoint = 0177777;
    m_wEmulatorPPUBreakpoint = 0177777;

    // Reset FPS indicator
    Global_showFps(-1.0);

    Global_UpdateAllViews();
}

void Emulator_Reset()
{
    ASSERT(g_pBoard != NULL);

    g_pBoard->Reset();

    m_nUptimeFrameCount = 0;
    m_dwEmulatorUptime = 0;
    Global_showUptime(0);

    Global_UpdateAllViews();
}

void Emulator_SetCPUBreakpoint(WORD address)
{
    m_wEmulatorCPUBreakpoint = address;
}
void Emulator_SetPPUBreakpoint(WORD address)
{
    m_wEmulatorPPUBreakpoint = address;
}
BOOL Emulator_IsBreakpoint()
{
    WORD wCPUAddr = g_pBoard->GetCPU()->GetPC();
    if (wCPUAddr == m_wEmulatorCPUBreakpoint)
        return TRUE;
    WORD wPPUAddr = g_pBoard->GetPPU()->GetPC();
    if (wPPUAddr == m_wEmulatorPPUBreakpoint)
        return TRUE;
    return FALSE;
}

int Emulator_SystemFrame()
{
    g_pBoard->SetCPUBreakpoint(m_wEmulatorCPUBreakpoint);
    g_pBoard->SetPPUBreakpoint(m_wEmulatorPPUBreakpoint);

    //ScreenView_ScanKeyboard();
    Emulator_ProcessKeyEvent();
    
	if (!g_pBoard->SystemFrame())
        return 0;

    // Calculate frames per second
    m_nFrameCount++;
    int nCurrentTicks = m_emulatorTime.elapsed();
    long nTicksElapsed = nCurrentTicks - m_nTickCount;
    if (nTicksElapsed >= 1200)
    {
        double dFramesPerSecond = m_nFrameCount * 1000.0 / nTicksElapsed;
        Global_showFps(dFramesPerSecond);

        m_nFrameCount = 0;
        m_nTickCount = nCurrentTicks;
    }

    // Calculate emulator uptime (25 frames per second)
    m_nUptimeFrameCount++;
    if (m_nUptimeFrameCount >= 25)
    {
        m_dwEmulatorUptime++;
        m_nUptimeFrameCount = 0;

        Global_showUptime(m_dwEmulatorUptime);
    }

    return 1;
}

float Emulator_GetUptime()
{
    return (float)m_dwEmulatorUptime + float(m_nUptimeFrameCount) / 25.0f;
}

// Update cached values after Run or Step
void Emulator_OnUpdate()
{
    // Update stored PC value
    g_wEmulatorPrevCpuPC = g_wEmulatorCpuPC;
    g_wEmulatorCpuPC = g_pBoard->GetCPU()->GetPC();
    g_wEmulatorPrevPpuPC = g_wEmulatorPpuPC;
    g_wEmulatorPpuPC = g_pBoard->GetPPU()->GetPC();

    // Update memory change flags
    for (int plane = 0; plane < 3; plane++)
    {
        BYTE* pOld = g_pEmulatorRam[plane];
        BYTE* pChanged = g_pEmulatorChangedRam[plane];
        WORD addr = 0;
        do
        {
            BYTE newvalue = g_pBoard->GetRAMByte(plane, addr);
            BYTE oldvalue = *pOld;
            *pChanged = (newvalue != oldvalue) ? 255 : 0;
            *pOld = newvalue;
            addr++;
            pOld++;  pChanged++;
        }
        while (addr < 65535);
    }
}

// Get RAM change flag
//   addrtype - address mode - see ADDRTYPE_XXX constants
WORD Emulator_GetChangeRamStatus(int addrtype, WORD address)
{
    switch (addrtype)
    {
    case ADDRTYPE_RAM0:
    case ADDRTYPE_RAM1:
    case ADDRTYPE_RAM2:
        return *((WORD*)(g_pEmulatorChangedRam[addrtype] + address));
    case ADDRTYPE_RAM12:
        if (address < 0170000)
            return MAKEWORD(
                    *(g_pEmulatorChangedRam[1] + address / 2),
                    *(g_pEmulatorChangedRam[2] + address / 2));
        else
            return 0;
    default:
        return 0;
    }
}

void Emulator_LoadROMCartridge(int slot, LPCTSTR sFilePath)
{
    // Open file
    FILE* fpFile = ::_tfopen(sFilePath, _T("rb"));
    if (fpFile == INVALID_HANDLE_VALUE)
    {
        AlertWarning(_T("Failed to load ROM cartridge image."));
        return;
    }

    // Allocate memory
    BYTE* pImage = (BYTE*) ::malloc(24 * 1024);

    DWORD dwBytesRead = ::fread(pImage, 1, 24 * 1024, fpFile);
    if (dwBytesRead != 24 * 1024)
    {
        AlertWarning(_T("Failed to load ROM cartridge image."));
        return;
    }

    g_pBoard->LoadROMCartridge(slot, pImage);

    // Free memory, close file
    ::free(pImage);
    ::fclose(fpFile);
}

void Emulator_PrepareScreenRGB32(void* pImageBits, const DWORD* colors)
{
    if (pImageBits == NULL) return;
    if (!g_okEmulatorInitialized) return;

    // Tag parsing loop
    BYTE cursorYRGB;
    BOOL okCursorType;
    BYTE cursorPos = 128;
    BOOL cursorOn = FALSE;
    BYTE cursorAddress;      // Address of graphical cursor
    WORD address = 0000270;  // Tag sequence start address
    BOOL okTagSize = FALSE;  // Tag size: TRUE - 4-word, FALSE - 2-word (first tag is always 2-word)
    BOOL okTagType = FALSE;  // Type of 4-word tag: TRUE - set palette, FALSE - set params
    int scale = 1;           // Horizontal scale: 1, 2, 4, or 8
    DWORD palette = 0;       // Palette
    BYTE pbpgpr = 7;         // 3-bit Y-value modifier
    for (int yy = 0; yy < 307; yy++) {

        if (okTagSize) {  // 4-word tag
            WORD tag1 = g_pBoard->GetRAMWord(0, address);
            address += 2;
            WORD tag2 = g_pBoard->GetRAMWord(0, address);
            address += 2;

            if (okTagType)  // 4-word palette tag
            {
                palette = MAKELONG(tag1, tag2);
            }
            else  // 4-word params tag
            {
                scale = (tag2 >> 4) & 3;  // Bits 4-5 - new scale value
                //TODO: use Y-value modifier
                pbpgpr = tag2 & 7;  // Y-value modifier
                cursorYRGB = tag1 & 15;  // Cursor color
                okCursorType = ((tag1 & 16) != 0);  // TRUE - graphical cursor, FALSE - symbolic cursor
                ASSERT(okCursorType==0);  //DEBUG
                cursorPos = ((tag1 >> 8) >> scale) & 0x7f;  // Cursor position in the line
                //TODO: Use cursorAddress
                cursorAddress = (tag1 >> 5) & 7;
                scale = 1 << scale;

            }
        }

        WORD addressBits = g_pBoard->GetRAMWord(0, address);  // The word before the last word - is address of bits from all three memory planes
        address += 2;

        // Calculate size, type and address of the next tag
        WORD tagB = g_pBoard->GetRAMWord(0, address);  // Last word of the tag - is address and type of the next tag
        okTagSize = (tagB & 2) != 0;  // Bit 1 shows size of the next tag
        if (okTagSize)
        {
            address = tagB & ~7;
            okTagType = (tagB & 4) != 0;  // Bit 2 shows type of the next tag
        }
        else
            address = tagB & ~3;
        if ((tagB & 1) != 0)
            cursorOn = !cursorOn;

        // Draw bits into the bitmap, from line 20 to line 307
        if (yy >= 19 && yy <= 306)
        {
            // Loop thru bits from addressBits, planes 0,1,2
            // For each pixel:
            //   Get bit from planes 0,1,2 and make value
            //   Map value to palette; result is 4-bit value YRGB
            //   Translate value to 24-bit RGB
            //   Put value to m_bits; repeat using scale value

            int x = 0;
            int y = yy - 19;
            DWORD* pBits = ((DWORD*)pImageBits) + y * 640;
            for (int pos = 0; ; pos++)
            {
                // Get bit from planes 0,1,2
                BYTE src0 = g_pBoard->GetRAMByte(0, addressBits);
                BYTE src1 = g_pBoard->GetRAMByte(1, addressBits);
                BYTE src2 = g_pBoard->GetRAMByte(2, addressBits);
                // Loop through the bits of the byte
                for (int bit = 0; bit < 8; bit++)
                {
                    // Make 3-bit value from the bits
                    BYTE value012 = (src0 & 1) | (src1 & 1) * 2 | (src2 & 1) * 4;
                    // Map value to palette; result is 4-bit value YRGB
                    BYTE valueYRGB;
                    if (cursorOn && (pos == cursorPos) && (!okCursorType || (okCursorType && bit == cursorAddress)))
                        valueYRGB = cursorYRGB;
                    else
                        valueYRGB = (BYTE) (palette >> (value012 * 4)) & 15;
                    DWORD valueRGB = colors[valueYRGB];

                    // Put value to m_bits; repeat using scale value
                    for (int s = 0; s < scale; s++)
                        *pBits++ = valueRGB;
                    x += scale;

                    // Shift to the next bit
                    src0 = src0 >> 1;
                    src1 = src1 >> 1;
                    src2 = src2 >> 1;
                }
                if (x >= 640)
                    break;  // End of line
                addressBits++;  // Go to the next byte
            }
        }

    }
}

void Emulator_KeyEvent(BYTE keyscan, BOOL pressed)
{
    if (m_EmulatorKeyQueueCount == KEYEVENT_QUEUE_SIZE) return;  // Full queue

    WORD keyevent = MAKEWORD(keyscan, pressed ? 128 : 0);

    m_EmulatorKeyQueue[m_EmulatorKeyQueueTop] = keyevent;
    m_EmulatorKeyQueueTop++;
    if (m_EmulatorKeyQueueTop >= KEYEVENT_QUEUE_SIZE)
        m_EmulatorKeyQueueTop = 0;
    m_EmulatorKeyQueueCount++;
}

WORD Emulator_GetKeyEventFromQueue()
{
    if (m_EmulatorKeyQueueCount == 0) return 0;  // Empty queue

    WORD keyevent = m_EmulatorKeyQueue[m_EmulatorKeyQueueBottom];
    m_EmulatorKeyQueueBottom++;
    if (m_EmulatorKeyQueueBottom >= KEYEVENT_QUEUE_SIZE)
        m_EmulatorKeyQueueBottom = 0;
    m_EmulatorKeyQueueCount--;

    return keyevent;
}

void Emulator_ProcessKeyEvent()
{
    // Process next event in the keyboard queue
    WORD keyevent = Emulator_GetKeyEventFromQueue();
    if (keyevent != 0)
    {
        BOOL pressed = ((keyevent & 0x8000) != 0);
        BYTE ukncscan = LOBYTE(keyevent);
        g_pBoard->KeyboardEvent(ukncscan, pressed);
    }
}

void CALLBACK Emulator_FeedDAC(unsigned short l, unsigned short r)
{
    if (g_sound)
    {
        if (m_okEmulatorSound)
            g_sound->FeedDAC(l,r);
    }
}

void Emulator_SetSound(BOOL enable)
{
    m_okEmulatorSound = enable;
    if (g_pBoard != 0)
    {
        if (enable)
            g_pBoard->SetSoundGenCallback(Emulator_FeedDAC);
        else
            g_pBoard->SetSoundGenCallback(0);
    }
}


//////////////////////////////////////////////////////////////////////
// Save/restore state

void Emulator_SaveImage(const QString& sFilePath)
{
    QFile file(sFilePath);
    if (! file.open(QIODevice::Truncate | QIODevice::WriteOnly))
    {
        AlertWarning(_T("Failed to save image file."));
        return;
    }

    // Allocate memory
    BYTE* pImage = (BYTE*) ::malloc(UKNCIMAGE_SIZE);
    memset(pImage, 0, UKNCIMAGE_SIZE);
    // Prepare header
    DWORD* pHeader = (DWORD*) pImage;
    *pHeader++ = UKNCIMAGE_HEADER1;
    *pHeader++ = UKNCIMAGE_HEADER2;
    *pHeader++ = UKNCIMAGE_VERSION;
    *pHeader++ = UKNCIMAGE_SIZE;
    // Store emulator state to the image
    g_pBoard->SaveToImage(pImage);
    *(DWORD*)(pImage + 16) = m_dwEmulatorUptime;

    // Save image to the file
    qint64 bytesWritten = file.write((const char *)pImage, UKNCIMAGE_SIZE);
    if (bytesWritten != UKNCIMAGE_SIZE)
    {
        AlertWarning(_T("Failed to save image file data."));
        return;
    }

    // Free memory, close file
    ::free(pImage);
    file.close();
}

void Emulator_LoadImage(const QString &sFilePath)
{
    QFile file(sFilePath);
    if (! file.open(QIODevice::ReadOnly))
    {
        AlertWarning(_T("Failed to load image file."));
        return;
    }

    Emulator_Stop();

    // Read header
    DWORD bufHeader[UKNCIMAGE_HEADER_SIZE / sizeof(DWORD)];
    qint64 bytesRead = file.read((char*)bufHeader, UKNCIMAGE_HEADER_SIZE);
    //TODO: Check if bytesRead != UKNCIMAGE_HEADER_SIZE

    //TODO: Check version and size

    // Allocate memory
    BYTE* pImage = (BYTE*) ::malloc(UKNCIMAGE_SIZE);

    // Read image
    file.seek(0);
    bytesRead = file.read((char*)pImage, UKNCIMAGE_SIZE);
    if (bytesRead != UKNCIMAGE_SIZE)
    {
        AlertWarning(_T("Failed to load image file data."));
        return;
    }
    else
    {
        // Restore emulator state from the image
        g_pBoard->LoadFromImage(pImage);

        m_dwEmulatorUptime = *(DWORD*)(pImage + 16);
    }

    // Free memory, close file
    ::free(pImage);
    file.close();
}


//////////////////////////////////////////////////////////////////////
