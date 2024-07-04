/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Core {

enum class GeoLocationAccuracy : uchar {
	Exact,
	Country,
	Failed,
};

struct GeoLocation {
	QPointF point;
	QRectF bounds;
	GeoLocationAccuracy accuracy = GeoLocationAccuracy::Failed;

	[[nodiscard]] bool exact() const {
		return accuracy == GeoLocationAccuracy::Exact;
	}
	[[nodiscard]] bool country() const {
		return accuracy == GeoLocationAccuracy::Country;
	}
	[[nodiscard]] bool failed() const {
		return accuracy == GeoLocationAccuracy::Failed;
	}

	explicit operator bool() const {
		return !failed();
	}
};

[[nodiscard]] GeoLocation ResolveCurrentCountryLocation();
void ResolveCurrentGeoLocation(Fn<void(GeoLocation)> callback);

} // namespace Core
