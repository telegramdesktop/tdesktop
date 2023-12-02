/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/invoke_queued.h"

namespace Webview {
struct DataRequest;
class Window;
} // namespace Webview

namespace Ui {
class RpWidget;
class RpWindow;
} // namespace Ui

namespace Iv {

struct Prepared;

class Controller final {
public:
	Controller();
	~Controller();

	enum class Event {
		Close,
		Quit,
	};

	void show(const QString &dataPath, Prepared page);
	[[nodiscard]] bool active() const;
	void minimize();

	[[nodiscard]] rpl::producer<Webview::DataRequest> dataRequests() const {
		return _dataRequests.events();
	}

	[[nodiscard]] rpl::producer<Event> events() const {
		return _events.events();
	}

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	void createWindow();
	void showInWindow(const QString &dataPath, Prepared page);

	void escape();
	void close();
	void quit();

	std::unique_ptr<Ui::RpWindow> _window;
	Ui::RpWidget *_container = nullptr;
	std::unique_ptr<Webview::Window> _webview;
	rpl::event_stream<Webview::DataRequest> _dataRequests;
	rpl::event_stream<Event> _events;
	SingleQueuedInvokation _updateStyles;
	bool _subscribedToColors = false;

	rpl::lifetime _lifetime;

};

} // namespace Iv
