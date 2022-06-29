/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "styles/style_widgets.h"

namespace Ui {
class Show;
} // namespace Ui

namespace Ui::Toast {
class Instance;
} // namespace Ui::Toast

namespace Window {
class SessionController;
} // namespace Window

namespace HistoryView {

class StickerToast final {
public:
	StickerToast(
		not_null<Window::SessionController*> controller,
		not_null<QWidget*> parent,
		Fn<void()> destroy);
	~StickerToast();

	void showFor(not_null<DocumentData*> document);

private:
	void requestSet();
	void cancelRequest();
	void showWithTitle(const QString &title);
	[[nodiscard]] QString lookupTitle() const;

	const not_null<Window::SessionController*> _controller;
	const not_null<QWidget*> _parent;
	style::Toast _st;
	base::weak_ptr<Ui::Toast::Instance> _weak;
	DocumentData *_for = nullptr;
	Fn<void()> _destroy;

	mtpRequestId _setRequestId = 0;

};

} // namespace HistoryView
