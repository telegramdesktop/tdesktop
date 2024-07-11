/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/current_geo_location_linux.h"

#include "core/current_geo_location.h"

namespace Platform {

void ResolveCurrentExactLocation(Fn<void(Core::GeoLocation)> callback) {
	callback({});
}
void ResolveLocationAddress(
		const Core::GeoLocation &location,
		const QString &language,
		Fn<void(Core::GeoAddress)> callback) {
	callback({});
}

} // namespace Platform
