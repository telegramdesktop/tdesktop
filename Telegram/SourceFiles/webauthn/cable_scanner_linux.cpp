/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "webauthn/cable_scanner.h"

#include "webauthn/cable_core.h"

#include <gio/gio.h>

#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace Platform::WebAuthn::Cable {
namespace {

constexpr auto kBluezService = "org.bluez";
constexpr auto kBluezAdapterInterface = "org.bluez.Adapter1";
constexpr auto kBluezDeviceInterface = "org.bluez.Device1";
constexpr auto kObjectManagerInterface = "org.freedesktop.DBus.ObjectManager";
constexpr auto kPropertiesInterface = "org.freedesktop.DBus.Properties";

constexpr auto kGoogleCableUuid = "0000fde2-0000-1000-8000-00805f9b34fb";
constexpr auto kFidoCableUuid = "0000fff9-0000-1000-8000-00805f9b34fb";

[[nodiscard]] bool IsCableUuid(const char *uuid) {
	return uuid
		&& (g_ascii_strcasecmp(uuid, kGoogleCableUuid) == 0
			|| g_ascii_strcasecmp(uuid, kFidoCableUuid) == 0);
}

[[nodiscard]] std::vector<QByteArray> CableServiceData(GVariant *properties) {
	auto result = std::vector<QByteArray>();
	auto serviceData = g_variant_lookup_value(
		properties,
		"ServiceData",
		G_VARIANT_TYPE("a{sv}"));
	if (!serviceData) {
		return result;
	}
	auto iterator = GVariantIter();
	g_variant_iter_init(&iterator, serviceData);
	auto uuid = (const char*)nullptr;
	auto value = (GVariant*)nullptr;
	while (g_variant_iter_loop(&iterator, "{&sv}", &uuid, &value)) {
		if (!IsCableUuid(uuid)
			|| !g_variant_is_of_type(value, G_VARIANT_TYPE("ay"))) {
			continue;
		}
		gsize size = 0;
		const auto data = static_cast<const char*>(
			g_variant_get_fixed_array(value, &size, sizeof(guint8)));
		if (data && size >= kAdvertSize) {
			result.emplace_back(data, int(size));
		}
	}
	g_variant_unref(serviceData);
	return result;
}

class LinuxBleScanner final : public BleScanner {
public:
	~LinuxBleScanner();

	bool start(
		std::function<void(QByteArray)> onAdvert,
		std::function<void()> onUnavailable) override;
	void stop() override;

	void handleServiceData(GDBusConnection *connection, void *serviceData);

private:
	GDBusConnection *_connection = nullptr;
	std::string _adapterPath;
	guint _interfacesAddedId = 0;
	guint _propertiesChangedId = 0;
	std::set<QByteArray> _seen;
	bool _discovering = false;
	std::function<void(QByteArray)> _onAdvert;

};

std::string ScanManagedObjects(
		GDBusConnection *connection,
		const std::function<void(GVariant*)> &deviceCallback) {
	auto error = (GError*)nullptr;
	auto reply = g_dbus_connection_call_sync(
		connection,
		kBluezService,
		"/",
		kObjectManagerInterface,
		"GetManagedObjects",
		nullptr,
		G_VARIANT_TYPE("(a{oa{sa{sv}}})"),
		G_DBUS_CALL_FLAGS_NONE,
		5000,
		nullptr,
		&error);
	if (!reply) {
		if (error) {
			g_error_free(error);
		}
		return {};
	}
	auto adapterPath = std::string();
	auto objects = g_variant_get_child_value(reply, 0);
	auto objectIterator = GVariantIter();
	g_variant_iter_init(&objectIterator, objects);
	auto path = (const char*)nullptr;
	auto interfaces = (GVariant*)nullptr;
	while (g_variant_iter_loop(
			&objectIterator,
			"{&o@a{sa{sv}}}",
			&path,
			&interfaces)) {
		auto interfaceIterator = GVariantIter();
		g_variant_iter_init(&interfaceIterator, interfaces);
		auto interfaceName = (const char*)nullptr;
		auto properties = (GVariant*)nullptr;
		while (g_variant_iter_loop(
				&interfaceIterator,
				"{&s@a{sv}}",
				&interfaceName,
				&properties)) {
			if (!interfaceName) {
				continue;
			}
			if (adapterPath.empty()
				&& !std::strcmp(interfaceName, kBluezAdapterInterface)) {
				adapterPath = path;
			} else if (!std::strcmp(interfaceName, kBluezDeviceInterface)
				&& deviceCallback) {
				deviceCallback(properties);
			}
		}
	}
	g_variant_unref(objects);
	g_variant_unref(reply);
	return adapterPath;
}

void InterfacesAddedCallback(
		GDBusConnection *connection,
		const gchar *sender,
		const gchar *path,
		const gchar *interfaceName,
		const gchar *signalName,
		GVariant *parameters,
		gpointer userData) {
	const auto scanner = static_cast<LinuxBleScanner*>(userData);
	auto interfaces = g_variant_get_child_value(parameters, 1);
	auto iterator = GVariantIter();
	g_variant_iter_init(&iterator, interfaces);
	auto added = (const char*)nullptr;
	auto properties = (GVariant*)nullptr;
	while (g_variant_iter_loop(&iterator, "{&s@a{sv}}", &added, &properties)) {
		if (added && !std::strcmp(added, kBluezDeviceInterface)) {
			scanner->handleServiceData(connection, properties);
		}
	}
	g_variant_unref(interfaces);
}

void PropertiesChangedCallback(
		GDBusConnection *connection,
		const gchar *sender,
		const gchar *path,
		const gchar *interfaceName,
		const gchar *signalName,
		GVariant *parameters,
		gpointer userData) {
	const auto scanner = static_cast<LinuxBleScanner*>(userData);
	auto changedInterface = g_variant_get_child_value(parameters, 0);
	const auto name = g_variant_get_string(changedInterface, nullptr);
	if (name && !std::strcmp(name, kBluezDeviceInterface)) {
		auto properties = g_variant_get_child_value(parameters, 1);
		scanner->handleServiceData(connection, properties);
		g_variant_unref(properties);
	}
	g_variant_unref(changedInterface);
}

LinuxBleScanner::~LinuxBleScanner() {
	stop();
}

void LinuxBleScanner::handleServiceData(
		GDBusConnection *connection,
		void *serviceData) {
	const auto properties = static_cast<GVariant*>(serviceData);
	for (auto &blob : CableServiceData(properties)) {
		if (_seen.emplace(blob).second && _onAdvert) {
			_onAdvert(blob);
		}
	}
}

bool LinuxBleScanner::start(
		std::function<void(QByteArray)> onAdvert,
		std::function<void()> onUnavailable) {
	_onAdvert = std::move(onAdvert);

	auto error = (GError*)nullptr;
	_connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
	if (!_connection) {
		if (error) {
			g_error_free(error);
		}
		return false;
	}
	const auto connection = _connection;

	_adapterPath = ScanManagedObjects(connection, [&](GVariant *p) {
		handleServiceData(connection, p);
	});
	if (_adapterPath.empty()) {
		return false;
	}

	_interfacesAddedId = g_dbus_connection_signal_subscribe(
		connection,
		kBluezService,
		kObjectManagerInterface,
		"InterfacesAdded",
		nullptr,
		nullptr,
		G_DBUS_SIGNAL_FLAGS_NONE,
		InterfacesAddedCallback,
		this,
		nullptr);
	_propertiesChangedId = g_dbus_connection_signal_subscribe(
		connection,
		kBluezService,
		kPropertiesInterface,
		"PropertiesChanged",
		nullptr,
		nullptr,
		G_DBUS_SIGNAL_FLAGS_NONE,
		PropertiesChangedCallback,
		this,
		nullptr);

	auto builder = GVariantBuilder();
	g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));
	g_variant_builder_add(
		&builder,
		"{sv}",
		"Transport",
		g_variant_new_string("le"));
	g_variant_builder_add(
		&builder,
		"{sv}",
		"DuplicateData",
		g_variant_new_boolean(TRUE));
	auto filterError = (GError*)nullptr;
	auto filterReply = g_dbus_connection_call_sync(
		connection,
		kBluezService,
		_adapterPath.c_str(),
		kBluezAdapterInterface,
		"SetDiscoveryFilter",
		g_variant_new("(a{sv})", &builder),
		nullptr,
		G_DBUS_CALL_FLAGS_NONE,
		5000,
		nullptr,
		&filterError);
	if (filterReply) {
		g_variant_unref(filterReply);
	} else if (filterError) {
		g_error_free(filterError);
	}

	auto startError = (GError*)nullptr;
	auto startReply = g_dbus_connection_call_sync(
		connection,
		kBluezService,
		_adapterPath.c_str(),
		kBluezAdapterInterface,
		"StartDiscovery",
		nullptr,
		nullptr,
		G_DBUS_CALL_FLAGS_NONE,
		5000,
		nullptr,
		&startError);
	if (startReply) {
		g_variant_unref(startReply);
		_discovering = true;
	} else {
		if (startError) {
			g_error_free(startError);
		}
		stop();
		return false;
	}

	return true;
}

void LinuxBleScanner::stop() {
	if (!_connection) {
		return;
	}
	const auto connection = _connection;
	if (_interfacesAddedId) {
		g_dbus_connection_signal_unsubscribe(connection, _interfacesAddedId);
		_interfacesAddedId = 0;
	}
	if (_propertiesChangedId) {
		g_dbus_connection_signal_unsubscribe(connection, _propertiesChangedId);
		_propertiesChangedId = 0;
	}
	if (_discovering) {
		auto stopError = (GError*)nullptr;
		auto stopReply = g_dbus_connection_call_sync(
			connection,
			kBluezService,
			_adapterPath.c_str(),
			kBluezAdapterInterface,
			"StopDiscovery",
			nullptr,
			nullptr,
			G_DBUS_CALL_FLAGS_NONE,
			5000,
			nullptr,
			&stopError);
		if (stopReply) {
			g_variant_unref(stopReply);
		} else if (stopError) {
			g_error_free(stopError);
		}
		_discovering = false;
	}
	g_object_unref(connection);
	_connection = nullptr;
}

} // namespace

std::unique_ptr<BleScanner> MakeBleScanner() {
	return std::make_unique<LinuxBleScanner>();
}

} // namespace Platform::WebAuthn::Cable
