/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Webview {
struct DataRequest;
class Window;
} // namespace Webview

namespace Ui {
class RpWindow;
} // namespace Ui

namespace Iv {

struct Prepared;

class Controller final {
public:
	Controller();
	~Controller();

	void show(const QString &dataPath, Prepared page);

	[[nodiscard]] rpl::producer<Webview::DataRequest> dataRequests() const;

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	std::unique_ptr<Ui::RpWindow> _window;
	std::unique_ptr<Webview::Window> _webview;
	rpl::event_stream<Webview::DataRequest> _dataRequests;
	rpl::lifetime _lifetime;

};

} // namespace Iv
