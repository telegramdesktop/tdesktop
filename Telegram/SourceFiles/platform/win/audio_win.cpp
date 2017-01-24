/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "platform/win/audio_win.h"

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
	LOG(("Audio Info: OnPropertyValueChanged() scheduling detach from audio device."));
	Media::Player::DetachFromDeviceByTimer();
	return S_OK;
}

STDMETHODIMP DeviceListener::OnDeviceStateChanged(LPCWSTR device_id, DWORD new_state) {
	LOG(("Audio Info: OnDeviceStateChanged() scheduling detach from audio device."));
	Media::Player::DetachFromDeviceByTimer();
	return S_OK;
}

STDMETHODIMP DeviceListener::OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR new_default_device_id) {
	// Only listen for console and communication device changes.
	if ((role != eConsole && role != eCommunications) || (flow != eRender && flow != eCapture)) {
		LOG(("Audio Info: skipping OnDefaultDeviceChanged() flow %1, role %2, new_default_device_id: %3").arg(flow).arg(role).arg(new_default_device_id ? '"' + QString::fromWCharArray(new_default_device_id) + '"' : QString("nullptr")));
		return S_OK;
	}

	LOG(("Audio Info: OnDefaultDeviceChanged() scheduling detach from audio device, flow %1, role %2, new_default_device_id: %3").arg(flow).arg(role).arg(new_default_device_id ? '"' + QString::fromWCharArray(new_default_device_id) + '"' : QString("nullptr")));
	Media::Player::DetachFromDeviceByTimer();

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
	AUDCLNT_E_NOT_INITIALIZED;
}

} // namespace Audio
} // namespace Platform
