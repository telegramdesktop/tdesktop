/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_location_manager.h"

#include "mainwidget.h"
#include "lang/lang_keys.h"
#include "platform/platform_specific.h"

namespace {

constexpr auto kCoordPrecision = 8;
constexpr auto kMaxHttpRedirects = 5;

GeoPointLocation ComputeLocation(const LocationCoords &coords) {
	int32 w = st::locationSize.width(), h = st::locationSize.height();
	int32 zoom = 15, scale = 1;
	if (cScale() == dbisTwo || cRetina()) {
		scale = 2;
		zoom = 16;
	} else {
		w = convertScale(w);
		h = convertScale(h);
	}

	auto result = GeoPointLocation();
	result.lat = coords.lat();
	result.lon = coords.lon();
	result.access = coords.accessHash();
	result.width = w;
	result.height = h;
	result.zoom = zoom;
	result.scale = scale;
	return result;
}

} // namespace

QString LocationClickHandler::copyToClipboardText() const {
	return _text;
}

QString LocationClickHandler::copyToClipboardContextItemText() const {
	return lang(lng_context_copy_link);
}

void LocationClickHandler::onClick(ClickContext context) const {
	if (!psLaunchMaps(_coords)) {
		QDesktopServices::openUrl(_text);
	}
}

void LocationClickHandler::setup() {
	auto latlon = _coords.latAsString() + ',' + _coords.lonAsString();
	_text = qsl("https://maps.google.com/maps?q=") + latlon + qsl("&ll=") + latlon + qsl("&z=16");
}

LocationData::LocationData(const LocationCoords &coords)
: coords(coords)
, thumb(ComputeLocation(coords)) {
}

void LocationData::load(Data::FileOrigin origin) {
	thumb->load(origin, false, false);
}
