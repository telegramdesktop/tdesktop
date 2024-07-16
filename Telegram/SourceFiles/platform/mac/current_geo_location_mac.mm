/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/mac/current_geo_location_mac.h"

#include "base/platform/mac/base_utilities_mac.h"
#include "core/current_geo_location.h"

#include <CoreLocation/CoreLocation.h>

@interface LocationDelegate : NSObject<CLLocationManagerDelegate>

- (id) initWithCallback:(Fn<void(Core::GeoLocation)>)callback;
- (void) locationManager:(CLLocationManager *)manager didUpdateLocations:(NSArray<CLLocation *> *)locations;
- (void) locationManager:(CLLocationManager *)manager didFailWithError:(NSError *)error;
- (void) locationManager:(CLLocationManager *) manager didChangeAuthorizationStatus:(CLAuthorizationStatus) status;
- (void) dealloc;

@end

@implementation LocationDelegate {
CLLocationManager *_manager;
Fn<void(Core::GeoLocation)> _callback;
}

- (void) fail {
	[_manager stopUpdatingLocation];

	const auto onstack = _callback;
	[self release];

	onstack({});
}

- (void) processWithStatus:(CLAuthorizationStatus)status {
	switch (status) {
	case kCLAuthorizationStatusNotDetermined:
		if (@available(macOS 10.15, *)) {
			[_manager requestWhenInUseAuthorization];
		} else {
			[_manager startUpdatingLocation];
		}
		break;
	case kCLAuthorizationStatusAuthorizedAlways:
		[_manager startUpdatingLocation];
		return;
	case kCLAuthorizationStatusRestricted:
	case kCLAuthorizationStatusDenied:
	default:
		[self fail];
		return;
	}
}

- (id) initWithCallback:(Fn<void(Core::GeoLocation)>)callback {
	if (self = [super init]) {
		_callback = std::move(callback);
		_manager = [[CLLocationManager alloc] init];
		_manager.desiredAccuracy = kCLLocationAccuracyThreeKilometers;
		_manager.delegate = self;
		if ([CLLocationManager locationServicesEnabled]) {
			if (@available(macOS 11, *)) {
				[self processWithStatus:[_manager authorizationStatus]];
			} else {
				[self processWithStatus:[CLLocationManager authorizationStatus]];
			}
		} else {
			[self fail];
		}
	}
	return self;
}

- (void) locationManager:(CLLocationManager *)manager didUpdateLocations:(NSArray<CLLocation*>*)locations {
	[_manager stopUpdatingLocation];

	auto result = Core::GeoLocation();
	if ([locations count] > 0) {
		const auto coordinate = [locations lastObject].coordinate;
		result.accuracy = Core::GeoLocationAccuracy::Exact;
		result.point = QPointF(coordinate.latitude, coordinate.longitude);
	}

	const auto onstack = _callback;
	[self release];

	onstack(result);
}

- (void) locationManager:(CLLocationManager *)manager didFailWithError:(NSError *)error {
	if (error.code != kCLErrorLocationUnknown) {
		[self fail];
	}
}

- (void) locationManager:(CLLocationManager *) manager didChangeAuthorizationStatus:(CLAuthorizationStatus) status {
	[self processWithStatus:status];
}

- (void) dealloc {
	if (_manager) {
		_manager.delegate = nil;
		[_manager release];
	}
	[super dealloc];
}

@end

namespace Platform {

void ResolveCurrentExactLocation(Fn<void(Core::GeoLocation)> callback) {
	[[LocationDelegate alloc] initWithCallback:std::move(callback)];
}

void ResolveLocationAddress(
		const Core::GeoLocation &location,
		const QString &language,
		Fn<void(Core::GeoAddress)> callback) {
	CLGeocoder *geocoder = [[CLGeocoder alloc] init];
	CLLocation *request = [[CLLocation alloc]
		initWithLatitude:location.point.x()
		longitude:location.point.y()];
	[geocoder reverseGeocodeLocation:request completionHandler:^(
			NSArray<CLPlacemark*> * __nullable placemarks,
			NSError * __nullable error) {
		if (placemarks && [placemarks count] > 0) {
			CLPlacemark *placemark = [placemarks firstObject];
			auto list = QStringList();
			const auto push = [&](NSString *text) {
				if (text) {
					const auto qt = NS2QString(text);
					if (!qt.isEmpty()) {
						list.push_back(qt);
					}
				}
			};
			push([placemark thoroughfare]);
			push([placemark locality]);
			push([placemark country]);
			callback({ .name = list.join(u", "_q) });
		} else {
			callback({});
		}
		[geocoder release];
	}];
	[request release];
}

} // namespace Platform
