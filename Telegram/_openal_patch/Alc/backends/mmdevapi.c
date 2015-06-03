/**
 * OpenAL cross platform audio library
 * Copyright (C) 2011 by authors.
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

#define COBJMACROS
#include <stdlib.h>
#include <stdio.h>
#include <memory.h>

#include <mmdeviceapi.h>
#include <audioclient.h>
#include <cguid.h>
#include <devpropdef.h>
#include <mmreg.h>
#include <propsys.h>
#include <propkey.h>
#include <devpkey.h>
#ifndef _WAVEFORMATEXTENSIBLE_
#include <ks.h>
#include <ksmedia.h>
#endif

#include "alMain.h"
#include "alu.h"
#include "threads.h"
#include "compat.h"
#include "alstring.h"

#include "backends/base.h"


DEFINE_GUID(KSDATAFORMAT_SUBTYPE_PCM, 0x00000001, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);
DEFINE_GUID(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, 0x00000003, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);

DEFINE_DEVPROPKEY(DEVPKEY_Device_FriendlyName, 0xa45c254e, 0xdf1c, 0x4efd, 0x80,0x20, 0x67,0xd1,0x46,0xa8,0x50,0xe0, 14);
DEFINE_PROPERTYKEY(PKEY_AudioEndpoint_FormFactor, 0x1da5d803, 0xd492, 0x4edd, 0x8c,0x23, 0xe0,0xc0,0xff,0xee,0x7f,0x0e, 0);

#define MONO SPEAKER_FRONT_CENTER
#define STEREO (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT)
#define QUAD (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT|SPEAKER_BACK_LEFT|SPEAKER_BACK_RIGHT)
#define X5DOT1 (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT|SPEAKER_FRONT_CENTER|SPEAKER_LOW_FREQUENCY|SPEAKER_SIDE_LEFT|SPEAKER_SIDE_RIGHT)
#define X5DOT1REAR (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT|SPEAKER_FRONT_CENTER|SPEAKER_LOW_FREQUENCY|SPEAKER_BACK_LEFT|SPEAKER_BACK_RIGHT)
#define X6DOT1 (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT|SPEAKER_FRONT_CENTER|SPEAKER_LOW_FREQUENCY|SPEAKER_BACK_CENTER|SPEAKER_SIDE_LEFT|SPEAKER_SIDE_RIGHT)
#define X7DOT1 (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT|SPEAKER_FRONT_CENTER|SPEAKER_LOW_FREQUENCY|SPEAKER_BACK_LEFT|SPEAKER_BACK_RIGHT|SPEAKER_SIDE_LEFT|SPEAKER_SIDE_RIGHT)
#define X7DOT1_WIDE (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT|SPEAKER_FRONT_CENTER|SPEAKER_LOW_FREQUENCY|SPEAKER_BACK_LEFT|SPEAKER_BACK_RIGHT|SPEAKER_FRONT_LEFT_OF_CENTER|SPEAKER_FRONT_RIGHT_OF_CENTER)


typedef struct {
    al_string name;
    WCHAR *devid;
} DevMap;
TYPEDEF_VECTOR(DevMap, vector_DevMap)

static void clear_devlist(vector_DevMap *list)
{
#define CLEAR_DEVMAP(i) do {     \
    AL_STRING_DEINIT((i)->name); \
    free((i)->devid);            \
    (i)->devid = NULL;           \
} while(0)
    VECTOR_FOR_EACH(DevMap, *list, CLEAR_DEVMAP);
    VECTOR_RESIZE(*list, 0);
#undef CLEAR_DEVMAP
}

static vector_DevMap PlaybackDevices;
static vector_DevMap CaptureDevices;


static HANDLE ThreadHdl;
static DWORD ThreadID;

typedef struct {
    HANDLE FinishedEvt;
    HRESULT result;
} ThreadRequest;

#define WM_USER_First       (WM_USER+0)
#define WM_USER_OpenDevice  (WM_USER+0)
#define WM_USER_ResetDevice (WM_USER+1)
#define WM_USER_StartDevice (WM_USER+2)
#define WM_USER_StopDevice  (WM_USER+3)
#define WM_USER_CloseDevice (WM_USER+4)
#define WM_USER_Enumerate   (WM_USER+5)
#define WM_USER_Last        (WM_USER+5)

static inline void ReturnMsgResponse(ThreadRequest *req, HRESULT res)
{
    req->result = res;
    SetEvent(req->FinishedEvt);
}

static HRESULT WaitForResponse(ThreadRequest *req)
{
    if(WaitForSingleObject(req->FinishedEvt, INFINITE) == WAIT_OBJECT_0)
        return req->result;
    ERR("Message response error: %lu\n", GetLastError());
    return E_FAIL;
}


static void get_device_name(IMMDevice *device, al_string *name)
{
    IPropertyStore *ps;
    PROPVARIANT pvname;
    HRESULT hr;

    hr = IMMDevice_OpenPropertyStore(device, STGM_READ, &ps);
    if(FAILED(hr))
    {
        WARN("OpenPropertyStore failed: 0x%08lx\n", hr);
        return;
    }

    PropVariantInit(&pvname);

    hr = IPropertyStore_GetValue(ps, (const PROPERTYKEY*)&DEVPKEY_Device_FriendlyName, &pvname);
    if(FAILED(hr))
        WARN("GetValue Device_FriendlyName failed: 0x%08lx\n", hr);
    else if(pvname.vt == VT_LPWSTR)
        al_string_copy_wcstr(name, pvname.pwszVal);
    else
        WARN("Unexpected PROPVARIANT type: 0x%04x\n", pvname.vt);

    PropVariantClear(&pvname);
    IPropertyStore_Release(ps);
}

static void get_device_formfactor(IMMDevice *device, EndpointFormFactor *formfactor)
{
    IPropertyStore *ps;
    PROPVARIANT pvform;
    HRESULT hr;

    hr = IMMDevice_OpenPropertyStore(device, STGM_READ, &ps);
    if(FAILED(hr))
    {
        WARN("OpenPropertyStore failed: 0x%08lx\n", hr);
        return;
    }

    PropVariantInit(&pvform);

    hr = IPropertyStore_GetValue(ps, &PKEY_AudioEndpoint_FormFactor, &pvform);
    if(FAILED(hr))
        WARN("GetValue AudioEndpoint_FormFactor failed: 0x%08lx\n", hr);
    else if(pvform.vt == VT_UI4)
        *formfactor = pvform.ulVal;
    else if(pvform.vt == VT_EMPTY)
        *formfactor = UnknownFormFactor;
    else
        WARN("Unexpected PROPVARIANT type: 0x%04x\n", pvform.vt);

    PropVariantClear(&pvform);
    IPropertyStore_Release(ps);
}


static void add_device(IMMDevice *device, LPCWSTR devid, vector_DevMap *list)
{
    DevMap entry;

    AL_STRING_INIT(entry.name);
    entry.devid = strdupW(devid);
    get_device_name(device, &entry.name);

    TRACE("Got device \"%s\", \"%ls\"\n", al_string_get_cstr(entry.name), entry.devid);
    VECTOR_PUSH_BACK(*list, entry);
}

static LPWSTR get_device_id(IMMDevice *device)
{
    LPWSTR devid;
    HRESULT hr;

    hr = IMMDevice_GetId(device, &devid);
    if(FAILED(hr))
    {
        ERR("Failed to get device id: %lx\n", hr);
        return NULL;
    }

    return devid;
}

static HRESULT probe_devices(IMMDeviceEnumerator *devenum, EDataFlow flowdir, vector_DevMap *list)
{
    IMMDeviceCollection *coll;
    IMMDevice *defdev = NULL;
    LPWSTR defdevid = NULL;
    HRESULT hr;
    UINT count;
    UINT i;

    hr = IMMDeviceEnumerator_EnumAudioEndpoints(devenum, flowdir, DEVICE_STATE_ACTIVE, &coll);
    if(FAILED(hr))
    {
        ERR("Failed to enumerate audio endpoints: 0x%08lx\n", hr);
        return hr;
    }

    count = 0;
    hr = IMMDeviceCollection_GetCount(coll, &count);
    if(SUCCEEDED(hr) && count > 0)
    {
        clear_devlist(list);
        if(!VECTOR_RESERVE(*list, count))
        {
            IMMDeviceCollection_Release(coll);
            return E_OUTOFMEMORY;
        }

        hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(devenum, flowdir,
                                                         eMultimedia, &defdev);
    }
    if(SUCCEEDED(hr) && defdev != NULL)
    {
        defdevid = get_device_id(defdev);
        if(defdevid)
            add_device(defdev, defdevid, list);
    }

    for(i = 0;i < count;++i)
    {
        IMMDevice *device;
        LPWSTR devid;

        hr = IMMDeviceCollection_Item(coll, i, &device);
        if(FAILED(hr)) continue;

        devid = get_device_id(device);
        if(devid)
        {
            if(wcscmp(devid, defdevid) != 0)
                add_device(device, devid, list);
            CoTaskMemFree(devid);
        }
        IMMDevice_Release(device);
    }

    if(defdev) IMMDevice_Release(defdev);
    if(defdevid) CoTaskMemFree(defdevid);
    IMMDeviceCollection_Release(coll);

    return S_OK;
}


/* Proxy interface used by the message handler. */
struct ALCmmdevProxyVtable;

typedef struct ALCmmdevProxy {
    const struct ALCmmdevProxyVtable *vtbl;
} ALCmmdevProxy;

struct ALCmmdevProxyVtable {
    HRESULT (*const openProxy)(ALCmmdevProxy*);
    void (*const closeProxy)(ALCmmdevProxy*);

    HRESULT (*const resetProxy)(ALCmmdevProxy*);
    HRESULT (*const startProxy)(ALCmmdevProxy*);
    void  (*const stopProxy)(ALCmmdevProxy*);
};

#define DEFINE_ALCMMDEVPROXY_VTABLE(T)                                        \
DECLARE_THUNK(T, ALCmmdevProxy, HRESULT, openProxy)                           \
DECLARE_THUNK(T, ALCmmdevProxy, void, closeProxy)                             \
DECLARE_THUNK(T, ALCmmdevProxy, HRESULT, resetProxy)                          \
DECLARE_THUNK(T, ALCmmdevProxy, HRESULT, startProxy)                          \
DECLARE_THUNK(T, ALCmmdevProxy, void, stopProxy)                              \
                                                                              \
static const struct ALCmmdevProxyVtable T##_ALCmmdevProxy_vtable = {          \
    T##_ALCmmdevProxy_openProxy,                                              \
    T##_ALCmmdevProxy_closeProxy,                                             \
    T##_ALCmmdevProxy_resetProxy,                                             \
    T##_ALCmmdevProxy_startProxy,                                             \
    T##_ALCmmdevProxy_stopProxy,                                              \
}

static void ALCmmdevProxy_Construct(ALCmmdevProxy* UNUSED(self)) { }
static void ALCmmdevProxy_Destruct(ALCmmdevProxy* UNUSED(self)) { }

static DWORD CALLBACK ALCmmdevProxy_messageHandler(void *ptr)
{
    ThreadRequest *req = ptr;
    IMMDeviceEnumerator *Enumerator;
    ALuint deviceCount = 0;
    ALCmmdevProxy *proxy;
    HRESULT hr, cohr;
    MSG msg;

    TRACE("Starting message thread\n");

    cohr = CoInitialize(NULL);
    if(FAILED(cohr))
    {
        WARN("Failed to initialize COM: 0x%08lx\n", cohr);
        ReturnMsgResponse(req, cohr);
        return 0;
    }

    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_INPROC_SERVER, &IID_IMMDeviceEnumerator, &ptr);
    if(FAILED(hr))
    {
        WARN("Failed to create IMMDeviceEnumerator instance: 0x%08lx\n", hr);
        CoUninitialize();
        ReturnMsgResponse(req, hr);
        return 0;
    }
    Enumerator = ptr;
    IMMDeviceEnumerator_Release(Enumerator);
    Enumerator = NULL;

    CoUninitialize();

    /* HACK: Force Windows to create a message queue for this thread before
     * returning success, otherwise PostThreadMessage may fail if it gets
     * called before GetMessage.
     */
    PeekMessage(&msg, NULL, WM_USER, WM_USER, PM_NOREMOVE);

    TRACE("Message thread initialization complete\n");
    ReturnMsgResponse(req, S_OK);

    TRACE("Starting message loop\n");
    while(GetMessage(&msg, NULL, WM_USER_First, WM_USER_Last))
    {
        TRACE("Got message %u (lparam=%p, wparam=%p)\n", msg.message, (void*)msg.lParam, (void*)msg.wParam);
        switch(msg.message)
        {
        case WM_USER_OpenDevice:
            req = (ThreadRequest*)msg.wParam;
            proxy = (ALCmmdevProxy*)msg.lParam;

            hr = cohr = S_OK;
            if(++deviceCount == 1)
                hr = cohr = CoInitialize(NULL);
            if(SUCCEEDED(hr))
                hr = V0(proxy,openProxy)();
            if(FAILED(hr))
            {
                if(--deviceCount == 0 && SUCCEEDED(cohr))
                    CoUninitialize();
            }

            ReturnMsgResponse(req, hr);
            continue;

        case WM_USER_ResetDevice:
            req = (ThreadRequest*)msg.wParam;
            proxy = (ALCmmdevProxy*)msg.lParam;

            hr = V0(proxy,resetProxy)();
            ReturnMsgResponse(req, hr);
            continue;

        case WM_USER_StartDevice:
            req = (ThreadRequest*)msg.wParam;
            proxy = (ALCmmdevProxy*)msg.lParam;

            hr = V0(proxy,startProxy)();
            ReturnMsgResponse(req, hr);
            continue;

        case WM_USER_StopDevice:
            req = (ThreadRequest*)msg.wParam;
            proxy = (ALCmmdevProxy*)msg.lParam;

            V0(proxy,stopProxy)();
            ReturnMsgResponse(req, S_OK);
            continue;

        case WM_USER_CloseDevice:
            req = (ThreadRequest*)msg.wParam;
            proxy = (ALCmmdevProxy*)msg.lParam;

            V0(proxy,closeProxy)();
            if(--deviceCount == 0)
                CoUninitialize();

            ReturnMsgResponse(req, S_OK);
            continue;

        case WM_USER_Enumerate:
            req = (ThreadRequest*)msg.wParam;

            hr = cohr = S_OK;
            if(++deviceCount == 1)
                hr = cohr = CoInitialize(NULL);
            if(SUCCEEDED(hr))
                hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_INPROC_SERVER, &IID_IMMDeviceEnumerator, &ptr);
            if(SUCCEEDED(hr))
            {
                Enumerator = ptr;

                if(msg.lParam == ALL_DEVICE_PROBE)
                    hr = probe_devices(Enumerator, eRender, &PlaybackDevices);
                else if(msg.lParam == CAPTURE_DEVICE_PROBE)
                    hr = probe_devices(Enumerator, eCapture, &CaptureDevices);

                IMMDeviceEnumerator_Release(Enumerator);
                Enumerator = NULL;
            }

            if(--deviceCount == 0 && SUCCEEDED(cohr))
                CoUninitialize();

            ReturnMsgResponse(req, hr);
            continue;

        default:
            ERR("Unexpected message: %u\n", msg.message);
            continue;
        }
    }
    TRACE("Message loop finished\n");

    return 0;
}


typedef struct ALCmmdevPlayback {
    DERIVE_FROM_TYPE(ALCbackend);
    DERIVE_FROM_TYPE(ALCmmdevProxy);

    WCHAR *devid;

    IMMDevice *mmdev;
    IAudioClient *client;
    IAudioRenderClient *render;
    HANDLE NotifyEvent;

    HANDLE MsgEvent;

    volatile UINT32 Padding;

    volatile int killNow;
    althrd_t thread;
} ALCmmdevPlayback;

static int ALCmmdevPlayback_mixerProc(void *arg);

static void ALCmmdevPlayback_Construct(ALCmmdevPlayback *self, ALCdevice *device);
static void ALCmmdevPlayback_Destruct(ALCmmdevPlayback *self);
static ALCenum ALCmmdevPlayback_open(ALCmmdevPlayback *self, const ALCchar *name);
static HRESULT ALCmmdevPlayback_openProxy(ALCmmdevPlayback *self);
static void ALCmmdevPlayback_close(ALCmmdevPlayback *self);
static void ALCmmdevPlayback_closeProxy(ALCmmdevPlayback *self);
static ALCboolean ALCmmdevPlayback_reset(ALCmmdevPlayback *self);
static HRESULT ALCmmdevPlayback_resetProxy(ALCmmdevPlayback *self);
static ALCboolean ALCmmdevPlayback_start(ALCmmdevPlayback *self);
static HRESULT ALCmmdevPlayback_startProxy(ALCmmdevPlayback *self);
static void ALCmmdevPlayback_stop(ALCmmdevPlayback *self);
static void ALCmmdevPlayback_stopProxy(ALCmmdevPlayback *self);
static DECLARE_FORWARD2(ALCmmdevPlayback, ALCbackend, ALCenum, captureSamples, ALCvoid*, ALCuint)
static DECLARE_FORWARD(ALCmmdevPlayback, ALCbackend, ALCuint, availableSamples)
static ALint64 ALCmmdevPlayback_getLatency(ALCmmdevPlayback *self);
static DECLARE_FORWARD(ALCmmdevPlayback, ALCbackend, void, lock)
static DECLARE_FORWARD(ALCmmdevPlayback, ALCbackend, void, unlock)
DECLARE_DEFAULT_ALLOCATORS(ALCmmdevPlayback)

DEFINE_ALCMMDEVPROXY_VTABLE(ALCmmdevPlayback);
DEFINE_ALCBACKEND_VTABLE(ALCmmdevPlayback);


static void ALCmmdevPlayback_Construct(ALCmmdevPlayback *self, ALCdevice *device)
{
    SET_VTABLE2(ALCmmdevPlayback, ALCbackend, self);
    SET_VTABLE2(ALCmmdevPlayback, ALCmmdevProxy, self);
    ALCbackend_Construct(STATIC_CAST(ALCbackend, self), device);
    ALCmmdevProxy_Construct(STATIC_CAST(ALCmmdevProxy, self));

    self->devid = NULL;

    self->mmdev = NULL;
    self->client = NULL;
    self->render = NULL;
    self->NotifyEvent = NULL;

    self->MsgEvent = NULL;

    self->Padding = 0;

    self->killNow = 0;
}

static void ALCmmdevPlayback_Destruct(ALCmmdevPlayback *self)
{
    if(self->NotifyEvent != NULL)
        CloseHandle(self->NotifyEvent);
    self->NotifyEvent = NULL;
    if(self->MsgEvent != NULL)
        CloseHandle(self->MsgEvent);
    self->MsgEvent = NULL;

    free(self->devid);
    self->devid = NULL;

    ALCmmdevProxy_Destruct(STATIC_CAST(ALCmmdevProxy, self));
    ALCbackend_Destruct(STATIC_CAST(ALCbackend, self));
}


FORCE_ALIGN static int ALCmmdevPlayback_mixerProc(void *arg)
{
    ALCmmdevPlayback *self = arg;
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    UINT32 buffer_len, written;
    ALuint update_size, len;
    BYTE *buffer;
    HRESULT hr;

    hr = CoInitialize(NULL);
    if(FAILED(hr))
    {
        ERR("CoInitialize(NULL) failed: 0x%08lx\n", hr);
        V0(device->Backend,lock)();
        aluHandleDisconnect(device);
        V0(device->Backend,unlock)();
        return 1;
    }

    SetRTPriority();
    althrd_setname(althrd_current(), MIXER_THREAD_NAME);

    update_size = device->UpdateSize;
    buffer_len = update_size * device->NumUpdates;
    while(!self->killNow)
    {
        hr = IAudioClient_GetCurrentPadding(self->client, &written);
        if(FAILED(hr))
        {
            ERR("Failed to get padding: 0x%08lx\n", hr);
            V0(device->Backend,lock)();
            aluHandleDisconnect(device);
            V0(device->Backend,unlock)();
            break;
        }
        self->Padding = written;

        len = buffer_len - written;
        if(len < update_size)
        {
            DWORD res;
            res = WaitForSingleObjectEx(self->NotifyEvent, 2000, FALSE);
            if(res != WAIT_OBJECT_0)
                ERR("WaitForSingleObjectEx error: 0x%lx\n", res);
            continue;
        }
        len -= len%update_size;

        hr = IAudioRenderClient_GetBuffer(self->render, len, &buffer);
        if(SUCCEEDED(hr))
        {
            V0(device->Backend,lock)();
            aluMixData(device, buffer, len);
            self->Padding = written + len;
            V0(device->Backend,unlock)();
            hr = IAudioRenderClient_ReleaseBuffer(self->render, len, 0);
        }
        if(FAILED(hr))
        {
            ERR("Failed to buffer data: 0x%08lx\n", hr);
            V0(device->Backend,lock)();
            aluHandleDisconnect(device);
            V0(device->Backend,unlock)();
            break;
        }
    }
    self->Padding = 0;

    CoUninitialize();
    return 0;
}


static ALCboolean MakeExtensible(WAVEFORMATEXTENSIBLE *out, const WAVEFORMATEX *in)
{
    memset(out, 0, sizeof(*out));
    if(in->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
        *out = *(const WAVEFORMATEXTENSIBLE*)in;
    else if(in->wFormatTag == WAVE_FORMAT_PCM)
    {
        out->Format = *in;
        out->Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        out->Format.cbSize = sizeof(*out) - sizeof(*in);
        if(out->Format.nChannels == 1)
            out->dwChannelMask = MONO;
        else if(out->Format.nChannels == 2)
            out->dwChannelMask = STEREO;
        else
            ERR("Unhandled PCM channel count: %d\n", out->Format.nChannels);
        out->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    }
    else if(in->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
    {
        out->Format = *in;
        out->Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        out->Format.cbSize = sizeof(*out) - sizeof(*in);
        if(out->Format.nChannels == 1)
            out->dwChannelMask = MONO;
        else if(out->Format.nChannels == 2)
            out->dwChannelMask = STEREO;
        else
            ERR("Unhandled IEEE float channel count: %d\n", out->Format.nChannels);
        out->SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    }
    else
    {
        ERR("Unhandled format tag: 0x%04x\n", in->wFormatTag);
        return ALC_FALSE;
    }
    return ALC_TRUE;
}


static ALCenum ALCmmdevPlayback_open(ALCmmdevPlayback *self, const ALCchar *deviceName)
{
    HRESULT hr = S_OK;

    self->NotifyEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    self->MsgEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if(self->NotifyEvent == NULL || self->MsgEvent == NULL)
    {
        ERR("Failed to create message events: %lu\n", GetLastError());
        hr = E_FAIL;
    }

    if(SUCCEEDED(hr))
    {
        if(deviceName)
        {
            const DevMap *iter, *end;

            if(VECTOR_SIZE(PlaybackDevices) == 0)
            {
                ThreadRequest req = { self->MsgEvent, 0 };
                if(PostThreadMessage(ThreadID, WM_USER_Enumerate, (WPARAM)&req, ALL_DEVICE_PROBE))
                    (void)WaitForResponse(&req);
            }

            hr = E_FAIL;
            iter = VECTOR_ITER_BEGIN(PlaybackDevices);
            end = VECTOR_ITER_END(PlaybackDevices);
            for(;iter != end;iter++)
            {
                if(al_string_cmp_cstr(iter->name, deviceName) == 0)
                {
                    self->devid = strdupW(iter->devid);
                    hr = S_OK;
                    break;
                }
            }
            if(FAILED(hr))
                WARN("Failed to find device name matching \"%s\"\n", deviceName);
        }
    }

    if(SUCCEEDED(hr))
    {
        ThreadRequest req = { self->MsgEvent, 0 };

        hr = E_FAIL;
        if(PostThreadMessage(ThreadID, WM_USER_OpenDevice, (WPARAM)&req, (LPARAM)STATIC_CAST(ALCmmdevProxy, self)))
            hr = WaitForResponse(&req);
        else
            ERR("Failed to post thread message: %lu\n", GetLastError());
    }

    if(FAILED(hr))
    {
        if(self->NotifyEvent != NULL)
            CloseHandle(self->NotifyEvent);
        self->NotifyEvent = NULL;
        if(self->MsgEvent != NULL)
            CloseHandle(self->MsgEvent);
        self->MsgEvent = NULL;

        free(self->devid);
        self->devid = NULL;

        ERR("Device init failed: 0x%08lx\n", hr);
        return ALC_INVALID_VALUE;
    }

    return ALC_NO_ERROR;
}

static HRESULT ALCmmdevPlayback_openProxy(ALCmmdevPlayback *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    void *ptr;
    HRESULT hr;

    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_INPROC_SERVER, &IID_IMMDeviceEnumerator, &ptr);
    if(SUCCEEDED(hr))
    {
        IMMDeviceEnumerator *Enumerator = ptr;
        if(!self->devid)
            hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(Enumerator, eRender, eMultimedia, &self->mmdev);
        else
            hr = IMMDeviceEnumerator_GetDevice(Enumerator, self->devid, &self->mmdev);
        IMMDeviceEnumerator_Release(Enumerator);
        Enumerator = NULL;
    }
    if(SUCCEEDED(hr))
        hr = IMMDevice_Activate(self->mmdev, &IID_IAudioClient, CLSCTX_INPROC_SERVER, NULL, &ptr);
    if(SUCCEEDED(hr))
    {
        self->client = ptr;
        get_device_name(self->mmdev, &device->DeviceName);
    }

    if(FAILED(hr))
    {
        if(self->mmdev)
            IMMDevice_Release(self->mmdev);
        self->mmdev = NULL;
    }

    return hr;
}


static void ALCmmdevPlayback_close(ALCmmdevPlayback *self)
{
    ThreadRequest req = { self->MsgEvent, 0 };

    if(PostThreadMessage(ThreadID, WM_USER_CloseDevice, (WPARAM)&req, (LPARAM)STATIC_CAST(ALCmmdevProxy, self)))
        (void)WaitForResponse(&req);

    CloseHandle(self->MsgEvent);
    self->MsgEvent = NULL;

    CloseHandle(self->NotifyEvent);
    self->NotifyEvent = NULL;

    free(self->devid);
    self->devid = NULL;
}

static void ALCmmdevPlayback_closeProxy(ALCmmdevPlayback *self)
{
    if(self->client)
        IAudioClient_Release(self->client);
    self->client = NULL;

    if(self->mmdev)
        IMMDevice_Release(self->mmdev);
    self->mmdev = NULL;
}


static ALCboolean ALCmmdevPlayback_reset(ALCmmdevPlayback *self)
{
    ThreadRequest req = { self->MsgEvent, 0 };
    HRESULT hr = E_FAIL;

    if(PostThreadMessage(ThreadID, WM_USER_ResetDevice, (WPARAM)&req, (LPARAM)STATIC_CAST(ALCmmdevProxy, self)))
        hr = WaitForResponse(&req);

    return SUCCEEDED(hr) ? ALC_TRUE : ALC_FALSE;
}

static HRESULT ALCmmdevPlayback_resetProxy(ALCmmdevPlayback *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    EndpointFormFactor formfactor = UnknownFormFactor;
    WAVEFORMATEXTENSIBLE OutputType;
    WAVEFORMATEX *wfx = NULL;
    REFERENCE_TIME min_per, buf_time;
    UINT32 buffer_len, min_len;
    void *ptr = NULL;
    HRESULT hr;

    if(self->client)
        IAudioClient_Release(self->client);
    self->client = NULL;

    hr = IMMDevice_Activate(self->mmdev, &IID_IAudioClient, CLSCTX_INPROC_SERVER, NULL, &ptr);
    if(FAILED(hr))
    {
        ERR("Failed to reactivate audio client: 0x%08lx\n", hr);
        return hr;
    }
    self->client = ptr;

    hr = IAudioClient_GetMixFormat(self->client, &wfx);
    if(FAILED(hr))
    {
        ERR("Failed to get mix format: 0x%08lx\n", hr);
        return hr;
    }

    if(!MakeExtensible(&OutputType, wfx))
    {
        CoTaskMemFree(wfx);
        return E_FAIL;
    }
    CoTaskMemFree(wfx);
    wfx = NULL;

    buf_time = ((REFERENCE_TIME)device->UpdateSize*device->NumUpdates*10000000 +
                                device->Frequency-1) / device->Frequency;

    if(!(device->Flags&DEVICE_FREQUENCY_REQUEST))
        device->Frequency = OutputType.Format.nSamplesPerSec;
    if(!(device->Flags&DEVICE_CHANNELS_REQUEST))
    {
        if(OutputType.Format.nChannels == 1 && OutputType.dwChannelMask == MONO)
            device->FmtChans = DevFmtMono;
        else if(OutputType.Format.nChannels == 2 && OutputType.dwChannelMask == STEREO)
            device->FmtChans = DevFmtStereo;
        else if(OutputType.Format.nChannels == 4 && OutputType.dwChannelMask == QUAD)
            device->FmtChans = DevFmtQuad;
        else if(OutputType.Format.nChannels == 6 && OutputType.dwChannelMask == X5DOT1)
            device->FmtChans = DevFmtX51;
        else if(OutputType.Format.nChannels == 6 && OutputType.dwChannelMask == X5DOT1REAR)
            device->FmtChans = DevFmtX51Rear;
        else if(OutputType.Format.nChannels == 7 && OutputType.dwChannelMask == X6DOT1)
            device->FmtChans = DevFmtX61;
        else if(OutputType.Format.nChannels == 8 && (OutputType.dwChannelMask == X7DOT1 || OutputType.dwChannelMask == X7DOT1_WIDE))
            device->FmtChans = DevFmtX71;
        else
            ERR("Unhandled channel config: %d -- 0x%08lx\n", OutputType.Format.nChannels, OutputType.dwChannelMask);
    }

    switch(device->FmtChans)
    {
        case DevFmtMono:
            OutputType.Format.nChannels = 1;
            OutputType.dwChannelMask = MONO;
            break;
        case DevFmtBFormat3D:
            device->FmtChans = DevFmtStereo;
            /*fall-through*/
        case DevFmtStereo:
            OutputType.Format.nChannels = 2;
            OutputType.dwChannelMask = STEREO;
            break;
        case DevFmtQuad:
            OutputType.Format.nChannels = 4;
            OutputType.dwChannelMask = QUAD;
            break;
        case DevFmtX51:
            OutputType.Format.nChannels = 6;
            OutputType.dwChannelMask = X5DOT1;
            break;
        case DevFmtX51Rear:
            OutputType.Format.nChannels = 6;
            OutputType.dwChannelMask = X5DOT1REAR;
            break;
        case DevFmtX61:
            OutputType.Format.nChannels = 7;
            OutputType.dwChannelMask = X6DOT1;
            break;
        case DevFmtX71:
            OutputType.Format.nChannels = 8;
            OutputType.dwChannelMask = X7DOT1;
            break;
    }
    switch(device->FmtType)
    {
        case DevFmtByte:
            device->FmtType = DevFmtUByte;
            /* fall-through */
        case DevFmtUByte:
            OutputType.Format.wBitsPerSample = 8;
            OutputType.Samples.wValidBitsPerSample = 8;
            OutputType.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
            break;
        case DevFmtUShort:
            device->FmtType = DevFmtShort;
            /* fall-through */
        case DevFmtShort:
            OutputType.Format.wBitsPerSample = 16;
            OutputType.Samples.wValidBitsPerSample = 16;
            OutputType.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
            break;
        case DevFmtUInt:
            device->FmtType = DevFmtInt;
            /* fall-through */
        case DevFmtInt:
            OutputType.Format.wBitsPerSample = 32;
            OutputType.Samples.wValidBitsPerSample = 32;
            OutputType.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
            break;
        case DevFmtFloat:
            OutputType.Format.wBitsPerSample = 32;
            OutputType.Samples.wValidBitsPerSample = 32;
            OutputType.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
            break;
    }
    OutputType.Format.nSamplesPerSec = device->Frequency;

    OutputType.Format.nBlockAlign = OutputType.Format.nChannels *
                                    OutputType.Format.wBitsPerSample / 8;
    OutputType.Format.nAvgBytesPerSec = OutputType.Format.nSamplesPerSec *
                                        OutputType.Format.nBlockAlign;

    hr = IAudioClient_IsFormatSupported(self->client, AUDCLNT_SHAREMODE_SHARED, &OutputType.Format, &wfx);
    if(FAILED(hr))
    {
        ERR("Failed to check format support: 0x%08lx\n", hr);
        hr = IAudioClient_GetMixFormat(self->client, &wfx);
    }
    if(FAILED(hr))
    {
        ERR("Failed to find a supported format: 0x%08lx\n", hr);
        return hr;
    }

    if(wfx != NULL)
    {
        if(!MakeExtensible(&OutputType, wfx))
        {
            CoTaskMemFree(wfx);
            return E_FAIL;
        }
        CoTaskMemFree(wfx);
        wfx = NULL;

        device->Frequency = OutputType.Format.nSamplesPerSec;
        if(OutputType.Format.nChannels == 1 && OutputType.dwChannelMask == MONO)
            device->FmtChans = DevFmtMono;
        else if(OutputType.Format.nChannels == 2 && OutputType.dwChannelMask == STEREO)
            device->FmtChans = DevFmtStereo;
        else if(OutputType.Format.nChannels == 4 && OutputType.dwChannelMask == QUAD)
            device->FmtChans = DevFmtQuad;
        else if(OutputType.Format.nChannels == 6 && OutputType.dwChannelMask == X5DOT1)
            device->FmtChans = DevFmtX51;
        else if(OutputType.Format.nChannels == 6 && OutputType.dwChannelMask == X5DOT1REAR)
            device->FmtChans = DevFmtX51Rear;
        else if(OutputType.Format.nChannels == 7 && OutputType.dwChannelMask == X6DOT1)
            device->FmtChans = DevFmtX61;
        else if(OutputType.Format.nChannels == 8 && (OutputType.dwChannelMask == X7DOT1 || OutputType.dwChannelMask == X7DOT1_WIDE))
            device->FmtChans = DevFmtX71;
        else
        {
            ERR("Unhandled extensible channels: %d -- 0x%08lx\n", OutputType.Format.nChannels, OutputType.dwChannelMask);
            device->FmtChans = DevFmtStereo;
            OutputType.Format.nChannels = 2;
            OutputType.dwChannelMask = STEREO;
        }

        if(IsEqualGUID(&OutputType.SubFormat, &KSDATAFORMAT_SUBTYPE_PCM))
        {
            if(OutputType.Format.wBitsPerSample == 8)
                device->FmtType = DevFmtUByte;
            else if(OutputType.Format.wBitsPerSample == 16)
                device->FmtType = DevFmtShort;
            else if(OutputType.Format.wBitsPerSample == 32)
                device->FmtType = DevFmtInt;
            else
            {
                device->FmtType = DevFmtShort;
                OutputType.Format.wBitsPerSample = 16;
            }
        }
        else if(IsEqualGUID(&OutputType.SubFormat, &KSDATAFORMAT_SUBTYPE_IEEE_FLOAT))
        {
            device->FmtType = DevFmtFloat;
            OutputType.Format.wBitsPerSample = 32;
        }
        else
        {
            ERR("Unhandled format sub-type\n");
            device->FmtType = DevFmtShort;
            OutputType.Format.wBitsPerSample = 16;
            OutputType.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
        }
        OutputType.Samples.wValidBitsPerSample = OutputType.Format.wBitsPerSample;
    }
    get_device_formfactor(self->mmdev, &formfactor);
    device->IsHeadphones = (device->FmtChans == DevFmtStereo && formfactor == Headphones);

    SetDefaultWFXChannelOrder(device);

    hr = IAudioClient_Initialize(self->client, AUDCLNT_SHAREMODE_SHARED,
                                 AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                 buf_time, 0, &OutputType.Format, NULL);
    if(FAILED(hr))
    {
        ERR("Failed to initialize audio client: 0x%08lx\n", hr);
        return hr;
    }

    hr = IAudioClient_GetDevicePeriod(self->client, &min_per, NULL);
    if(SUCCEEDED(hr))
    {
        min_len = (UINT32)((min_per*device->Frequency + 10000000-1) / 10000000);
        /* Find the nearest multiple of the period size to the update size */
        if(min_len < device->UpdateSize)
            min_len *= (device->UpdateSize + min_len/2)/min_len;
        hr = IAudioClient_GetBufferSize(self->client, &buffer_len);
    }
    if(FAILED(hr))
    {
        ERR("Failed to get audio buffer info: 0x%08lx\n", hr);
        return hr;
    }

    device->UpdateSize = min_len;
    device->NumUpdates = buffer_len / device->UpdateSize;
    if(device->NumUpdates <= 1)
    {
        ERR("Audio client returned buffer_len < period*2; expect break up\n");
        device->NumUpdates = 2;
        device->UpdateSize = buffer_len / device->NumUpdates;
    }

    hr = IAudioClient_SetEventHandle(self->client, self->NotifyEvent);
    if(FAILED(hr))
    {
        ERR("Failed to set event handle: 0x%08lx\n", hr);
        return hr;
    }

    return hr;
}


static ALCboolean ALCmmdevPlayback_start(ALCmmdevPlayback *self)
{
    ThreadRequest req = { self->MsgEvent, 0 };
    HRESULT hr = E_FAIL;

    if(PostThreadMessage(ThreadID, WM_USER_StartDevice, (WPARAM)&req, (LPARAM)STATIC_CAST(ALCmmdevProxy, self)))
        hr = WaitForResponse(&req);

    return SUCCEEDED(hr) ? ALC_TRUE : ALC_FALSE;
}

static HRESULT ALCmmdevPlayback_startProxy(ALCmmdevPlayback *self)
{
    HRESULT hr;
    void *ptr;

    ResetEvent(self->NotifyEvent);
    hr = IAudioClient_Start(self->client);
    if(FAILED(hr))
        ERR("Failed to start audio client: 0x%08lx\n", hr);

    if(SUCCEEDED(hr))
        hr = IAudioClient_GetService(self->client, &IID_IAudioRenderClient, &ptr);
    if(SUCCEEDED(hr))
    {
        self->render = ptr;
        self->killNow = 0;
        if(althrd_create(&self->thread, ALCmmdevPlayback_mixerProc, self) != althrd_success)
        {
            if(self->render)
                IAudioRenderClient_Release(self->render);
            self->render = NULL;
            IAudioClient_Stop(self->client);
            ERR("Failed to start thread\n");
            hr = E_FAIL;
        }
    }

    return hr;
}


static void ALCmmdevPlayback_stop(ALCmmdevPlayback *self)
{
    ThreadRequest req = { self->MsgEvent, 0 };
    if(PostThreadMessage(ThreadID, WM_USER_StopDevice, (WPARAM)&req, (LPARAM)STATIC_CAST(ALCmmdevProxy, self)))
        (void)WaitForResponse(&req);
}

static void ALCmmdevPlayback_stopProxy(ALCmmdevPlayback *self)
{
    int res;

    if(!self->render)
        return;

    self->killNow = 1;
    althrd_join(self->thread, &res);

    IAudioRenderClient_Release(self->render);
    self->render = NULL;
    IAudioClient_Stop(self->client);
}


static ALint64 ALCmmdevPlayback_getLatency(ALCmmdevPlayback *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    return (ALint64)self->Padding * 1000000000 / device->Frequency;
}


typedef struct ALCmmdevCapture {
    DERIVE_FROM_TYPE(ALCbackend);
    DERIVE_FROM_TYPE(ALCmmdevProxy);

    WCHAR *devid;

    IMMDevice *mmdev;
    IAudioClient *client;
    IAudioCaptureClient *capture;
    HANDLE NotifyEvent;

    HANDLE MsgEvent;

    RingBuffer *Ring;

    volatile int killNow;
    althrd_t thread;
} ALCmmdevCapture;

static int ALCmmdevCapture_recordProc(void *arg);

static void ALCmmdevCapture_Construct(ALCmmdevCapture *self, ALCdevice *device);
static void ALCmmdevCapture_Destruct(ALCmmdevCapture *self);
static ALCenum ALCmmdevCapture_open(ALCmmdevCapture *self, const ALCchar *name);
static HRESULT ALCmmdevCapture_openProxy(ALCmmdevCapture *self);
static void ALCmmdevCapture_close(ALCmmdevCapture *self);
static void ALCmmdevCapture_closeProxy(ALCmmdevCapture *self);
static DECLARE_FORWARD(ALCmmdevCapture, ALCbackend, ALCboolean, reset)
static HRESULT ALCmmdevCapture_resetProxy(ALCmmdevCapture *self);
static ALCboolean ALCmmdevCapture_start(ALCmmdevCapture *self);
static HRESULT ALCmmdevCapture_startProxy(ALCmmdevCapture *self);
static void ALCmmdevCapture_stop(ALCmmdevCapture *self);
static void ALCmmdevCapture_stopProxy(ALCmmdevCapture *self);
static ALCenum ALCmmdevCapture_captureSamples(ALCmmdevCapture *self, ALCvoid *buffer, ALCuint samples);
static ALuint ALCmmdevCapture_availableSamples(ALCmmdevCapture *self);
static DECLARE_FORWARD(ALCmmdevCapture, ALCbackend, ALint64, getLatency)
static DECLARE_FORWARD(ALCmmdevCapture, ALCbackend, void, lock)
static DECLARE_FORWARD(ALCmmdevCapture, ALCbackend, void, unlock)
DECLARE_DEFAULT_ALLOCATORS(ALCmmdevCapture)

DEFINE_ALCMMDEVPROXY_VTABLE(ALCmmdevCapture);
DEFINE_ALCBACKEND_VTABLE(ALCmmdevCapture);


static void ALCmmdevCapture_Construct(ALCmmdevCapture *self, ALCdevice *device)
{
    SET_VTABLE2(ALCmmdevCapture, ALCbackend, self);
    SET_VTABLE2(ALCmmdevCapture, ALCmmdevProxy, self);
    ALCbackend_Construct(STATIC_CAST(ALCbackend, self), device);
    ALCmmdevProxy_Construct(STATIC_CAST(ALCmmdevProxy, self));

    self->devid = NULL;

    self->mmdev = NULL;
    self->client = NULL;
    self->capture = NULL;
    self->NotifyEvent = NULL;

    self->MsgEvent = NULL;

    self->Ring = NULL;

    self->killNow = 0;
}

static void ALCmmdevCapture_Destruct(ALCmmdevCapture *self)
{
    DestroyRingBuffer(self->Ring);
    self->Ring = NULL;

    if(self->NotifyEvent != NULL)
        CloseHandle(self->NotifyEvent);
    self->NotifyEvent = NULL;
    if(self->MsgEvent != NULL)
        CloseHandle(self->MsgEvent);
    self->MsgEvent = NULL;

    free(self->devid);
    self->devid = NULL;

    ALCmmdevProxy_Destruct(STATIC_CAST(ALCmmdevProxy, self));
    ALCbackend_Destruct(STATIC_CAST(ALCbackend, self));
}


FORCE_ALIGN int ALCmmdevCapture_recordProc(void *arg)
{
    ALCmmdevCapture *self = arg;
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    HRESULT hr;

    hr = CoInitialize(NULL);
    if(FAILED(hr))
    {
        ERR("CoInitialize(NULL) failed: 0x%08lx\n", hr);
        V0(device->Backend,lock)();
        aluHandleDisconnect(device);
        V0(device->Backend,unlock)();
        return 1;
    }

    althrd_setname(althrd_current(), RECORD_THREAD_NAME);

    while(!self->killNow)
    {
        UINT32 avail;
        DWORD res;

        hr = IAudioCaptureClient_GetNextPacketSize(self->capture, &avail);
        if(FAILED(hr))
            ERR("Failed to get next packet size: 0x%08lx\n", hr);
        else while(avail > 0 && SUCCEEDED(hr))
        {
            UINT32 numsamples;
            DWORD flags;
            BYTE *data;

            hr = IAudioCaptureClient_GetBuffer(self->capture,
                &data, &numsamples, &flags, NULL, NULL
            );
            if(FAILED(hr))
            {
                ERR("Failed to get capture buffer: 0x%08lx\n", hr);
                break;
            }

            WriteRingBuffer(self->Ring, data, numsamples);

            hr = IAudioCaptureClient_ReleaseBuffer(self->capture, numsamples);
            if(FAILED(hr))
            {
                ERR("Failed to release capture buffer: 0x%08lx\n", hr);
                break;
            }

            hr = IAudioCaptureClient_GetNextPacketSize(self->capture, &avail);
            if(FAILED(hr))
                ERR("Failed to get next packet size: 0x%08lx\n", hr);
        }

        if(FAILED(hr))
        {
            V0(device->Backend,lock)();
            aluHandleDisconnect(device);
            V0(device->Backend,unlock)();
            break;
        }

        res = WaitForSingleObjectEx(self->NotifyEvent, 2000, FALSE);
        if(res != WAIT_OBJECT_0)
            ERR("WaitForSingleObjectEx error: 0x%lx\n", res);
    }

    CoUninitialize();
    return 0;
}


static ALCenum ALCmmdevCapture_open(ALCmmdevCapture *self, const ALCchar *deviceName)
{
    HRESULT hr = S_OK;

    self->NotifyEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    self->MsgEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if(self->NotifyEvent == NULL || self->MsgEvent == NULL)
    {
        ERR("Failed to create message events: %lu\n", GetLastError());
        hr = E_FAIL;
    }

    if(SUCCEEDED(hr))
    {
        if(deviceName)
        {
            const DevMap *iter;

            if(VECTOR_SIZE(CaptureDevices) == 0)
            {
                ThreadRequest req = { self->MsgEvent, 0 };
                if(PostThreadMessage(ThreadID, WM_USER_Enumerate, (WPARAM)&req, CAPTURE_DEVICE_PROBE))
                    (void)WaitForResponse(&req);
            }

            hr = E_FAIL;
#define MATCH_NAME(i) (al_string_cmp_cstr((i)->name, deviceName) == 0)
            VECTOR_FIND_IF(iter, const DevMap, CaptureDevices, MATCH_NAME);
            if(iter == VECTOR_ITER_END(CaptureDevices))
                WARN("Failed to find device name matching \"%s\"\n", deviceName);
            else
            {
                self->devid = strdupW(iter->devid);
                hr = S_OK;
            }
#undef MATCH_NAME
        }
    }

    if(SUCCEEDED(hr))
    {
        ThreadRequest req = { self->MsgEvent, 0 };

        hr = E_FAIL;
        if(PostThreadMessage(ThreadID, WM_USER_OpenDevice, (WPARAM)&req, (LPARAM)STATIC_CAST(ALCmmdevProxy, self)))
            hr = WaitForResponse(&req);
        else
            ERR("Failed to post thread message: %lu\n", GetLastError());
    }

    if(FAILED(hr))
    {
        if(self->NotifyEvent != NULL)
            CloseHandle(self->NotifyEvent);
        self->NotifyEvent = NULL;
        if(self->MsgEvent != NULL)
            CloseHandle(self->MsgEvent);
        self->MsgEvent = NULL;

        free(self->devid);
        self->devid = NULL;

        ERR("Device init failed: 0x%08lx\n", hr);
        return ALC_INVALID_VALUE;
    }
    else
    {
        ThreadRequest req = { self->MsgEvent, 0 };

        hr = E_FAIL;
        if(PostThreadMessage(ThreadID, WM_USER_ResetDevice, (WPARAM)&req, (LPARAM)STATIC_CAST(ALCmmdevProxy, self)))
            hr = WaitForResponse(&req);
        else
            ERR("Failed to post thread message: %lu\n", GetLastError());

        if(FAILED(hr))
        {
            ALCmmdevCapture_close(self);
            if(hr == E_OUTOFMEMORY)
               return ALC_OUT_OF_MEMORY;
            return ALC_INVALID_VALUE;
        }
    }

    return ALC_NO_ERROR;
}

static HRESULT ALCmmdevCapture_openProxy(ALCmmdevCapture *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    void *ptr;
    HRESULT hr;

    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_INPROC_SERVER, &IID_IMMDeviceEnumerator, &ptr);
    if(SUCCEEDED(hr))
    {
        IMMDeviceEnumerator *Enumerator = ptr;
        if(!self->devid)
            hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(Enumerator, eCapture, eMultimedia, &self->mmdev);
        else
            hr = IMMDeviceEnumerator_GetDevice(Enumerator, self->devid, &self->mmdev);
        IMMDeviceEnumerator_Release(Enumerator);
        Enumerator = NULL;
    }
    if(SUCCEEDED(hr))
        hr = IMMDevice_Activate(self->mmdev, &IID_IAudioClient, CLSCTX_INPROC_SERVER, NULL, &ptr);
    if(SUCCEEDED(hr))
    {
        self->client = ptr;
        get_device_name(self->mmdev, &device->DeviceName);
    }

    if(FAILED(hr))
    {
        if(self->mmdev)
            IMMDevice_Release(self->mmdev);
        self->mmdev = NULL;
    }

    return hr;
}


static void ALCmmdevCapture_close(ALCmmdevCapture *self)
{
    ThreadRequest req = { self->MsgEvent, 0 };

    if(PostThreadMessage(ThreadID, WM_USER_CloseDevice, (WPARAM)&req, (LPARAM)STATIC_CAST(ALCmmdevProxy, self)))
        (void)WaitForResponse(&req);

    DestroyRingBuffer(self->Ring);
    self->Ring = NULL;

    CloseHandle(self->MsgEvent);
    self->MsgEvent = NULL;

    CloseHandle(self->NotifyEvent);
    self->NotifyEvent = NULL;

    free(self->devid);
    self->devid = NULL;
}

static void ALCmmdevCapture_closeProxy(ALCmmdevCapture *self)
{
    if(self->client)
        IAudioClient_Release(self->client);
    self->client = NULL;

    if(self->mmdev)
        IMMDevice_Release(self->mmdev);
    self->mmdev = NULL;
}


static HRESULT ALCmmdevCapture_resetProxy(ALCmmdevCapture *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    WAVEFORMATEXTENSIBLE OutputType;
    REFERENCE_TIME buf_time;
    UINT32 buffer_len;
    void *ptr = NULL;
    HRESULT hr;

    if(self->client)
        IAudioClient_Release(self->client);
    self->client = NULL;

    hr = IMMDevice_Activate(self->mmdev, &IID_IAudioClient, CLSCTX_INPROC_SERVER, NULL, &ptr);
    if(FAILED(hr))
    {
        ERR("Failed to reactivate audio client: 0x%08lx\n", hr);
        return hr;
    }
    self->client = ptr;

    buf_time = ((REFERENCE_TIME)device->UpdateSize*device->NumUpdates*10000000 +
                                device->Frequency-1) / device->Frequency;

    OutputType.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    switch(device->FmtChans)
    {
        case DevFmtMono:
            OutputType.Format.nChannels = 1;
            OutputType.dwChannelMask = MONO;
            break;
        case DevFmtStereo:
            OutputType.Format.nChannels = 2;
            OutputType.dwChannelMask = STEREO;
            break;
        case DevFmtQuad:
            OutputType.Format.nChannels = 4;
            OutputType.dwChannelMask = QUAD;
            break;
        case DevFmtX51:
            OutputType.Format.nChannels = 6;
            OutputType.dwChannelMask = X5DOT1;
            break;
        case DevFmtX51Rear:
            OutputType.Format.nChannels = 6;
            OutputType.dwChannelMask = X5DOT1REAR;
            break;
        case DevFmtX61:
            OutputType.Format.nChannels = 7;
            OutputType.dwChannelMask = X6DOT1;
            break;
        case DevFmtX71:
            OutputType.Format.nChannels = 8;
            OutputType.dwChannelMask = X7DOT1;
            break;

        case DevFmtBFormat3D:
            return E_FAIL;
    }
    switch(device->FmtType)
    {
        case DevFmtUByte:
            OutputType.Format.wBitsPerSample = 8;
            OutputType.Samples.wValidBitsPerSample = 8;
            OutputType.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
            break;
        case DevFmtShort:
            OutputType.Format.wBitsPerSample = 16;
            OutputType.Samples.wValidBitsPerSample = 16;
            OutputType.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
            break;
        case DevFmtInt:
            OutputType.Format.wBitsPerSample = 32;
            OutputType.Samples.wValidBitsPerSample = 32;
            OutputType.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
            break;
        case DevFmtFloat:
            OutputType.Format.wBitsPerSample = 32;
            OutputType.Samples.wValidBitsPerSample = 32;
            OutputType.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
            break;

        case DevFmtByte:
        case DevFmtUShort:
        case DevFmtUInt:
            WARN("%s capture samples not supported\n", DevFmtTypeString(device->FmtType));
            return E_FAIL;
    }
    OutputType.Format.nSamplesPerSec = device->Frequency;

    OutputType.Format.nBlockAlign = OutputType.Format.nChannels *
                                    OutputType.Format.wBitsPerSample / 8;
    OutputType.Format.nAvgBytesPerSec = OutputType.Format.nSamplesPerSec *
                                        OutputType.Format.nBlockAlign;

    hr = IAudioClient_IsFormatSupported(self->client,
        AUDCLNT_SHAREMODE_SHARED, &OutputType.Format, NULL
    );
    if(FAILED(hr))
    {
        ERR("Failed to check format support: 0x%08lx\n", hr);
        return hr;
    }

    hr = IAudioClient_Initialize(self->client,
        AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
        buf_time, 0, &OutputType.Format, NULL
    );
    if(FAILED(hr))
    {
        ERR("Failed to initialize audio client: 0x%08lx\n", hr);
        return hr;
    }

    hr = IAudioClient_GetBufferSize(self->client, &buffer_len);
    if(FAILED(hr))
    {
        ERR("Failed to get buffer size: 0x%08lx\n", hr);
        return hr;
    }

    buffer_len = maxu(device->UpdateSize*device->NumUpdates, buffer_len);
    self->Ring = CreateRingBuffer(OutputType.Format.nBlockAlign, buffer_len);
    if(!self->Ring)
    {
        ERR("Failed to allocate capture ring buffer\n");
        return E_OUTOFMEMORY;
    }

    hr = IAudioClient_SetEventHandle(self->client, self->NotifyEvent);
    if(FAILED(hr))
    {
        ERR("Failed to set event handle: 0x%08lx\n", hr);
        return hr;
    }

    return hr;
}


static ALCboolean ALCmmdevCapture_start(ALCmmdevCapture *self)
{
    ThreadRequest req = { self->MsgEvent, 0 };
    HRESULT hr = E_FAIL;

    if(PostThreadMessage(ThreadID, WM_USER_StartDevice, (WPARAM)&req, (LPARAM)STATIC_CAST(ALCmmdevProxy, self)))
        hr = WaitForResponse(&req);

    return SUCCEEDED(hr) ? ALC_TRUE : ALC_FALSE;
}

static HRESULT ALCmmdevCapture_startProxy(ALCmmdevCapture *self)
{
    HRESULT hr;
    void *ptr;

    ResetEvent(self->NotifyEvent);
    hr = IAudioClient_Start(self->client);
    if(FAILED(hr))
    {
        ERR("Failed to start audio client: 0x%08lx\n", hr);
        return hr;
    }

    hr = IAudioClient_GetService(self->client, &IID_IAudioCaptureClient, &ptr);
    if(SUCCEEDED(hr))
    {
        self->capture = ptr;
        self->killNow = 0;
        if(althrd_create(&self->thread, ALCmmdevCapture_recordProc, self) != althrd_success)
        {
            ERR("Failed to start thread\n");
            IAudioCaptureClient_Release(self->capture);
            self->capture = NULL;
            hr = E_FAIL;
        }
    }

    if(FAILED(hr))
    {
        IAudioClient_Stop(self->client);
        IAudioClient_Reset(self->client);
    }

    return hr;
}


static void ALCmmdevCapture_stop(ALCmmdevCapture *self)
{
    ThreadRequest req = { self->MsgEvent, 0 };
    if(PostThreadMessage(ThreadID, WM_USER_StopDevice, (WPARAM)&req, (LPARAM)STATIC_CAST(ALCmmdevProxy, self)))
        (void)WaitForResponse(&req);
}

static void ALCmmdevCapture_stopProxy(ALCmmdevCapture *self)
{
    int res;

    if(!self->capture)
        return;

    self->killNow = 1;
    althrd_join(self->thread, &res);

    IAudioCaptureClient_Release(self->capture);
    self->capture = NULL;
    IAudioClient_Stop(self->client);
    IAudioClient_Reset(self->client);
}


ALuint ALCmmdevCapture_availableSamples(ALCmmdevCapture *self)
{
    return RingBufferSize(self->Ring);
}

ALCenum ALCmmdevCapture_captureSamples(ALCmmdevCapture *self, ALCvoid *buffer, ALCuint samples)
{
    if(ALCmmdevCapture_availableSamples(self) < samples)
        return ALC_INVALID_VALUE;
    ReadRingBuffer(self->Ring, buffer, samples);
    return ALC_NO_ERROR;
}


static inline void AppendAllDevicesList2(const DevMap *entry)
{ AppendAllDevicesList(al_string_get_cstr(entry->name)); }
static inline void AppendCaptureDeviceList2(const DevMap *entry)
{ AppendCaptureDeviceList(al_string_get_cstr(entry->name)); }

typedef struct ALCmmdevBackendFactory {
    DERIVE_FROM_TYPE(ALCbackendFactory);
} ALCmmdevBackendFactory;
#define ALCMMDEVBACKENDFACTORY_INITIALIZER { { GET_VTABLE2(ALCmmdevBackendFactory, ALCbackendFactory) } }

static ALCboolean ALCmmdevBackendFactory_init(ALCmmdevBackendFactory *self);
static void ALCmmdevBackendFactory_deinit(ALCmmdevBackendFactory *self);
static ALCboolean ALCmmdevBackendFactory_querySupport(ALCmmdevBackendFactory *self, ALCbackend_Type type);
static void ALCmmdevBackendFactory_probe(ALCmmdevBackendFactory *self, enum DevProbe type);
static ALCbackend* ALCmmdevBackendFactory_createBackend(ALCmmdevBackendFactory *self, ALCdevice *device, ALCbackend_Type type);

DEFINE_ALCBACKENDFACTORY_VTABLE(ALCmmdevBackendFactory);


static BOOL MMDevApiLoad(void)
{
    static HRESULT InitResult;
    if(!ThreadHdl)
    {
        ThreadRequest req;
        InitResult = E_FAIL;

        req.FinishedEvt = CreateEvent(NULL, FALSE, FALSE, NULL);
        if(req.FinishedEvt == NULL)
            ERR("Failed to create event: %lu\n", GetLastError());
        else
        {
            ThreadHdl = CreateThread(NULL, 0, ALCmmdevProxy_messageHandler, &req, 0, &ThreadID);
            if(ThreadHdl != NULL)
                InitResult = WaitForResponse(&req);
            CloseHandle(req.FinishedEvt);
        }
    }
    return SUCCEEDED(InitResult);
}

static ALCboolean ALCmmdevBackendFactory_init(ALCmmdevBackendFactory* UNUSED(self))
{
    VECTOR_INIT(PlaybackDevices);
    VECTOR_INIT(CaptureDevices);

    if(!MMDevApiLoad())
        return ALC_FALSE;
    return ALC_TRUE;
}

static void ALCmmdevBackendFactory_deinit(ALCmmdevBackendFactory* UNUSED(self))
{
    clear_devlist(&PlaybackDevices);
    VECTOR_DEINIT(PlaybackDevices);

    clear_devlist(&CaptureDevices);
    VECTOR_DEINIT(CaptureDevices);

    if(ThreadHdl)
    {
        TRACE("Sending WM_QUIT to Thread %04lx\n", ThreadID);
        PostThreadMessage(ThreadID, WM_QUIT, 0, 0);
        CloseHandle(ThreadHdl);
        ThreadHdl = NULL;
    }
}

static ALCboolean ALCmmdevBackendFactory_querySupport(ALCmmdevBackendFactory* UNUSED(self), ALCbackend_Type type)
{
    if(type == ALCbackend_Playback/* || type == ALCbackend_Capture*/)
        return ALC_TRUE;
    return ALC_FALSE;
}

static void ALCmmdevBackendFactory_probe(ALCmmdevBackendFactory* UNUSED(self), enum DevProbe type)
{
    ThreadRequest req = { NULL, 0 };

    req.FinishedEvt = CreateEvent(NULL, FALSE, FALSE, NULL);
    if(req.FinishedEvt == NULL)
        ERR("Failed to create event: %lu\n", GetLastError());
    else
    {
        HRESULT hr = E_FAIL;
        if(PostThreadMessage(ThreadID, WM_USER_Enumerate, (WPARAM)&req, type))
            hr = WaitForResponse(&req);
        if(SUCCEEDED(hr)) switch(type)
        {
        case ALL_DEVICE_PROBE:
            VECTOR_FOR_EACH(const DevMap, PlaybackDevices, AppendAllDevicesList2);
            break;

        case CAPTURE_DEVICE_PROBE:
            VECTOR_FOR_EACH(const DevMap, CaptureDevices, AppendCaptureDeviceList2);
            break;
        }
        CloseHandle(req.FinishedEvt);
        req.FinishedEvt = NULL;
    }
}

static ALCbackend* ALCmmdevBackendFactory_createBackend(ALCmmdevBackendFactory* UNUSED(self), ALCdevice *device, ALCbackend_Type type)
{
    if(type == ALCbackend_Playback)
    {
        ALCmmdevPlayback *backend;

        backend = ALCmmdevPlayback_New(sizeof(*backend));
        if(!backend) return NULL;
        memset(backend, 0, sizeof(*backend));

        ALCmmdevPlayback_Construct(backend, device);

        return STATIC_CAST(ALCbackend, backend);
    }
    if(type == ALCbackend_Capture)
    {
        ALCmmdevCapture *backend;

        backend = ALCmmdevCapture_New(sizeof(*backend));
        if(!backend) return NULL;
        memset(backend, 0, sizeof(*backend));

        ALCmmdevCapture_Construct(backend, device);

        return STATIC_CAST(ALCbackend, backend);
    }

    return NULL;
}


ALCbackendFactory *ALCmmdevBackendFactory_getFactory(void)
{
    static ALCmmdevBackendFactory factory = ALCMMDEVBACKENDFACTORY_INITIALIZER;
    return STATIC_CAST(ALCbackendFactory, &factory);
}
