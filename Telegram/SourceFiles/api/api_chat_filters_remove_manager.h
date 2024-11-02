/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Api {

class RemoveComplexChatFilter final {
public:
	RemoveComplexChatFilter();

	void request(
		QPointer<Ui::RpWidget> widget,
		base::weak_ptr<Window::SessionController> weak,
		FilterId id);

private:
	FilterId _removingId = 0;
	mtpRequestId _removingRequestId = 0;

};

} // namespace Api
