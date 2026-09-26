/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "core/core_settings.h"

namespace Ui {
class RpWindow;
class RpWidget;
class InfiniteRadialAnimation;
} // namespace Ui

namespace Window {

class RestoreShell final {
public:
	RestoreShell(const QString &title, Core::WindowPosition position);
	~RestoreShell();

	void showUnavailable();
	void close();
	void activate();
	[[nodiscard]] bool isActiveWindow() const;
	[[nodiscard]] Core::WindowPosition countPositionForSave() const;
	[[nodiscard]] rpl::producer<> closeRequests() const;
	[[nodiscard]] rpl::lifetime &lifetime();

private:
	void setupBody();
	void applyPosition(Core::WindowPosition position);

	base::unique_qptr<Ui::RpWindow> _window;
	Ui::RpWidget *_content = nullptr;
	std::unique_ptr<Ui::InfiniteRadialAnimation> _loading;
	rpl::event_stream<> _closeRequests;
	rpl::lifetime _lifetime;
	bool _unavailable = false;

};

} // namespace Window
