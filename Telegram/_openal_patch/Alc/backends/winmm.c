/**
 * OpenAL cross platform audio library
 * Copyright (C) 1999-2007 by authors.
 * This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, write to the
 *  Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 * Or go to http://www.gnu.org/copyleft/lgpl.html
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <memory.h>

#include <windows.h>
#include <mmsystem.h>

#include "alMain.h"
#include "alu.h"
#include "threads.h"

#ifndef WAVE_FORMAT_IEEE_FLOAT
#define WAVE_FORMAT_IEEE_FLOAT  0x0003
#endif


typedef struct {
    // MMSYSTEM Device
    volatile ALboolean killNow;
    althrd_t thread;

    RefCount WaveBuffersCommitted;
    WAVEHDR WaveBuffer[4];

    union {
        HWAVEIN  In;
        HWAVEOUT Out;
    } WaveHandle;

    WAVEFORMATEX Format;

    RingBuffer *Ring;
} WinMMData;


TYPEDEF_VECTOR(al_string, vector_al_string)
static vector_al_string PlaybackDevices;
static vector_al_string CaptureDevices;

static void clear_devlist(vector_al_string *list)
{
    VECTOR_FOR_EACH(al_string, *list, al_string_deinit);
    VECTOR_RESIZE(*list, 0);
}


static void ProbePlaybackDevices(void)
{
    al_string *iter, *end;
    ALuint numdevs;
    ALuint i;

    clear_devlist(&PlaybackDevices);

    numdevs = waveOutGetNumDevs();
    VECTOR_RESERVE(PlaybackDevices, numdevs);
    for(i = 0;i < numdevs;i++)
    {
        WAVEOUTCAPSW WaveCaps;
        al_string dname;

        AL_STRING_INIT(dname);
        if(waveOutGetDevCapsW(i, &WaveCaps, sizeof(WaveCaps)) == MMSYSERR_NOERROR)
        {
            ALuint count = 0;
            do {
                al_string_copy_wcstr(&dname, WaveCaps.szPname);
                if(count != 0)
                {
                    char str[64];
                    snprintf(str, sizeof(str), " #%d", count+1);
                    al_string_append_cstr(&dname, str);
                }
                count++;

                iter = VECTOR_ITER_BEGIN(PlaybackDevices);
                end = VECTOR_ITER_END(PlaybackDevices);
                for(;iter != end;iter++)
                {
                    if(al_string_cmp(*iter, dname) == 0)
                        break;
                }
            } while(iter != end);

            TRACE("Got device \"%s\", ID %u\n", al_string_get_cstr(dname), i);
        }
        VECTOR_PUSH_BACK(PlaybackDevices, dname);
    }
}

static void ProbeCaptureDevices(void)
{
    al_string *iter, *end;
    ALuint numdevs;
    ALuint i;

    clear_devlist(&CaptureDevices);

    numdevs = waveInGetNumDevs();
    VECTOR_RESERVE(CaptureDevices, numdevs);
    for(i = 0;i < numdevs;i++)
    {
        WAVEINCAPSW WaveCaps;
        al_string dname;

        AL_STRING_INIT(dname);
        if(waveInGetDevCapsW(i, &WaveCaps, sizeof(WaveCaps)) == MMSYSERR_NOERROR)
        {
            ALuint count = 0;
            do {
                al_string_copy_wcstr(&dname, WaveCaps.szPname);
                if(count != 0)
                {
                    char str[64];
                    snprintf(str, sizeof(str), " #%d", count+1);
                    al_string_append_cstr(&dname, str);
                }
                count++;

                iter = VECTOR_ITER_BEGIN(CaptureDevices);
                end = VECTOR_ITER_END(CaptureDevices);
                for(;iter != end;iter++)
                {
                    if(al_string_cmp(*iter, dname) == 0)
                        break;
                }
            } while(iter != end);

            TRACE("Got device \"%s\", ID %u\n", al_string_get_cstr(dname), i);
        }
        VECTOR_PUSH_BACK(CaptureDevices, dname);
    }
}


/*
    WaveOutProc

    Posts a message to 'PlaybackThreadProc' everytime a WaveOut Buffer is completed and
    returns to the application (for more data)
*/
static void CALLBACK WaveOutProc(HWAVEOUT UNUSED(device), UINT msg, DWORD_PTR instance, DWORD_PTR param1, DWORD_PTR UNUSED(param2))
{
    ALCdevice *Device = (ALCdevice*)instance;
    WinMMData *data = Device->ExtraData;

    if(msg != WOM_DONE)
        return;

    DecrementRef(&data->WaveBuffersCommitted);
    PostThreadMessage(data->thread, msg, 0, param1);
}

FORCE_ALIGN static int PlaybackThreadProc(void *arg)
{
    ALCdevice *Device = (ALCdevice*)arg;
    WinMMData *data = Device->ExtraData;
    WAVEHDR *WaveHdr;
    MSG msg;

    SetRTPriority();
    althrd_setname(althrd_current(), MIXER_THREAD_NAME);

    while(GetMessage(&msg, NULL, 0, 0))
    {
        if(msg.message != WOM_DONE)
            continue;

        if(data->killNow)
        {
            if(ReadRef(&data->WaveBuffersCommitted) == 0)
                break;
            continue;
        }

        WaveHdr = ((WAVEHDR*)msg.lParam);
        aluMixData(Device, WaveHdr->lpData, WaveHdr->dwBufferLength /
                                            data->Format.nBlockAlign);

        // Send buffer back to play more data
        waveOutWrite(data->WaveHandle.Out, WaveHdr, sizeof(WAVEHDR));
        IncrementRef(&data->WaveBuffersCommitted);
    }

    return 0;
}

/*
    WaveInProc

    Posts a message to 'CaptureThreadProc' everytime a WaveIn Buffer is completed and
    returns to the application (with more data)
*/
static void CALLBACK WaveInProc(HWAVEIN UNUSED(device), UINT msg, DWORD_PTR instance, DWORD_PTR param1, DWORD_PTR UNUSED(param2))
{
    ALCdevice *Device = (ALCdevice*)instance;
    WinMMData *data = Device->ExtraData;

    if(msg != WIM_DATA)
        return;

    DecrementRef(&data->WaveBuffersCommitted);
    PostThreadMessage(data->thread, msg, 0, param1);
}

static int CaptureThreadProc(void *arg)
{
    ALCdevice *Device = (ALCdevice*)arg;
    WinMMData *data = Device->ExtraData;
    WAVEHDR *WaveHdr;
    MSG msg;

    althrd_setname(althrd_current(), "alsoft-record");

    if (!data->killNow) while(GetMessage(&msg, NULL, 0, 0))
    {
        if(msg.message != WIM_DATA)
            continue;
        /* Don't wait for other buffers to finish before quitting. We're
         * closing so we don't need them. */
        if(data->killNow)
            break;

        WaveHdr = ((WAVEHDR*)msg.lParam);
        WriteRingBuffer(data->Ring, (ALubyte*)WaveHdr->lpData,
                        WaveHdr->dwBytesRecorded/data->Format.nBlockAlign);

        // Send buffer back to capture more data
        waveInAddBuffer(data->WaveHandle.In, WaveHdr, sizeof(WAVEHDR));
        IncrementRef(&data->WaveBuffersCommitted);
    }

    return 0;
}


static ALCenum WinMMOpenPlayback(ALCdevice *Device, const ALCchar *deviceName)
{
    WinMMData *data = NULL;
    const al_string *iter, *end;
    UINT DeviceID;
    MMRESULT res;

    if(VECTOR_SIZE(PlaybackDevices) == 0)
        ProbePlaybackDevices();

    // Find the Device ID matching the deviceName if valid
    iter = VECTOR_ITER_BEGIN(PlaybackDevices);
    end = VECTOR_ITER_END(PlaybackDevices);
    for(;iter != end;iter++)
    {
        if(!al_string_empty(*iter) &&
           (!deviceName || al_string_cmp_cstr(*iter, deviceName) == 0))
        {
            DeviceID = (UINT)(iter - VECTOR_ITER_BEGIN(PlaybackDevices));
            break;
        }
    }
    if(iter == end)
        return ALC_INVALID_VALUE;

    data = calloc(1, sizeof(*data));
    if(!data)
        return ALC_OUT_OF_MEMORY;
    Device->ExtraData = data;

retry_open:
    memset(&data->Format, 0, sizeof(WAVEFORMATEX));
    if(Device->FmtType == DevFmtFloat)
    {
        data->Format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        data->Format.wBitsPerSample = 32;
    }
    else
    {
        data->Format.wFormatTag = WAVE_FORMAT_PCM;
        if(Device->FmtType == DevFmtUByte || Device->FmtType == DevFmtByte)
            data->Format.wBitsPerSample = 8;
        else
            data->Format.wBitsPerSample = 16;
    }
    data->Format.nChannels = ((Device->FmtChans == DevFmtMono) ? 1 : 2);
    data->Format.nBlockAlign = data->Format.wBitsPerSample *
                               data->Format.nChannels / 8;
    data->Format.nSamplesPerSec = Device->Frequency;
    data->Format.nAvgBytesPerSec = data->Format.nSamplesPerSec *
                                   data->Format.nBlockAlign;
    data->Format.cbSize = 0;

    if((res=waveOutOpen(&data->WaveHandle.Out, DeviceID, &data->Format, (DWORD_PTR)&WaveOutProc, (DWORD_PTR)Device, CALLBACK_FUNCTION)) != MMSYSERR_NOERROR)
    {
        if(Device->FmtType == DevFmtFloat)
        {
            Device->FmtType = DevFmtShort;
            goto retry_open;
        }
        ERR("waveOutOpen failed: %u\n", res);
        goto failure;
    }

    al_string_copy(&Device->DeviceName, VECTOR_ELEM(PlaybackDevices, DeviceID));
    return ALC_NO_ERROR;

failure:
    if(data->WaveHandle.Out)
        waveOutClose(data->WaveHandle.Out);

    free(data);
    Device->ExtraData = NULL;
    return ALC_INVALID_VALUE;
}

static void WinMMClosePlayback(ALCdevice *device)
{
    WinMMData *data = (WinMMData*)device->ExtraData;

    // Close the Wave device
    waveOutClose(data->WaveHandle.Out);
    data->WaveHandle.Out = 0;

    free(data);
    device->ExtraData = NULL;
}

static ALCboolean WinMMResetPlayback(ALCdevice *device)
{
    WinMMData *data = (WinMMData*)device->ExtraData;

    device->UpdateSize = (ALuint)((ALuint64)device->UpdateSize *
                                  data->Format.nSamplesPerSec /
                                  device->Frequency);
    device->UpdateSize = (device->UpdateSize*device->NumUpdates + 3) / 4;
    device->NumUpdates = 4;
    device->Frequency = data->Format.nSamplesPerSec;

    if(data->Format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
    {
        if(data->Format.wBitsPerSample == 32)
            device->FmtType = DevFmtFloat;
        else
        {
            ERR("Unhandled IEEE float sample depth: %d\n", data->Format.wBitsPerSample);
            return ALC_FALSE;
        }
    }
    else if(data->Format.wFormatTag == WAVE_FORMAT_PCM)
    {
        if(data->Format.wBitsPerSample == 16)
            device->FmtType = DevFmtShort;
        else if(data->Format.wBitsPerSample == 8)
            device->FmtType = DevFmtUByte;
        else
        {
            ERR("Unhandled PCM sample depth: %d\n", data->Format.wBitsPerSample);
            return ALC_FALSE;
        }
    }
    else
    {
        ERR("Unhandled format tag: 0x%04x\n", data->Format.wFormatTag);
        return ALC_FALSE;
    }

    if(data->Format.nChannels == 2)
        device->FmtChans = DevFmtStereo;
    else if(data->Format.nChannels == 1)
        device->FmtChans = DevFmtMono;
    else
    {
        ERR("Unhandled channel count: %d\n", data->Format.nChannels);
        return ALC_FALSE;
    }
    SetDefaultWFXChannelOrder(device);

    return ALC_TRUE;
}

static ALCboolean WinMMStartPlayback(ALCdevice *device)
{
    WinMMData *data = (WinMMData*)device->ExtraData;
    ALbyte *BufferData;
    ALint BufferSize;
    ALuint i;

    data->killNow = AL_FALSE;
    if(althrd_create(&data->thread, PlaybackThreadProc, device) != althrd_success)
        return ALC_FALSE;

    InitRef(&data->WaveBuffersCommitted, 0);

    // Create 4 Buffers
    BufferSize  = device->UpdateSize*device->NumUpdates / 4;
    BufferSize *= FrameSizeFromDevFmt(device->FmtChans, device->FmtType);

    BufferData = calloc(4, BufferSize);
    for(i = 0;i < 4;i++)
    {
        memset(&data->WaveBuffer[i], 0, sizeof(WAVEHDR));
        data->WaveBuffer[i].dwBufferLength = BufferSize;
        data->WaveBuffer[i].lpData = ((i==0) ? (CHAR*)BufferData :
                                      (data->WaveBuffer[i-1].lpData +
                                       data->WaveBuffer[i-1].dwBufferLength));
        waveOutPrepareHeader(data->WaveHandle.Out, &data->WaveBuffer[i], sizeof(WAVEHDR));
        waveOutWrite(data->WaveHandle.Out, &data->WaveBuffer[i], sizeof(WAVEHDR));
        IncrementRef(&data->WaveBuffersCommitted);
    }

    return ALC_TRUE;
}

static void WinMMStopPlayback(ALCdevice *device)
{
    WinMMData *data = (WinMMData*)device->ExtraData;
    void *buffer = NULL;
    int i;

    if(data->killNow)
        return;

    // Set flag to stop processing headers
    data->killNow = AL_TRUE;
    althrd_join(data->thread, &i);

    // Release the wave buffers
    for(i = 0;i < 4;i++)
    {
        waveOutUnprepareHeader(data->WaveHandle.Out, &data->WaveBuffer[i], sizeof(WAVEHDR));
        if(i == 0) buffer = data->WaveBuffer[i].lpData;
        data->WaveBuffer[i].lpData = NULL;
    }
    free(buffer);
}


static ALCenum WinMMOpenCapture(ALCdevice *Device, const ALCchar *deviceName)
{
    const al_string *iter, *end;
    ALbyte *BufferData = NULL;
    DWORD CapturedDataSize;
    WinMMData *data = NULL;
    ALint BufferSize;
    UINT DeviceID;
    MMRESULT res;
    ALuint i;

    if(VECTOR_SIZE(CaptureDevices) == 0)
        ProbeCaptureDevices();

    // Find the Device ID matching the deviceName if valid
    iter = VECTOR_ITER_BEGIN(CaptureDevices);
    end = VECTOR_ITER_END(CaptureDevices);
    for(;iter != end;iter++)
    {
        if(!al_string_empty(*iter) &&
           (!deviceName || al_string_cmp_cstr(*iter, deviceName) == 0))
        {
            DeviceID = (UINT)(iter - VECTOR_ITER_BEGIN(CaptureDevices));
            break;
        }
    }
    if(iter == end)
        return ALC_INVALID_VALUE;

    switch(Device->FmtChans)
    {
        case DevFmtMono:
        case DevFmtStereo:
            break;

        case DevFmtQuad:
        case DevFmtX51:
        case DevFmtX51Side:
        case DevFmtX61:
        case DevFmtX71:
            return ALC_INVALID_ENUM;
    }

    switch(Device->FmtType)
    {
        case DevFmtUByte:
        case DevFmtShort:
        case DevFmtInt:
        case DevFmtFloat:
            break;

        case DevFmtByte:
        case DevFmtUShort:
        case DevFmtUInt:
            return ALC_INVALID_ENUM;
    }

    data = calloc(1, sizeof(*data));
    if(!data)
        return ALC_OUT_OF_MEMORY;
    Device->ExtraData = data;

    memset(&data->Format, 0, sizeof(WAVEFORMATEX));
    data->Format.wFormatTag = ((Device->FmtType == DevFmtFloat) ?
                               WAVE_FORMAT_IEEE_FLOAT : WAVE_FORMAT_PCM);
    data->Format.nChannels = ChannelsFromDevFmt(Device->FmtChans);
    data->Format.wBitsPerSample = BytesFromDevFmt(Device->FmtType) * 8;
    data->Format.nBlockAlign = data->Format.wBitsPerSample *
                               data->Format.nChannels / 8;
    data->Format.nSamplesPerSec = Device->Frequency;
    data->Format.nAvgBytesPerSec = data->Format.nSamplesPerSec *
                                   data->Format.nBlockAlign;
    data->Format.cbSize = 0;

    if((res=waveInOpen(&data->WaveHandle.In, DeviceID, &data->Format, (DWORD_PTR)&WaveInProc, (DWORD_PTR)Device, CALLBACK_FUNCTION)) != MMSYSERR_NOERROR)
    {
        ERR("waveInOpen failed: %u\n", res);
        goto failure;
    }

    // Allocate circular memory buffer for the captured audio
    CapturedDataSize = Device->UpdateSize*Device->NumUpdates;

    // Make sure circular buffer is at least 100ms in size
    if(CapturedDataSize < (data->Format.nSamplesPerSec / 10))
        CapturedDataSize = data->Format.nSamplesPerSec / 10;

    data->Ring = CreateRingBuffer(data->Format.nBlockAlign, CapturedDataSize);
    if(!data->Ring)
        goto failure;

    InitRef(&data->WaveBuffersCommitted, 0);

    // Create 4 Buffers of 50ms each
    BufferSize = data->Format.nAvgBytesPerSec / 20;
    BufferSize -= (BufferSize % data->Format.nBlockAlign);

    BufferData = calloc(4, BufferSize);
    if(!BufferData)
        goto failure;

    for(i = 0;i < 4;i++)
    {
        memset(&data->WaveBuffer[i], 0, sizeof(WAVEHDR));
        data->WaveBuffer[i].dwBufferLength = BufferSize;
        data->WaveBuffer[i].lpData = ((i==0) ? (CHAR*)BufferData :
                                      (data->WaveBuffer[i-1].lpData +
                                       data->WaveBuffer[i-1].dwBufferLength));
        data->WaveBuffer[i].dwFlags = 0;
        data->WaveBuffer[i].dwLoops = 0;
        waveInPrepareHeader(data->WaveHandle.In, &data->WaveBuffer[i], sizeof(WAVEHDR));
        waveInAddBuffer(data->WaveHandle.In, &data->WaveBuffer[i], sizeof(WAVEHDR));
        IncrementRef(&data->WaveBuffersCommitted);
    }

    if(althrd_create(&data->thread, CaptureThreadProc, Device) != althrd_success)
        goto failure;

    al_string_copy(&Device->DeviceName, VECTOR_ELEM(CaptureDevices, DeviceID));
    return ALC_NO_ERROR;

failure:
    if(BufferData)
    {
        for(i = 0;i < 4;i++)
            waveInUnprepareHeader(data->WaveHandle.In, &data->WaveBuffer[i], sizeof(WAVEHDR));
        free(BufferData);
    }

    if(data->Ring)
        DestroyRingBuffer(data->Ring);

    if(data->WaveHandle.In)
        waveInClose(data->WaveHandle.In);

    free(data);
    Device->ExtraData = NULL;
    return ALC_INVALID_VALUE;
}

static void WinMMCloseCapture(ALCdevice *Device)
{
    WinMMData *data = (WinMMData*)Device->ExtraData;
    void *buffer = NULL;
    int i;

    /* Tell the processing thread to quit and wait for it to do so. */
    data->killNow = AL_TRUE;
    PostThreadMessage(data->thread, WM_QUIT, 0, 0);

    althrd_join(data->thread, &i);

    /* Make sure capture is stopped and all pending buffers are flushed. */
    waveInReset(data->WaveHandle.In);

    // Release the wave buffers
    for(i = 0;i < 4;i++)
    {
        waveInUnprepareHeader(data->WaveHandle.In, &data->WaveBuffer[i], sizeof(WAVEHDR));
        if(i == 0) buffer = data->WaveBuffer[i].lpData;
        data->WaveBuffer[i].lpData = NULL;
    }
    free(buffer);

    DestroyRingBuffer(data->Ring);
    data->Ring = NULL;

    // Close the Wave device
    waveInClose(data->WaveHandle.In);
    data->WaveHandle.In = 0;

    free(data);
    Device->ExtraData = NULL;
}

static void WinMMStartCapture(ALCdevice *Device)
{
    WinMMData *data = (WinMMData*)Device->ExtraData;
    waveInStart(data->WaveHandle.In);
}

static void WinMMStopCapture(ALCdevice *Device)
{
    WinMMData *data = (WinMMData*)Device->ExtraData;
    waveInStop(data->WaveHandle.In);
}

static ALCenum WinMMCaptureSamples(ALCdevice *Device, ALCvoid *Buffer, ALCuint Samples)
{
    WinMMData *data = (WinMMData*)Device->ExtraData;
    ReadRingBuffer(data->Ring, Buffer, Samples);
    return ALC_NO_ERROR;
}

static ALCuint WinMMAvailableSamples(ALCdevice *Device)
{
    WinMMData *data = (WinMMData*)Device->ExtraData;
    return RingBufferSize(data->Ring);
}


static inline void AppendAllDevicesList2(const al_string *name)
{
    if(!al_string_empty(*name))
        AppendAllDevicesList(al_string_get_cstr(*name));
}
static inline void AppendCaptureDeviceList2(const al_string *name)
{
    if(!al_string_empty(*name))
        AppendCaptureDeviceList(al_string_get_cstr(*name));
}

static const BackendFuncs WinMMFuncs = {
    WinMMOpenPlayback,
    WinMMClosePlayback,
    WinMMResetPlayback,
    WinMMStartPlayback,
    WinMMStopPlayback,
    WinMMOpenCapture,
    WinMMCloseCapture,
    WinMMStartCapture,
    WinMMStopCapture,
    WinMMCaptureSamples,
    WinMMAvailableSamples,
    ALCdevice_GetLatencyDefault
};

ALCboolean alcWinMMInit(BackendFuncs *FuncList)
{
    VECTOR_INIT(PlaybackDevices);
    VECTOR_INIT(CaptureDevices);

    *FuncList = WinMMFuncs;
    return ALC_TRUE;
}

void alcWinMMDeinit()
{
    clear_devlist(&PlaybackDevices);
    VECTOR_DEINIT(PlaybackDevices);

    clear_devlist(&CaptureDevices);
    VECTOR_DEINIT(CaptureDevices);
}

void alcWinMMProbe(enum DevProbe type)
{
    switch(type)
    {
        case ALL_DEVICE_PROBE:
            ProbePlaybackDevices();
            VECTOR_FOR_EACH(const al_string, PlaybackDevices, AppendAllDevicesList2);
            break;

        case CAPTURE_DEVICE_PROBE:
            ProbeCaptureDevices();
            VECTOR_FOR_EACH(const al_string, CaptureDevices, AppendCaptureDeviceList2);
            break;
    }
}
