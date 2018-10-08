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

	friend inline uint qHash(const LocationCoords &t, uint seed = 0) {
#ifndef OS_MAC_OLD
		return qHash(QtPrivate::QHashCombine().operator()(qHash(t._lat), t._lon), seed);
#else // OS_MAC_OLD
		uint h1 = qHash(t._lat, seed);
		uint h2 = qHash(t._lon, seed);
		return ((h1 << 16) | (h1 >> 16)) ^ h2 ^ seed;
#endif // OS_MAC_OLD
	}

	float64 _lat = 0;
	float64 _lon = 0;
	uint64 _access = 0;

};

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
