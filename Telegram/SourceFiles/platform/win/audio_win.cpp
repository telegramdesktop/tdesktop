/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/win/audio_win.h"

#include "platform/win/windows_dlls.h"
#include "media/media_audio.h"

#include <mmdeviceapi.h>
#include <audioclient.h>

#include <wrl\client.h>
using namespace Microsoft::WRL;

namespace Platform {
namespace Audio {
namespace {

// Inspired by Chromium.
class DeviceListener : public IMMNotificationClient {
public:
	DeviceListener() = default;
	DeviceListener(const DeviceListener &other) = delete;
	DeviceListener &operator=(const DeviceListener &other) = delete;
	virtual ~DeviceListener() = default;

private:
	// IMMNotificationClient implementation.
	STDMETHOD_(ULONG, AddRef)() override {
		return 1;
	}
	STDMETHOD_(ULONG, Release)() override {
		return 1;
	}
	STDMETHOD(QueryInterface)(REFIID iid, void** object) override;
	STDMETHOD(OnPropertyValueChanged)(LPCWSTR device_id, const PROPERTYKEY key) override;
	STDMETHOD(OnDeviceAdded)(LPCWSTR device_id) override {
		return S_OK;
	}
	STDMETHOD(OnDeviceRemoved)(LPCWSTR device_id) override {
		return S_OK;
	}
	STDMETHOD(OnDeviceStateChanged)(LPCWSTR device_id, DWORD new_state) override;
	STDMETHOD(OnDefaultDeviceChanged)(EDataFlow flow, ERole role, LPCWSTR new_default_device_id) override;

};

STDMETHODIMP DeviceListener::QueryInterface(REFIID iid, void** object) {
	if (iid == IID_IUnknown || iid == __uuidof(IMMNotificationClient)) {
		*object = static_cast<IMMNotificationClient*>(this);
		return S_OK;
	}

	*object = NULL;
	return E_NOINTERFACE;
}

STDMETHODIMP DeviceListener::OnPropertyValueChanged(LPCWSTR device_id, const PROPERTYKEY key) {
	auto deviceName = device_id ? '"' + QString::fromWCharArray(device_id) + '"' : QString("nullptr");

	constexpr auto kKeyBufferSize = 1024;
	WCHAR keyBuffer[kKeyBufferSize] = { 0 };
	auto hr = Dlls::PSStringFromPropertyKey ? Dlls::PSStringFromPropertyKey(key, keyBuffer, kKeyBufferSize) : E_FAIL;
	auto keyName = Dlls::PSStringFromPropertyKey ? (SUCCEEDED(hr) ? '"' + QString::fromWCharArray(keyBuffer) + '"' : QString("unknown")) : QString("unsupported");

	// BAD GUID { 0xD4EF3098, 0xC967, 0x4A4E, { 0xB2, 0x19, 0xAC, 0xB6, 0xDA, 0x1D, 0xC3, 0x73 } };
	// BAD GUID { 0x3DE556E2, 0xE087, 0x4721, { 0xBE, 0x97, 0xEC, 0x16, 0x2D, 0x54, 0x81, 0xF8 } };

	// VERY BAD GUID { 0x91F1336D, 0xC37C, 0x4C48, { 0xAD, 0xEB, 0x92, 0x17, 0x2F, 0xA8, 0x7E, 0xEB } };
	// It is fired somewhere from CloseAudioPlaybackDevice() causing deadlock on AudioMutex.

	// Sometimes unknown value change events come very frequently, like each 0.5 seconds.
	// So we will handle only special value change events from mmdeviceapi.h
	//
	// We have logs of PKEY_AudioEndpoint_Disable_SysFx property change 3-5 times each second.
	// So for now we disable PKEY_AudioEndpoint and both PKEY_AudioUnknown changes handling
	//.
	// constexpr GUID pkey_AudioEndpoint = { 0x1da5d803, 0xd492, 0x4edd, { 0x8c, 0x23, 0xe0, 0xc0, 0xff, 0xee, 0x7f, 0x0e } };
	constexpr GUID pkey_AudioEngine_Device = { 0xf19f064d, 0x82c, 0x4e27, { 0xbc, 0x73, 0x68, 0x82, 0xa1, 0xbb, 0x8e, 0x4c } };
	constexpr GUID pkey_AudioEngine_OEM = { 0xe4870e26, 0x3cc5, 0x4cd2, { 0xba, 0x46, 0xca, 0xa, 0x9a, 0x70, 0xed, 0x4 } };
	// constexpr GUID pkey_AudioUnknown1 = { 0x3d6e1656, 0x2e50, 0x4c4c, { 0x8d, 0x85, 0xd0, 0xac, 0xae, 0x3c, 0x6c, 0x68 } };
	// constexpr GUID pkey_AudioUnknown2 = { 0x624f56de, 0xfd24, 0x473e, { 0x81, 0x4a, 0xde, 0x40, 0xaa, 0xca, 0xed, 0x16 } };
	if (false
//		|| key.fmtid == pkey_AudioEndpoint
		|| key.fmtid == pkey_AudioEngine_Device
		|| key.fmtid == pkey_AudioEngine_OEM
//		|| key.fmtid == pkey_AudioUnknown1
//		|| key.fmtid == pkey_AudioUnknown2
		|| false) {
		LOG(("Audio Info: OnPropertyValueChanged(%1, %2) scheduling detach from audio device.").arg(deviceName).arg(keyName));
		Media::Audio::ScheduleDetachFromDeviceSafe();
	} else {
		DEBUG_LOG(("Audio Info: OnPropertyValueChanged(%1, %2) unknown, skipping.").arg(deviceName).arg(keyName));
	}
	return S_OK;
}

STDMETHODIMP DeviceListener::OnDeviceStateChanged(LPCWSTR device_id, DWORD new_state) {
	auto deviceName = device_id ? '"' + QString::fromWCharArray(device_id) + '"' : QString("nullptr");
	LOG(("Audio Info: OnDeviceStateChanged(%1, %2) scheduling detach from audio device.").arg(deviceName).arg(new_state));
	Media::Audio::ScheduleDetachFromDeviceSafe();
	return S_OK;
}

STDMETHODIMP DeviceListener::OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR new_default_device_id) {
	// Only listen for console and communication device changes.
	if ((role != eConsole && role != eCommunications) || (flow != eRender && flow != eCapture)) {
		LOG(("Audio Info: skipping OnDefaultDeviceChanged() flow %1, role %2, new_default_device_id: %3").arg(flow).arg(role).arg(new_default_device_id ? '"' + QString::fromWCharArray(new_default_device_id) + '"' : QString("nullptr")));
		return S_OK;
	}

	LOG(("Audio Info: OnDefaultDeviceChanged() scheduling detach from audio device, flow %1, role %2, new_default_device_id: %3").arg(flow).arg(role).arg(new_default_device_id ? '"' + QString::fromWCharArray(new_default_device_id) + '"' : QString("nullptr")));
	Media::Audio::ScheduleDetachFromDeviceSafe();

	return S_OK;
}

auto WasCoInitialized = false;
ComPtr<IMMDeviceEnumerator> Enumerator;

DeviceListener *Listener = nullptr;

} // namespace

void Init() {
	auto hr = CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&Enumerator));
	if (FAILED(hr)) {
		Enumerator.Reset();

		if (hr == CO_E_NOTINITIALIZED) {
			LOG(("Audio Info: CoCreateInstance fails with CO_E_NOTINITIALIZED"));
			hr = CoInitialize(nullptr);
			if (SUCCEEDED(hr)) {
				WasCoInitialized = true;
				hr = CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&Enumerator));
				if (FAILED(hr)) {
					Enumerator.Reset();

					LOG(("Audio Error: could not CoCreateInstance of MMDeviceEnumerator, HRESULT: %1").arg(hr));
					return;
				}
			}
		} else {
			LOG(("Audio Error: could not CoCreateInstance of MMDeviceEnumerator, HRESULT: %1").arg(hr));
			return;
		}
	}

	Listener = new DeviceListener();
	hr = Enumerator->RegisterEndpointNotificationCallback(Listener);
	if (FAILED(hr)) {
		LOG(("Audio Error: RegisterEndpointNotificationCallback failed, HRESULT: %1").arg(hr));
		delete base::take(Listener);
	}
}

void DeInit() {
	if (Enumerator) {
		if (Listener) {
			auto hr = Enumerator->UnregisterEndpointNotificationCallback(Listener);
			if (FAILED(hr)) {
				LOG(("Audio Error: UnregisterEndpointNotificationCallback failed, HRESULT: %1").arg(hr));
			}
			delete base::take(Listener);
		}
		Enumerator.Reset();
	}
	if (WasCoInitialized) {
		CoUninitialize();
	}
}

} // namespace Audio
} // namespace Platform
