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

[[nodiscard]] QString AsString(float64 value) {
	constexpr auto kPrecision = 6;
	return QString::number(value, 'f', kPrecision);
}

} // namespace

LocationPoint::LocationPoint(const MTPDgeoPoint &point)
: _lat(point.vlat().v)
, _lon(point.vlong().v)
, _access(point.vaccess_hash().v) {
}

QString LocationPoint::latAsString() const {
	return AsString(_lat);
}

QString LocationPoint::lonAsString() const {
	return AsString(_lon);
}

MTPGeoPoint LocationPoint::toMTP() const {
	return MTP_geoPoint(
		MTP_flags(0),
		MTP_double(_lon),
		MTP_double(_lat),
		MTP_long(_access),
		MTP_int(0)); // accuracy_radius
}

float64 LocationPoint::lat() const {
	return _lat;
}

float64 LocationPoint::lon() const {
	return _lon;
}

uint64 LocationPoint::accessHash() const {
	return _access;
}

size_t LocationPoint::hash() const {
	return QtPrivate::QHashCombine().operator()(
		std::hash<float64>()(_lat),
		_lon);
}

GeoPointLocation ComputeLocation(const LocationPoint &point) {
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

} // namespace Data
