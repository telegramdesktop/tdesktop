/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "webauthn/cable_scanner.h"

#include "webauthn/cable_core.h"

#include "base/algorithm.h"
#include "base/debug_log.h"
#include "base/weak_ptr.h"

#include <crl/crl_on_main.h>
#include <bluez/bluez.hpp>

#include <set>
#include <vector>

namespace Platform::WebAuthn::Cable {
namespace {

using namespace gi::repository;
namespace GObject = gi::repository::GObject;

constexpr auto kBluezService = "org.bluez";
constexpr auto kBluezObjectPath = "/";

constexpr auto kGoogleCableUuid = "0000fde2-0000-1000-8000-00805f9b34fb";
constexpr auto kFidoCableUuid = "0000fff9-0000-1000-8000-00805f9b34fb";

[[nodiscard]] bool IsCableUuid(const gi::cstring_v uuid) {
	return !GLib::ascii_strcasecmp(uuid, kGoogleCableUuid)
		|| !GLib::ascii_strcasecmp(uuid, kFidoCableUuid);
}

[[nodiscard]] QByteArray FixedArrayBytes(GLib::Variant value) {
	return QByteArray(
		static_cast<const char*>(value.get_data()),
		int(value.get_size()));
}

[[nodiscard]] std::vector<QByteArray> CableServiceData(
		GLib::Variant serviceData) {
	auto result = std::vector<QByteArray>();
	if (!serviceData) {
		return result;
	}
	const auto count = serviceData.n_children();
	for (auto i = gsize(0); i != count; ++i) {
		auto entry = serviceData.get_child_value(i);
		if (!IsCableUuid(entry.get_child_value(0).get_string(nullptr))) {
			continue;
		}
		auto value = entry.get_child_value(1).get_variant();
		if (value.is_of_type(GLib::VariantType::new_("ay"))
			&& value.get_size() >= kAdvertSize) {
			result.push_back(FixedArrayBytes(value));
		}
	}
	return result;
}

[[nodiscard]] GLib::Variant CableDiscoveryFilter() {
	return GLib::Variant::new_array({
		GLib::Variant::new_dict_entry(
			GLib::Variant::new_string("Transport"),
			GLib::Variant::new_variant(GLib::Variant::new_string("le"))),
		GLib::Variant::new_dict_entry(
			GLib::Variant::new_string("DuplicateData"),
			GLib::Variant::new_variant(GLib::Variant::new_boolean(true))),
	});
}

class LinuxBleScanner final : public BleScanner, public base::has_weak_ptr {
public:
	~LinuxBleScanner();

	bool start(
		std::function<void(QByteArray)> onAdvert,
		std::function<void(bool)> onAvailability) override;
	void stop() override;

private:
	void subscribeToObjects();
	void handleObject(Gio::DBusObject object);
	void handleDevice(Bluez::Device1 device);
	void setDiscoveryFilter();
	void startDiscovery();
	void handleAvailability(bool available);

	Bluez::ObjectManagerClient _manager;
	Bluez::Adapter1 _adapter;
	Gio::Cancellable _cancellable;
	std::set<QByteArray> _seen;
	bool _discovering = false;
	std::function<void(QByteArray)> _onAdvert;
	std::function<void(bool)> _onAvailability;

};

LinuxBleScanner::~LinuxBleScanner() {
	stop();
}

bool LinuxBleScanner::start(
		std::function<void(QByteArray)> onAdvert,
		std::function<void(bool)> onAvailability) {
	stop();

	_cancellable = Gio::Cancellable::new_();
	_onAdvert = std::move(onAdvert);
	_onAvailability = std::move(onAvailability);

	Bluez::ObjectManagerClient::new_for_bus(
		Gio::BusType::SYSTEM_,
		Gio::DBusObjectManagerClientFlags::NONE_,
		kBluezService,
		kBluezObjectPath,
		_cancellable,
		crl::guard(this, [=](GObject::Object, Gio::AsyncResult result) {
			if (!_cancellable) {
				return;
			}
			auto manager = Bluez::ObjectManagerClient::new_for_bus_finish(
				result);
			if (!manager) {
				Gio::DBusErrorNS_::strip_remote_error(manager.error());
				LOG(("Passkey Error: BlueZ object manager: %1."
					).arg(manager.error().message_().c_str()));
				handleAvailability(false);
				return;
			}
			_manager = *manager;
			subscribeToObjects();
			auto objects = Gio::DBusObjectManager(_manager).get_objects();
			for (auto object : objects) {
				handleObject(object);
			}
			if (!_adapter) {
				LOG(("Passkey Error: No bluetooth adapter in BlueZ."));
				handleAvailability(false);
				return;
			} else if (!_adapter.get_powered()) {
				LOG(("Passkey Error: Bluetooth adapter is powered off."));
				handleAvailability(false);
				return;
			}
			setDiscoveryFilter();
		}));
	return true;
}

void LinuxBleScanner::subscribeToObjects() {
	Gio::DBusObjectManager(_manager).signal_object_added().connect(
		crl::guard(this, [=](
				Gio::DBusObjectManager,
				Gio::DBusObject object) {
			handleObject(object);
		}));
}

void LinuxBleScanner::handleObject(Gio::DBusObject object) {
	auto typed = gi::object_cast<Bluez::Object>(object);
	if (!typed) {
		return;
	} else if (auto device = typed.get_device1()) {
		device.property_service_data().signal_notify().connect(
			crl::guard(this, [=](auto changed, auto) {
				if (auto device = gi::object_cast<Bluez::Device1>(changed)) {
					handleDevice(device);
				}
			}));
		handleDevice(device);
	} else if (!_adapter) {
		_adapter = typed.get_adapter1();
	}
}

void LinuxBleScanner::handleDevice(Bluez::Device1 device) {
	for (auto &blob : CableServiceData(device.get_service_data())) {
		if (_seen.emplace(blob).second && _onAdvert) {
			_onAdvert(blob);
		}
	}
}

void LinuxBleScanner::setDiscoveryFilter() {
	_adapter.call_set_discovery_filter(
		CableDiscoveryFilter(),
		_cancellable,
		crl::guard(this, [=](GObject::Object, Gio::AsyncResult) {
			if (!_cancellable) {
				return;
			}
			startDiscovery();
		}));
}

void LinuxBleScanner::startDiscovery() {
	_adapter.call_start_discovery(
		_cancellable,
		crl::guard(this, [=](GObject::Object, Gio::AsyncResult result) {
			if (!_cancellable) {
				return;
			}
			auto started = _adapter.call_start_discovery_finish(result);
			if (!started) {
				Gio::DBusErrorNS_::strip_remote_error(started.error());
				LOG(("Passkey Error: BlueZ StartDiscovery: %1."
					).arg(started.error().message_().c_str()));
				handleAvailability(false);
				return;
			}
			_discovering = true;
			handleAvailability(true);
		}));
}

void LinuxBleScanner::handleAvailability(bool available) {
	if (const auto onAvailability = base::take(_onAvailability)) {
		onAvailability(available);
	}
}

void LinuxBleScanner::stop() {
	if (!_cancellable) {
		return;
	}
	base::take(_cancellable).cancel();
	if (base::take(_discovering)) {
		_adapter.call_stop_discovery([](GObject::Object, Gio::AsyncResult) {});
	}
	_manager = nullptr;
	_adapter = nullptr;
	_onAdvert = nullptr;
	_onAvailability = nullptr;
	_seen.clear();
}

} // namespace

std::unique_ptr<BleScanner> MakeBleScanner() {
	return std::make_unique<LinuxBleScanner>();
}

} // namespace Platform::WebAuthn::Cable
