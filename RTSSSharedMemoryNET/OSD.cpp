// This is the main DLL file.

#include "stdafx.h"

#include "OSD.h"
#include <cfloat>

#define TICKS_PER_MICROSECOND 10
#define RTSS_VERSION(x, y) ((x << 16) + y)

namespace RTSSSharedMemoryNET {

    ///<param name="entryName">
    ///The name of the OSD entry. Should be unique and not more than 255 chars once converted to ANSI.
    ///</param>
    OSD::OSD(String^ entryName)
    {
        if( String::IsNullOrWhiteSpace(entryName) )
            throw gcnew ArgumentException("Entry name cannot be null, empty, or whitespace", "entryName");

        m_entryName = (LPCSTR)Marshal::StringToHGlobalAnsi(entryName).ToPointer();
        if( strlen(m_entryName) > 255 )
            throw gcnew ArgumentException("Entry name exceeds max length of 255 when converted to ANSI", "entryName");

        //just open/close to make sure RTSS is working
        HANDLE hMapFile = NULL;
        LPRTSS_SHARED_MEMORY pMem = NULL;
        openSharedMemory(&hMapFile, &pMem);
        closeSharedMemory(hMapFile, pMem);

        m_osdSlot = 0;
        m_disposed = false;
    }

    OSD::~OSD()
    {
        if( m_disposed )
            return;

        

        //delete managed, if any

        this->!OSD();
        m_disposed = true;
    }

    OSD::!OSD()
    {
        HANDLE hMapFile = NULL;
        LPRTSS_SHARED_MEMORY pMem = NULL;
        openSharedMemory(&hMapFile, &pMem);

        //find entries and zero them out
        for(DWORD i=1; i < pMem->dwOSDArrSize; i++)
        {
            //calc offset of entry
            auto pEntry = (RTSS_SHARED_MEMORY::LPRTSS_SHARED_MEMORY_OSD_ENTRY)( (LPBYTE)pMem + pMem->dwOSDArrOffset + (i * pMem->dwOSDEntrySize) );

            if( STRMATCHES(strcmp(pEntry->szOSDOwner, m_entryName)) )
            {
                SecureZeroMemory(pEntry, pMem->dwOSDEntrySize); //won't get optimized away
                pMem->dwOSDFrame++; //forces OSD update
            }
        }

        closeSharedMemory(hMapFile, pMem);
        Marshal::FreeHGlobal(IntPtr((LPVOID)m_entryName));
    }

    System::Version^ OSD::Version::get()
    {
        HANDLE hMapFile = NULL;
        LPRTSS_SHARED_MEMORY pMem = NULL;
        openSharedMemory(&hMapFile, &pMem);

        auto ver = gcnew System::Version(pMem->dwVersion >> 16, pMem->dwVersion & 0xFFFF);

        closeSharedMemory(hMapFile, pMem);
        return ver;
    }

    ///<summary>
    ///Text should be no longer than 4095 chars once converted to ANSI. Lower case looks awful.
    ///</summary>
    void OSD::Update(String^ text)
    {
        if( text == nullptr )
            throw gcnew ArgumentNullException("text");

        HANDLE hMapFile = NULL;
        LPRTSS_SHARED_MEMORY pMem = NULL;
        openSharedMemory(&hMapFile, &pMem);

        // Allocate buffer for embedded objects (v2.12+)
        LPBYTE lpBuffer = nullptr;
        DWORD dwBufferSize = 262144; // Size of pEntry->buffer
        DWORD dwBufferOffset = 0;
        String^ processedText = text;

        if( pMem->dwVersion >= RTSS_VERSION(2,12) )
        {
            lpBuffer = new BYTE[dwBufferSize];
            ZeroMemory(lpBuffer, dwBufferSize);
            
            // Process graph tags and get modified text
            processedText = ProcessGraphTags(text, lpBuffer, dwBufferSize, dwBufferOffset);
        }

        LPCSTR lpText = (LPCSTR)Marshal::StringToHGlobalAnsi(processedText).ToPointer();
        if( strlen(lpText) > 4095 )
        {
            if( lpBuffer != nullptr )
                delete[] lpBuffer;
            Marshal::FreeHGlobal(IntPtr((LPVOID)lpText));
            closeSharedMemory(hMapFile, pMem);
            throw gcnew ArgumentException("Text exceeds max length of 4095 when converted to ANSI", "text");
        }

        //start at either our previously used slot, or the top
        for(DWORD i=(m_osdSlot == 0 ? 1 : m_osdSlot); i < pMem->dwOSDArrSize; i++)
        {
            auto pEntry = (RTSS_SHARED_MEMORY::LPRTSS_SHARED_MEMORY_OSD_ENTRY)( (LPBYTE)pMem + pMem->dwOSDArrOffset + (i * pMem->dwOSDEntrySize) );

            //if we need a new slot and this one is unused, claim it
            if( m_osdSlot == 0 && !strlen(pEntry->szOSDOwner) )
            {
                m_osdSlot = i;
                strcpy_s(pEntry->szOSDOwner, m_entryName);
            }

            //if this is our slot
            if( STRMATCHES(strcmp(pEntry->szOSDOwner, m_entryName)) )
            {
                //use extended text slot for v2.7 and higher shared memory, it allows displaying 4096 symbols instead of 256 for regular text slot
                if( pMem->dwVersion >= RTSS_VERSION(2,7) )
                    strncpy_s(pEntry->szOSDEx, lpText, sizeof(pEntry->szOSDEx)-1);
                else
                    strncpy_s(pEntry->szOSD, lpText, sizeof(pEntry->szOSD)-1);

                // Copy embedded object buffer if we have data (v2.12+)
                if( lpBuffer != nullptr && dwBufferOffset > 0 )
                    CopyMemory(pEntry->buffer, lpBuffer, min(dwBufferOffset, sizeof(pEntry->buffer)));

                pMem->dwOSDFrame++; //forces OSD update
                break;
            }

            //in case we lost our previously used slot or something, let's start over
            if( m_osdSlot != 0 )
            {
                m_osdSlot = 0;
                i = 1;
            }
        }

        if( lpBuffer != nullptr )
            delete[] lpBuffer;

        closeSharedMemory(hMapFile, pMem);
        Marshal::FreeHGlobal(IntPtr((LPVOID)lpText));
    }

    array<OSDEntry^>^ OSD::GetOSDEntries()
    {
        HANDLE hMapFile = NULL;
        LPRTSS_SHARED_MEMORY pMem = NULL;
        openSharedMemory(&hMapFile, &pMem);

        auto list = gcnew List<OSDEntry^>;

        //include all slots
        for(DWORD i=0; i < pMem->dwOSDArrSize; i++)
        {
            auto pEntry = (RTSS_SHARED_MEMORY::LPRTSS_SHARED_MEMORY_OSD_ENTRY)( (LPBYTE)pMem + pMem->dwOSDArrOffset + (i * pMem->dwOSDEntrySize) );
            if( strlen(pEntry->szOSDOwner) )
            {
                auto entry = gcnew OSDEntry;
                entry->Owner = Marshal::PtrToStringAnsi(IntPtr(pEntry->szOSDOwner));

                if( pMem->dwVersion >= RTSS_VERSION(2,7) )
                    entry->Text = Marshal::PtrToStringAnsi(IntPtr(pEntry->szOSDEx));
                else
                    entry->Text = Marshal::PtrToStringAnsi(IntPtr(pEntry->szOSD));

                list->Add(entry);
            }
        }

        closeSharedMemory(hMapFile, pMem);
        return list->ToArray();
    }

    array<AppEntry^>^ OSD::GetAppEntries()
    {
        HANDLE hMapFile = NULL;
        LPRTSS_SHARED_MEMORY pMem = NULL;
        openSharedMemory(&hMapFile, &pMem);

        auto list = gcnew List<AppEntry^>;

        //include all slots
        for(DWORD i=0; i < pMem->dwAppArrSize; i++)
        {
            auto pEntry = (RTSS_SHARED_MEMORY::LPRTSS_SHARED_MEMORY_APP_ENTRY)( (LPBYTE)pMem + pMem->dwAppArrOffset + (i * pMem->dwAppEntrySize) );
            if( pEntry->dwProcessID )
            {
                auto entry = gcnew AppEntry;
                
                //basic fields
                entry->ProcessId = pEntry->dwProcessID;
                entry->Name = Marshal::PtrToStringAnsi(IntPtr(pEntry->szName));
                entry->Flags = (AppFlags)pEntry->dwFlags;

                //instantaneous framerate fields
                entry->InstantaneousTimeStart = timeFromTickcount(pEntry->dwTime0);
                entry->InstantaneousTimeEnd = timeFromTickcount(pEntry->dwTime1);
                entry->InstantaneousFrames = pEntry->dwFrames;
                entry->InstantaneousFrameTime = TimeSpan::FromTicks(pEntry->dwFrameTime * TICKS_PER_MICROSECOND);

                //framerate stats fields
                entry->StatFlags = (StatFlags)pEntry->dwStatFlags;
                entry->StatTimeStart = timeFromTickcount(pEntry->dwStatTime0);
                entry->StatTimeEnd = timeFromTickcount(pEntry->dwStatTime1);
                entry->StatFrames = pEntry->dwStatFrames;
                entry->StatCount = pEntry->dwStatCount;
                entry->StatFramerateMin = pEntry->dwStatFramerateMin;
                entry->StatFramerateAvg = pEntry->dwStatFramerateAvg;
                entry->StatFramerateMax = pEntry->dwStatFramerateMax;
                if( pMem->dwVersion >= RTSS_VERSION(2,5) )
                {
                    entry->StatFrameTimeMin = pEntry->dwStatFrameTimeMin;
                    entry->StatFrameTimeAvg = pEntry->dwStatFrameTimeAvg;
                    entry->StatFrameTimeMax = pEntry->dwStatFrameTimeMax;
                    entry->StatFrameTimeCount = pEntry->dwStatFrameTimeCount;
                    //TODO - frametime buffer?
                }

                //OSD fields
                entry->OSDCoordinateX = pEntry->dwOSDX;
                entry->OSDCoordinateY = pEntry->dwOSDY;
                entry->OSDZoom = pEntry->dwOSDPixel;
                entry->OSDFrameId = pEntry->dwOSDFrame;
                entry->OSDColor = Color::FromArgb(pEntry->dwOSDColor);
                if( pMem->dwVersion >= RTSS_VERSION(2,1) )
                    entry->OSDBackgroundColor = Color::FromArgb(pEntry->dwOSDBgndColor);

                //screenshot fields
                entry->ScreenshotFlags = (ScreenshotFlags)pEntry->dwScreenCaptureFlags;
                entry->ScreenshotPath = Marshal::PtrToStringAnsi(IntPtr(pEntry->szScreenCapturePath));
                if( pMem->dwVersion >= RTSS_VERSION(2,2) )
                {
                    entry->ScreenshotQuality = pEntry->dwScreenCaptureQuality;
                    entry->ScreenshotThreads = pEntry->dwScreenCaptureThreads;
                }

                //video capture fields
                if( pMem->dwVersion >= RTSS_VERSION(2,2) )
                {
                    entry->VideoCaptureFlags = (VideoCaptureFlags)pEntry->dwVideoCaptureFlags;
                    entry->VideoCapturePath = Marshal::PtrToStringAnsi(IntPtr(pEntry->szVideoCapturePath));
                    entry->VideoFramerate = pEntry->dwVideoFramerate;
                    entry->VideoFramesize = pEntry->dwVideoFramesize;
                    entry->VideoFormat = pEntry->dwVideoFormat;
                    entry->VideoQuality = pEntry->dwVideoQuality;
                    entry->VideoCaptureThreads = pEntry->dwVideoCaptureThreads;
                }
                if( pMem->dwVersion >= RTSS_VERSION(2,4) )
                    entry->VideoCaptureFlagsEx = pEntry->dwVideoCaptureFlagsEx;

                //audio capture fields
                if( pMem->dwVersion >= RTSS_VERSION(2,3) )
                    entry->AudioCaptureFlags = pEntry->dwAudioCaptureFlags;
                if( pMem->dwVersion >= RTSS_VERSION(2,5) )
                    entry->AudioCaptureFlags2 = pEntry->dwAudioCaptureFlags2;
                if( pMem->dwVersion >= RTSS_VERSION(2,6) )
                {
                    entry->AudioCapturePTTEventPush = pEntry->qwAudioCapturePTTEventPush.QuadPart;
                    entry->AudioCapturePTTEventRelease = pEntry->qwAudioCapturePTTEventRelease.QuadPart;
                    entry->AudioCapturePTTEventPush2 = pEntry->qwAudioCapturePTTEventPush2.QuadPart;
                    entry->AudioCapturePTTEventRelease2 = pEntry->qwAudioCapturePTTEventRelease2.QuadPart;
                }

                list->Add(entry);
            }
        }

        closeSharedMemory(hMapFile, pMem);
        return list->ToArray();
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    void OSD::openSharedMemory(HANDLE* phMapFile, LPRTSS_SHARED_MEMORY* ppMem)
    {
        HANDLE hMapFile = NULL;
        LPRTSS_SHARED_MEMORY pMem = NULL;
        try
        {
            hMapFile = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, L"RTSSSharedMemoryV2");
            if( !hMapFile )
                THROW_LAST_ERROR();

            pMem = (LPRTSS_SHARED_MEMORY)MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, 0);
            if( !pMem )
                THROW_LAST_ERROR();

            if( !(pMem->dwSignature == 'RTSS' && pMem->dwVersion >= RTSS_VERSION(2,0)) )
                throw gcnew System::IO::InvalidDataException("Failed to validate RTSS Shared Memory structure");

            *phMapFile = hMapFile;
            *ppMem = pMem;
        }
        catch(...)
        {
            closeSharedMemory(hMapFile, pMem);
            throw;
        }
    }

    void OSD::closeSharedMemory(HANDLE hMapFile, LPRTSS_SHARED_MEMORY pMem)
    {
        if( pMem )
            UnmapViewOfFile(pMem);

        if( hMapFile )
            CloseHandle(hMapFile);

    }

    DateTime OSD::timeFromTickcount(DWORD ticks)
    {
        return DateTime::Now - TimeSpan::FromMilliseconds(ticks);
    }

    ///////////////////////////////////////////////////////////////////////////////
    // Graph embedding helper methods
    ///////////////////////////////////////////////////////////////////////////////

    DWORD OSD::EmbedGraphInBuffer(LPBYTE lpObjBuffer, DWORD dwObjBufferSize, DWORD dwOffset, 
        array<float>^ lpBuffer, DWORD dwBufferPos, DWORD dwBufferSize, 
        LONG dwWidth, LONG dwHeight, LONG dwMargin, FLOAT fltMin, FLOAT fltMax, DWORD dwFlags)
    {
        DWORD dwResult = 0;

        if (dwOffset + sizeof(RTSS_EMBEDDED_OBJECT_GRAPH) + dwBufferSize * sizeof(FLOAT) > dwObjBufferSize)
            //validate embedded object offset and size and ensure that we don't overrun the buffer
            return 0;

        LPRTSS_EMBEDDED_OBJECT_GRAPH lpGraph = (LPRTSS_EMBEDDED_OBJECT_GRAPH)(lpObjBuffer + dwOffset);
            //get pointer to object in buffer

        lpGraph->header.dwSignature = RTSS_EMBEDDED_OBJECT_GRAPH_SIGNATURE;
        lpGraph->header.dwSize      = sizeof(RTSS_EMBEDDED_OBJECT_GRAPH) + dwBufferSize * sizeof(FLOAT);
        lpGraph->header.dwWidth     = dwWidth;
        lpGraph->header.dwHeight    = dwHeight;
        lpGraph->header.dwMargin    = dwMargin;
        lpGraph->dwFlags            = dwFlags;
        lpGraph->fltMin             = fltMin;
        lpGraph->fltMax             = fltMax;
        lpGraph->dwDataCount        = dwBufferSize;

        if (lpBuffer != nullptr && dwBufferSize > 0)
        {
            pin_ptr<float> pinnedBuffer = &lpBuffer[0];
            for (DWORD dwPos=0; dwPos<dwBufferSize; dwPos++)
            {
                FLOAT fltData = pinnedBuffer[dwBufferPos];

                lpGraph->fltData[dwPos] = (fltData == FLT_MAX) ? 0 : fltData;

                dwBufferPos = (dwBufferPos + 1) & (dwBufferSize - 1);
            }
        }

        dwResult = lpGraph->header.dwSize;

        return dwResult;
    }

    String^ OSD::ProcessGraphTags(String^ text, LPBYTE buffer, DWORD bufferSize, DWORD% bufferOffset)
    {
        String^ result = text;
        int searchPos = 0;

        // Search for graph tags in RTSS format: <G=source> or <G=source,width,height>
        while (true)
        {
            int tagStart = result->IndexOf("<G=", searchPos);
            if (tagStart == -1)
                break;

            // Find the matching closing '>' by counting nested brackets
            int tagEnd = -1;
            int bracketCount = 1; // We've already seen the opening '<'
            for (int i = tagStart + 1; i < result->Length; i++)
            {
                if (result[i] == '<')
                    bracketCount++;
                else if (result[i] == '>')
                {
                    bracketCount--;
                    if (bracketCount == 0)
                    {
                        tagEnd = i;
                        break;
                    }
                }
            }

            if (tagEnd == -1)
                break; // No matching closing bracket found

            // Extract the full tag content: everything between <G= and >
            String^ fullTag = result->Substring(tagStart, tagEnd - tagStart + 1);
            String^ tagContent = result->Substring(tagStart + 3, tagEnd - tagStart - 3);

            // Default values
            DWORD dwFlags = 0;
            LONG dwWidth = -64;   // Default: 64 chars wide
            LONG dwHeight = -1;   // Default: 1 char tall
            LONG dwMargin = 1;
            FLOAT fltMin = 0.0f;
            FLOAT fltMax = 200.0f;

            // Parse the tag content: source[,width[,height]]
            array<String^>^ parts = tagContent->Split(',');
            
            if (parts->Length == 0)
            {
                searchPos = tagEnd + 1;
                continue;
            }

            // Parse source (first part)
            String^ source = parts[0]->Trim();
            
            // Determine graph type from source
            if (source == "<FT>" || source == "%Frametime%")
            {
                dwFlags = RTSS_EMBEDDED_OBJECT_GRAPH_FLAG_FRAMETIME;
                fltMax = 50000.0f;
            }
            else if (source == "<FR>" || source == "%Framerate%")
            {
                dwFlags = RTSS_EMBEDDED_OBJECT_GRAPH_FLAG_FRAMERATE;
                fltMax = 200.0f;
            }
            else
            {
                // Unknown source, skip
                searchPos = tagEnd + 1;
                continue;
            }

            // Parse optional width (second part)
            if (parts->Length > 1)
            {
                String^ widthStr = parts[1]->Trim();
                int width;
                if (Int32::TryParse(widthStr, width))
                    dwWidth = width;
            }

            // Parse optional height (third part)
            if (parts->Length > 2)
            {
                String^ heightStr = parts[2]->Trim();
                int height;
                if (Int32::TryParse(heightStr, height))
                    dwHeight = height;
            }

            // Embed graph (with null data - RTSS will auto-populate from app stats)
            DWORD dwObjectSize = EmbedGraphInBuffer(buffer, bufferSize, bufferOffset, 
                nullptr, 0, 0, dwWidth, dwHeight, dwMargin, fltMin, fltMax, dwFlags);

            if (dwObjectSize > 0)
            {
                // Replace <G=...> with <OBJ=XXXXXXXX>
                String^ objTag = String::Format("<OBJ={0:X8}>", bufferOffset);
                result = result->Remove(tagStart, fullTag->Length);
                result = result->Insert(tagStart, objTag);

                bufferOffset += dwObjectSize;
                searchPos = tagStart + objTag->Length;
            }
            else
            {
                // Failed to embed, skip this tag
                searchPos = tagEnd + 1;
            }
        }

        return result;
    }
}