/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/win/current_geo_location_win.h"

#include "base/platform/win/base_windows_winrt.h"
#include "core/current_geo_location.h"

#include <winrt/Windows.Devices.Geolocation.h>
#include <winrt/Windows.Foundation.h>

#include <winrt/Windows.Services.Maps.h>
#include <winrt/Windows.Foundation.Collections.h>

namespace Platform {

void ResolveCurrentExactLocation(Fn<void(Core::GeoLocation)> callback) {
	using namespace winrt::Windows::Foundation;
	using namespace winrt::Windows::Devices::Geolocation;

	const auto success = base::WinRT::Try([&] {
		Geolocator geolocator;
		geolocator.DesiredAccuracy(PositionAccuracy::High);
		if (geolocator.LocationStatus() == PositionStatus::NotAvailable) {
			callback({});
			return;
		}
		geolocator.GetGeopositionAsync().Completed([=](
				IAsyncOperation<Geoposition> that,
				AsyncStatus status) {
			if (status != AsyncStatus::Completed) {
				crl::on_main([=] {
					callback({});
				});
				return;
			}
			const auto point = base::WinRT::Try([&] {
				const auto coordinate = that.GetResults().Coordinate();
				return coordinate.Point().Position();
			});
			crl::on_main([=] {
				if (!point) {
					callback({});
				} else {
					callback({
						.point = { point->Latitude, point->Longitude },
						.accuracy = Core::GeoLocationAccuracy::Exact,
					});
				}
			});
		});
	});
	if (!success) {
		callback({});
	}
}

void ResolveLocationAddress(
		const Core::GeoLocation &location,
		const QString &language,
		Fn<void(Core::GeoAddress)> callback) {
	callback({});
}

} // namespace Platform
