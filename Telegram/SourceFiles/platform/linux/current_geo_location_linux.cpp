/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/current_geo_location_linux.h"

#include "core/current_geo_location.h"
#include "base/platform/linux/base_linux_library.h"

#include <gio/gio.h>

namespace Platform {
namespace {

typedef struct _GClueSimple GClueSimple;
typedef struct _GClueLocation GClueLocation;
typedef struct _GeocodeLocation GeocodeLocation;
typedef struct _GeocodeReverse GeocodeReverse;
typedef struct _GeocodePlace GeocodePlace;

typedef enum {
	GCLUE_ACCURACY_LEVEL_NONE = 0,
	GCLUE_ACCURACY_LEVEL_COUNTRY = 1,
	GCLUE_ACCURACY_LEVEL_CITY = 4,
	GCLUE_ACCURACY_LEVEL_NEIGHBORHOOD = 5,
	GCLUE_ACCURACY_LEVEL_STREET = 6,
	GCLUE_ACCURACY_LEVEL_EXACT = 8,
} GClueAccuracyLevel;

void (*gclue_simple_new)(
	const char *desktop_id,
	GClueAccuracyLevel accuracy_level,
	GCancellable *cancellable,
	GAsyncReadyCallback callback,
	gpointer user_data);

GClueSimple *(*gclue_simple_new_finish)(GAsyncResult *result, GError **error);
GClueLocation *(*gclue_simple_get_location)(GClueSimple *simple);

gdouble (*gclue_location_get_latitude)(GClueLocation *loc);
gdouble (*gclue_location_get_longitude)(GClueLocation *loc);

GeocodeLocation *(*geocode_location_new)(
	gdouble latitude,
	gdouble longitude,
	gdouble accuracy);

GeocodeReverse *(*geocode_reverse_new_for_location)(
	GeocodeLocation *location);

void (*geocode_reverse_resolve_async)(
	GeocodeReverse *object,
	GCancellable *cancellable,
	GAsyncReadyCallback callback,
	gpointer user_data);

GeocodePlace *(*geocode_reverse_resolve_finish)(
	GeocodeReverse *object,
	GAsyncResult *res,
	GError **error);

const char *(*geocode_place_get_street_address)(GeocodePlace *place);
const char *(*geocode_place_get_town)(GeocodePlace *place);
const char *(*geocode_place_get_country)(GeocodePlace *place);

} // namespace

void ResolveCurrentExactLocation(Fn<void(Core::GeoLocation)> callback) {
	static const auto Inited = [] {
		const auto lib = base::Platform::LoadLibrary(
			"libgeoclue-2.so.0",
			RTLD_NODELETE);
		return lib
			&& LOAD_LIBRARY_SYMBOL(lib, gclue_simple_new)
			&& LOAD_LIBRARY_SYMBOL(lib, gclue_simple_new_finish)
			&& LOAD_LIBRARY_SYMBOL(lib, gclue_simple_get_location)
			&& LOAD_LIBRARY_SYMBOL(lib, gclue_location_get_latitude)
			&& LOAD_LIBRARY_SYMBOL(lib, gclue_location_get_longitude);
	}();

	if (!Inited) {
		callback({});
		return;
	}

	gclue_simple_new(
		QGuiApplication::desktopFileName().toUtf8().constData(),
		GCLUE_ACCURACY_LEVEL_EXACT,
		nullptr,
		GAsyncReadyCallback(+[](
				GObject *object,
				GAsyncResult* res,
				Fn<void(Core::GeoLocation)> *callback) {
			const auto callbackGuard = gsl::finally([&] {
				delete callback;
			});

			const auto simple = gclue_simple_new_finish(res, nullptr);
			if (!simple) {
				(*callback)({});
				return;
			}

			const auto simpleGuard = gsl::finally([&] {
				g_object_unref(simple);
			});

			const auto location = gclue_simple_get_location(simple);

			(*callback)({
				.point = {
					gclue_location_get_latitude(location),
					gclue_location_get_longitude(location),
				},
				.accuracy = Core::GeoLocationAccuracy::Exact,
			});
		}),
		new Fn(callback));
}

void ResolveLocationAddress(
		const Core::GeoLocation &location,
		const QString &language,
		Fn<void(Core::GeoAddress)> callback) {
	static const auto Inited = [] {
		const auto lib = base::Platform::LoadLibrary(
			"libgeocode-glib-2.so.0",
			RTLD_NODELETE) ?: base::Platform::LoadLibrary(
			"libgeocode-glib.so.0",
			RTLD_NODELETE);
		return lib
			&& LOAD_LIBRARY_SYMBOL(lib, geocode_location_new)
			&& LOAD_LIBRARY_SYMBOL(lib, geocode_reverse_new_for_location)
			&& LOAD_LIBRARY_SYMBOL(lib, geocode_reverse_resolve_async)
			&& LOAD_LIBRARY_SYMBOL(lib, geocode_reverse_resolve_finish)
			&& LOAD_LIBRARY_SYMBOL(lib, geocode_place_get_street_address)
			&& LOAD_LIBRARY_SYMBOL(lib, geocode_place_get_town)
			&& LOAD_LIBRARY_SYMBOL(lib, geocode_place_get_country);
	}();

	if (!Inited) {
		callback({});
		return;
	}

	geocode_reverse_resolve_async(
		geocode_reverse_new_for_location(geocode_location_new(
			location.point.x(),
			location.point.y(),
			-1)),
		nullptr,
		GAsyncReadyCallback(+[](
				GeocodeReverse *reverse,
				GAsyncResult* res,
				Fn<void(Core::GeoAddress)> *callback) {
			const auto argsGuard = gsl::finally([&] {
				delete callback;
				g_object_unref(reverse);
			});

			const auto place = geocode_reverse_resolve_finish(
				reverse,
				res,
				nullptr);

			if (!place) {
				(*callback)({});
				return;
			}

			const auto placeGuard = gsl::finally([&] {
				g_object_unref(place);
			});

			const auto values = {
				geocode_place_get_street_address(place),
				geocode_place_get_town(place),
				geocode_place_get_country(place),
			};

			QStringList checked;
			for (const auto &value : values) {
				if (value) {
					const auto qt = QString::fromUtf8(value);
					if (!qt.isEmpty()) {
						checked.push_back(qt);
					}
				}
			}

			(*callback)({ .name = checked.join(u", "_q) });
		}),
		new Fn(callback));
}

} // namespace Platform
