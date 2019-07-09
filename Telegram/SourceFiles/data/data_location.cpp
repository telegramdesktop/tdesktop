/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_location.h"

#include "ui/image/image.h"
#include "data/data_file_origin.h"

namespace Data {
namespace {

GeoPointLocation ComputeLocation(const Data::LocationPoint &point) {
	const auto scale = 1 + (cScale() * cIntRetinaFactor()) / 200;
	const auto zoom = 13 + (scale - 1);
	const auto w = st::locationSize.width() / scale;
	const auto h = st::locationSize.height() / scale;

	auto result = GeoPointLocation();
	result.lat = point.lat();
	result.lon = point.lon();
	result.access = point.accessHash();
	result.width = w;
	result.height = h;
	result.zoom = zoom;
	result.scale = scale;
	return result;
}

} // namespace

LocationThumbnail::LocationThumbnail(const LocationPoint &point)
: point(point)
, thumb(Images::Create(ComputeLocation(point))) {
}

void LocationThumbnail::load(FileOrigin origin) {
	thumb->load(origin);
}

} // namespace Data
