/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "webauthn/cable_scanner.h"

#include "webauthn/cable_core.h"
#include "base/algorithm.h"
#include "base/basic_types.h"
#include "base/platform/win/base_windows_safe_library.h"
#include "base/platform/win/base_windows_winrt.h"

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Storage.Streams.h>

#include <windows.h>
#include <bluetoothapis.h>

#include <crl/crl.h>

#include <memory>
#include <set>
#include <vector>

namespace Platform::WebAuthn::Cable {
namespace {

using namespace winrt::Windows::Devices::Bluetooth::Advertisement;
using namespace winrt::Windows::Storage::Streams;

constexpr auto kServiceDataAdType = uint8_t(0x16);
constexpr auto kGoogleCableUuid16 = uint16_t(0xFDE2);
constexpr auto kFidoCableUuid16 = uint16_t(0xFFF9);

[[nodiscard]] std::vector<QByteArray> CableServiceData(
		const BluetoothLEAdvertisement &advertisement) {
	auto result = std::vector<QByteArray>();
	const auto sections = advertisement.GetSectionsByType(kServiceDataAdType);
	for (const auto &section : sections) {
		const auto buffer = section.Data();
		if (!buffer || buffer.Length() < 2 + kAdvertSize) {
			continue;
		}
		auto bytes = std::vector<uint8_t>(buffer.Length());
		DataReader::FromBuffer(buffer).ReadBytes(bytes);
		const auto uuid = uint16_t(bytes[0]) | (uint16_t(bytes[1]) << 8);
		if (uuid != kGoogleCableUuid16 && uuid != kFidoCableUuid16) {
			continue;
		}
		result.emplace_back(
			reinterpret_cast<const char*>(bytes.data() + 2),
			int(bytes.size() - 2));
	}
	return result;
}

HBLUETOOTH_RADIO_FIND(__stdcall *BluetoothFindFirstRadio)(
	const BLUETOOTH_FIND_RADIO_PARAMS *pbtfrp,
	HANDLE *phRadio);
BOOL(__stdcall *BluetoothFindRadioClose)(HBLUETOOTH_RADIO_FIND hFind);

[[nodiscard]] bool ResolveBluetoothApi() {
	const auto bthprops = base::Platform::SafeLoadLibrary(L"bthprops.cpl");
	if (!bthprops) {
		return false;
	}
	auto total = 0, resolved = 0;
#define LOAD_SYMBOL(name) \
	++total; \
	if (base::Platform::LoadMethod(bthprops, #name, name)) ++resolved;

	LOAD_SYMBOL(BluetoothFindFirstRadio);
	LOAD_SYMBOL(BluetoothFindRadioClose);
#undef LOAD_SYMBOL

	return (total == resolved);
}

[[nodiscard]] bool BluetoothRadioPresent() {
	static const auto Resolved = ResolveBluetoothApi();
	if (!Resolved) {
		return false;
	}
	auto params = BLUETOOTH_FIND_RADIO_PARAMS();
	params.dwSize = sizeof(BLUETOOTH_FIND_RADIO_PARAMS);
	auto radio = HANDLE(nullptr);
	const auto found = BluetoothFindFirstRadio(&params, &radio);
	if (!found) {
		return false;
	}
	CloseHandle(radio);
	BluetoothFindRadioClose(found);
	return true;
}

struct State {
	std::function<void(QByteArray)> onAdvert;
	std::function<void(bool)> onAvailability;
	std::set<QByteArray> seen;
	bool stopped = false;
};

class WinBleScanner final : public BleScanner {
public:
	~WinBleScanner();

	bool start(
		std::function<void(QByteArray)> onAdvert,
		std::function<void(bool)> onAvailability) override;
	void stop() override;

private:
	std::shared_ptr<State> _state;
	BluetoothLEAdvertisementWatcher _watcher = { nullptr };
	winrt::event_token _receivedToken;

};

WinBleScanner::~WinBleScanner() {
	stop();
}

bool WinBleScanner::start(
		std::function<void(QByteArray)> onAdvert,
		std::function<void(bool)> onAvailability) {
	if (!BluetoothRadioPresent()) {
		return false;
	}
	_state = std::make_shared<State>();
	_state->onAdvert = std::move(onAdvert);
	_state->onAvailability = std::move(onAvailability);
	const auto weak = std::weak_ptr(_state);
	const auto started = base::WinRT::Try([&] {
		_watcher = BluetoothLEAdvertisementWatcher();
		_watcher.ScanningMode(BluetoothLEScanningMode::Active);
		_receivedToken = _watcher.Received([weak](
				const BluetoothLEAdvertisementWatcher&,
				const BluetoothLEAdvertisementReceivedEventArgs &args) {
			const auto advertisement = args.Advertisement();
			if (!advertisement) {
				return;
			}
			auto blobs = base::WinRT::Try([&] {
				return CableServiceData(advertisement);
			});
			if (!blobs || blobs->empty()) {
				return;
			}
			crl::on_main([weak, blobs = std::move(*blobs)] {
				const auto state = weak.lock();
				if (!state || state->stopped) {
					return;
				}
				for (const auto &blob : blobs) {
					if (state->seen.emplace(blob).second && state->onAdvert) {
						state->onAdvert(blob);
					}
				}
			});
		});
		_watcher.Start();
	});
	if (!started || !_watcher) {
		return false;
	}
	crl::on_main([weak] {
		const auto state = weak.lock();
		if (state && !state->stopped && state->onAvailability) {
			base::take(state->onAvailability)(true);
		}
	});
	return true;
}

void WinBleScanner::stop() {
	if (_state) {
		_state->stopped = true;
	}
	if (_watcher) {
		base::WinRT::Try([&] {
			_watcher.Received(_receivedToken);
			_watcher.Stop();
		});
		_watcher = { nullptr };
	}
	_state = nullptr;
}

} // namespace

std::unique_ptr<BleScanner> MakeBleScanner() {
	return std::make_unique<WinBleScanner>();
}

} // namespace Platform::WebAuthn::Cable
