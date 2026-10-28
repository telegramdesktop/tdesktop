/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <optional>

namespace Data {

struct FileOrigin;

[[nodiscard]] bool ValidGeoCoordinates(float64 lat, float64 lon);

class LocationPoint {
public:
	LocationPoint() = default;

	[[nodiscard]] static std::optional<LocationPoint> FromMTP(
		const MTPGeoPoint &point);
	[[nodiscard]] static std::optional<LocationPoint> FromCoordinates(
		float64 lat,
		float64 lon,
		uint64 accessHash = 0);

	[[nodiscard]] QString latAsString() const;
	[[nodiscard]] QString lonAsString() const;
	[[nodiscard]] MTPGeoPoint toMTP() const;

	[[nodiscard]] float64 lat() const;
	[[nodiscard]] float64 lon() const;
	[[nodiscard]] uint64 accessHash() const;

	[[nodiscard]] size_t hash() const;

	friend inline bool operator==(
			const LocationPoint &a,
			const LocationPoint &b) {
		return (a._lat == b._lat) && (a._lon == b._lon);
	}

	friend inline bool operator<(
			const LocationPoint &a,
			const LocationPoint &b) {
		return (a._lat < b._lat) || ((a._lat == b._lat) && (a._lon < b._lon));
	}

private:
	LocationPoint(float64 lat, float64 lon, uint64 accessHash);

	float64 _lat = 0;
	float64 _lon = 0;
	uint64 _access = 0;

};

struct InputVenue {
	float64 lat = 0.;
	float64 lon = 0.;
	QString title;
	QString address;
	QString provider;
	QString id;
	QString venueType;

	[[nodiscard]] bool justLocation() const {
		return id.isEmpty();
	}

	friend inline bool operator==(
		const InputVenue &,
		const InputVenue &) = default;
};

[[nodiscard]] GeoPointLocation ComputeLocation(const LocationPoint &point);

} // namespace Data

namespace std {

template <>
struct hash<Data::LocationPoint> {
	size_t operator()(const Data::LocationPoint &value) const {
		return value.hash();
	}
};

} // namespace std
