/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/invoke_queued.h"
#include "base/weak_ptr.h"
#include "webview/webview_common.h"

namespace Webview {
class Window;
} // namespace Webview

namespace Ui {

class SeparatePanel;
class RpWidget;

struct LocationInfo {
	float64 lat = 0.;
	float64 lon = 0.;
};

class LocationPicker final : public base::has_weak_ptr {
public:
	struct Descriptor {
		RpWidget *parent = nullptr;
		Fn<void(LocationInfo)> callback;
		Fn<void()> quit;
		Webview::StorageId storageId;
		rpl::producer<> closeRequests;
	};

	[[nodiscard]] static bool Available(const QString &token);
	static not_null<LocationPicker*> Show(Descriptor &&descriptor);

	void close();
	void minimize();
	void quit();

private:
	explicit LocationPicker(Descriptor &&descriptor);

	void setup(const Descriptor &descriptor);
	void setupWindow(const Descriptor &descriptor);
	void setupWebview(const Descriptor &descriptor);
	void processKey(const QString &key, const QString &modifier);
	void resolveCurrentLocation();
	void initMap();

	rpl::lifetime _lifetime;

	Fn<void(LocationInfo)> _callback;
	Fn<void()> _quit;
	std::unique_ptr<SeparatePanel> _window;
	not_null<RpWidget*> _body;
	RpWidget *_container = nullptr;
	std::unique_ptr<Webview::Window> _webview;
	SingleQueuedInvokation _updateStyles;
	bool _subscribedToColors = false;

};

} // namespace Ui
