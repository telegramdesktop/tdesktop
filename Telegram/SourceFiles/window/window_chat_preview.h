/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "base/unique_qptr.h"
#include "dialogs/dialogs_key.h"

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Window {

class SessionController;

class ChatPreviewManager final {
public:
	ChatPreviewManager(not_null<SessionController*> controller);

	bool show(
		Dialogs::RowDescriptor row,
		Fn<void(bool shown)> callback = nullptr,
		QPointer<QWidget> parentOverride = nullptr,
		std::optional<QPoint> positionOverride = {});
	bool schedule(
		Dialogs::RowDescriptor row,
		Fn<void(bool shown)> callback = nullptr,
		QPointer<QWidget> parentOverride = nullptr,
		std::optional<QPoint> positionOverride = {});
	void cancelScheduled();

private:
	void showScheduled();

	const not_null<SessionController*> _controller;
	Dialogs::RowDescriptor _scheduled;
	Fn<void(bool)> _scheduledCallback;
	QPointer<QWidget> _scheduledParentOverride;
	std::optional<QPoint> _scheduledPositionOverride;
	base::Timer _timer;

	rpl::lifetime _topicLifetime;

	base::unique_qptr<Ui::PopupMenu> _menu;

};

} // namespace Window