/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "webauthn/cable_scanner.h"

#include "webauthn/cable_core.h"

#include <crl/crl.h>

#import <CoreBluetooth/CoreBluetooth.h>

#include <set>

namespace Platform::WebAuthn::Cable {
namespace {

struct State {
	std::function<void(QByteArray)> onAdvert;
	std::function<void()> onUnavailable;
	std::set<QByteArray> seen;
	bool stopped = false;
};

[[nodiscard]] std::vector<QByteArray> CableServiceData(
		NSDictionary<NSString*, id> *advertisementData) {
	auto result = std::vector<QByteArray>();
	NSDictionary<CBUUID*, NSData*> *serviceData
		= advertisementData[CBAdvertisementDataServiceDataKey];
	if (!serviceData) {
		return result;
	}
	CBUUID *google = [CBUUID UUIDWithString:@"FDE2"];
	CBUUID *fido = [CBUUID UUIDWithString:@"FFF9"];
	for (CBUUID *uuid in serviceData) {
		if (![uuid isEqual:google] && ![uuid isEqual:fido]) {
			continue;
		}
		NSData *data = serviceData[uuid];
		if (!data || data.length < kAdvertSize) {
			continue;
		}
		result.emplace_back(
			reinterpret_cast<const char*>(data.bytes),
			int(data.length));
	}
	return result;
}

} // namespace
} // namespace Platform::WebAuthn::Cable

@interface TDCableScannerDelegate : NSObject<CBCentralManagerDelegate>
- (instancetype)initWithState:
	(std::shared_ptr<Platform::WebAuthn::Cable::State>)state;
@end

@implementation TDCableScannerDelegate {
	std::weak_ptr<Platform::WebAuthn::Cable::State> _state;
}

- (instancetype)initWithState:
		(std::shared_ptr<Platform::WebAuthn::Cable::State>)state {
	if (self = [super init]) {
		_state = state;
	}
	return self;
}

- (void)centralManagerDidUpdateState:(CBCentralManager *)central {
	if (central.state == CBManagerStatePoweredOn) {
		[central
			scanForPeripheralsWithServices:nil
			options:@{ CBCentralManagerScanOptionAllowDuplicatesKey: @YES }];
		return;
	} else if (central.state == CBManagerStateUnknown
		|| central.state == CBManagerStateResetting) {
		return;
	}
	crl::on_main([weak = _state] {
		const auto state = weak.lock();
		if (state && !state->stopped && state->onUnavailable) {
			state->onUnavailable();
		}
	});
}

- (void)centralManager:(CBCentralManager *)central
		didDiscoverPeripheral:(CBPeripheral *)peripheral
		advertisementData:(NSDictionary<NSString *, id> *)advertisementData
		RSSI:(NSNumber *)RSSI {
	auto blobs = Platform::WebAuthn::Cable::CableServiceData(advertisementData);
	if (blobs.empty()) {
		return;
	}
	crl::on_main([weak = _state, blobs = std::move(blobs)] {
		const auto state = weak.lock();
		if (!state || state->stopped) {
			return;
		}
		for (const auto &blob : blobs) {
			if (state->seen.emplace(blob).second && state->onAdvert) {
				state->onAdvert(blob);
			}
		}
	});
}

@end

namespace Platform::WebAuthn::Cable {
namespace {

class MacBleScanner final : public BleScanner {
public:
	~MacBleScanner();

	bool start(
		std::function<void(QByteArray)> onAdvert,
		std::function<void()> onUnavailable) override;
	void stop() override;

private:
	std::shared_ptr<State> _state;
	CBCentralManager *_central = nil;
	TDCableScannerDelegate *_delegate = nil;

};

MacBleScanner::~MacBleScanner() {
	stop();
}

bool MacBleScanner::start(
		std::function<void(QByteArray)> onAdvert,
		std::function<void()> onUnavailable) {
	_state = std::make_shared<State>();
	_state->onAdvert = std::move(onAdvert);
	_state->onUnavailable = std::move(onUnavailable);
	_delegate = [[TDCableScannerDelegate alloc] initWithState:_state];
	_central = [[CBCentralManager alloc]
		initWithDelegate:_delegate
		queue:nil];
	return _central != nil;
}

void MacBleScanner::stop() {
	if (_state) {
		_state->stopped = true;
	}
	if (_central) {
		[_central stopScan];
		_central.delegate = nil;
		[_central release];
		_central = nil;
	}
	if (_delegate) {
		[_delegate release];
		_delegate = nil;
	}
	_state = nullptr;
}

} // namespace

std::unique_ptr<BleScanner> MakeBleScanner() {
	return std::make_unique<MacBleScanner>();
}

} // namespace Platform::WebAuthn::Cable
