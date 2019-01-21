/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class LocationCoords {
public:
	LocationCoords() = default;
	explicit LocationCoords(const MTPDgeoPoint &point)
	: _lat(point.vlat.v)
	, _lon(point.vlong.v)
	, _access(point.vaccess_hash.v) {
	}

	QString latAsString() const {
		return asString(_lat);
	}
	QString lonAsString() const {
		return asString(_lon);
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
	static QString asString(float64 value) {
		constexpr auto kPrecision = 6;
		return QString::number(value, 'f', kPrecision);
	}

	friend inline bool operator==(const LocationCoords &a, const LocationCoords &b) {
		return (a._lat == b._lat) && (a._lon == b._lon);
	}

	friend inline bool operator<(const LocationCoords &a, const LocationCoords &b) {
		return (a._lat < b._lat) || ((a._lat == b._lat) && (a._lon < b._lon));
	}

	float64 _lat = 0;
	float64 _lon = 0;
	uint64 _access = 0;

};

namespace std {

template <>
struct hash<LocationCoords> {
	size_t operator()(const LocationCoords &value) const {
		return value.hash();
	}
};

} // namespace std

struct LocationData {
	LocationData(const LocationCoords &coords);

	LocationCoords coords;
	ImagePtr thumb;

	void load(Data::FileOrigin origin);

};

class LocationClickHandler : public ClickHandler {
public:
	LocationClickHandler(const LocationCoords &coords) : _coords(coords) {
		setup();
	}

	void onClick(ClickContext context) const override;

	QString tooltip() const override {
		return QString();
	}

	QString dragText() const override {
		return _text;
	}

	QString copyToClipboardText() const override;
	QString copyToClipboardContextItemText() const override;

private:

	void setup();
	LocationCoords _coords;
	QString _text;

};
