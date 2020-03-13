/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_chat_filters.h"

class ApiWrap;

namespace Ui {
class GenericBox;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

class ManageFiltersPrepare final {
public:
	explicit ManageFiltersPrepare(
		not_null<Window::SessionController*> window);
	~ManageFiltersPrepare();

	void showBox();

private:
	struct Suggested {
		Data::ChatFilter filter;
		QString description;
	};

	void showBoxWithSuggested();
	static void SetupBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> window,
		const std::vector<Suggested> &suggested);
	static void EditBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> window,
		const Data::ChatFilter &filter,
		Fn<void(const Data::ChatFilter &)> doneCallback);

	const not_null<Window::SessionController*> _window;
	const not_null<ApiWrap*> _api;

	mtpRequestId _requestId = 0;
	std::vector<Suggested> _suggested;
	crl::time _suggestedLastReceived = 0;

};
