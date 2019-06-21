/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {

struct FileOrigin;

class LocationPoint {
public:
	LocationPoint() = default;
	explicit LocationPoint(const MTPDgeoPoint &point)
	: _lat(point.vlat.v)
	, _lon(point.vlong.v)
	, _access(point.vaccess_hash.v) {
	}

	QString latAsString() const {
		return AsString(_lat);
	}
	QString lonAsString() const {
		return AsString(_lon);
	}
	MTPGeoPoint toMTP() const {
		return MTP_geoPoint(
			MTP_double(_lon),
			MTP_double(_lat),
			MTP_long(_access));
	}

	float64 lat() const {
		return _lat;
	}
	float64 lon() const {
		return _lon;
	}
	uint64 accessHash() const {
		return _access;
	}

	inline size_t hash() const {
#ifndef OS_MAC_OLD
		return QtPrivate::QHashCombine().operator()(
				std::hash<float64>()(_lat),
				_lon);
#else // OS_MAC_OLD
		const auto h1 = std::hash<float64>()(_lat);
		const auto h2 = std::hash<float64>()(_lon);
		return ((h1 << 16) | (h1 >> 16)) ^ h2;
#endif // OS_MAC_OLD
	}

private:
	static QString AsString(float64 value) {
		constexpr auto kPrecision = 6;
		return QString::number(value, 'f', kPrecision);
	}

	friend inline bool operator==(const LocationPoint &a, const LocationPoint &b) {
		return (a._lat == b._lat) && (a._lon == b._lon);
	}

	friend inline bool operator<(const LocationPoint &a, const LocationPoint &b) {
		return (a._lat < b._lat) || ((a._lat == b._lat) && (a._lon < b._lon));
	}

	float64 _lat = 0;
	float64 _lon = 0;
	uint64 _access = 0;

};

struct LocationThumbnail {
	LocationThumbnail(const LocationPoint &point);

	LocationPoint point;
	ImagePtr thumb;

	void load(FileOrigin origin);

};

} // namespace Data

namespace std {

template <>
struct hash<Data::LocationPoint> {
	size_t operator()(const Data::LocationPoint &value) const {
		return value.hash();
	}
};

} // namespace std
