/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_location.h"

#include "ui/image/image.h"
#include "data/data_file_origin.h"

#include <cmath>

namespace Data {
namespace {

[[nodiscard]] QString AsString(float64 value) {
	constexpr auto kPrecision = 6;
	return QString::number(value, 'f', kPrecision);
}

} // namespace

bool ValidGeoCoordinates(float64 lat, float64 lon) {
	return std::isfinite(lat)
		&& std::isfinite(lon)
		&& (std::abs(lat) <= 90.)
		&& (std::abs(lon) <= 180.);
}

LocationPoint::LocationPoint(float64 lat, float64 lon, uint64 accessHash)
: _lat(lat)
, _lon(lon)
, _access(accessHash) {
}

std::optional<LocationPoint> LocationPoint::FromMTP(
		const MTPGeoPoint &point) {
	return point.match([](const MTPDgeoPoint &data) {
		return FromCoordinates(
			data.vlat().v,
			data.vlong().v,
			data.vaccess_hash().v);
	}, [](const MTPDgeoPointEmpty &) {
		return std::optional<LocationPoint>();
	});
}

std::optional<LocationPoint> LocationPoint::FromCoordinates(
		float64 lat,
		float64 lon,
		uint64 accessHash) {
	if (!ValidGeoCoordinates(lat, lon)) {
		return std::nullopt;
	}
	return LocationPoint(lat, lon, accessHash);
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
	const auto scale = 1 + (cScale() * style::DevicePixelRatio()) / 200;
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
