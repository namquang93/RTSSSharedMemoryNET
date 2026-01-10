// OSD.h

#pragma once

#include "Structs.h"

using namespace System;
using namespace System::Collections::Generic;
using namespace System::Runtime::InteropServices;

namespace RTSSSharedMemoryNET {

    public ref class OSD
    {
    private:
        LPCSTR m_entryName;
        DWORD m_osdSlot;
        bool m_disposed;

    public:
        OSD(String^ entryName);
        ~OSD();
        !OSD();

        static property System::Version^ Version
        {
            System::Version^ get();
        }

        void Update(String^ text);
        static array<OSDEntry^>^ GetOSDEntries();
        static array<AppEntry^>^ GetAppEntries();

    private:
        static void openSharedMemory(HANDLE* phMapFile, LPRTSS_SHARED_MEMORY* ppMem);
        static void closeSharedMemory(HANDLE hMapFile, LPRTSS_SHARED_MEMORY pMem);
        static DateTime timeFromTickcount(DWORD ticks);
        
        static DWORD EmbedGraphInBuffer(LPBYTE buffer, DWORD bufferSize, DWORD offset, 
            array<float>^ data, DWORD dataPos, DWORD dataCount, 
            LONG width, LONG height, LONG margin, float min, float max, DWORD flags);
        static String^ ProcessGraphTags(String^ text, LPBYTE buffer, DWORD bufferSize, DWORD% bufferOffset);
    };

    LPCWSTR MBtoWC(const char* str);
}
