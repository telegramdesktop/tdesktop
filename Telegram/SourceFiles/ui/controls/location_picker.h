/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/invoke_queued.h"
#include "base/timer.h"
#include "base/weak_ptr.h"
#include "core/current_geo_location.h"
#include "webview/webview_common.h"

namespace Data {
class LocationPoint;
} // namespace Data

namespace Webview {
class Window;
} // namespace Webview

namespace Ui {

class AbstractButton;
class SeparatePanel;
class RpWidget;

struct LocationPickerConfig {
	QString mapsToken;
	QString geoToken;
};

class LocationPicker final : public base::has_weak_ptr {
public:
	struct Descriptor {
		RpWidget *parent = nullptr;
		LocationPickerConfig config;
		rpl::producer<QString> chooseLabel;
		Core::GeoLocation initial;
		Fn<void(Data::LocationPoint point, QString address)> callback;
		Fn<void()> quit;
		Webview::StorageId storageId;
		rpl::producer<> closeRequests;
	};

	[[nodiscard]] static bool Available(const LocationPickerConfig &config);
	static not_null<LocationPicker*> Show(Descriptor &&descriptor);

	void activate();
	void close();
	void minimize();
	void quit();

private:
	explicit LocationPicker(Descriptor &&descriptor);

	void setup(const Descriptor &descriptor);
	void setupWindow(const Descriptor &descriptor);
	void setupWebview();
	void showWebviewError();
	void processKey(const QString &key, const QString &modifier);
	void resolveCurrentLocation();
	void resolveAddressByTimer();
	void resolveAddress(Core::GeoLocation location);
	void mapReady();

	LocationPickerConfig _config;
	Fn<void(Data::LocationPoint, QString)> _callback;
	Fn<void()> _quit;
	std::unique_ptr<SeparatePanel> _window;
	not_null<RpWidget*> _body;
	RpWidget *_container = nullptr;
	RpWidget *_mapPlaceholder = nullptr;
	RpWidget *_mapLoading = nullptr;
	AbstractButton *_mapButton = nullptr;
	rpl::variable<QString> _chooseButtonLabel;
	Webview::StorageId _webviewStorageId;
	std::unique_ptr<Webview::Window> _webview;
	SingleQueuedInvokation _updateStyles;
	Core::GeoLocation _initialProvided;
	bool _subscribedToColors = false;

	base::Timer _geocoderResolveTimer;
	Core::GeoLocation _geocoderResolvePostponed;
	Core::GeoLocation _geocoderResolvingFor;
	QString _geocoderSavedAddress;
	rpl::variable<QString> _geocoderAddress;

	rpl::lifetime _lifetime;

};

} // namespace Ui
