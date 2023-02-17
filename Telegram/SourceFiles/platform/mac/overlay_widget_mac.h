/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "platform/platform_overlay_widget.h"

namespace Platform {

class MacOverlayWidgetHelper final : public OverlayWidgetHelper {
public:
	MacOverlayWidgetHelper(
		not_null<Ui::RpWindow*> window,
		Fn<void(bool)> maximize);
	~MacOverlayWidgetHelper();

	void beforeShow(bool fullscreen) override;
	void afterShow(bool fullscreen) override;
	void notifyFileDialogShown(bool shown) override;
	void minimize(not_null<Ui::RpWindow*> window) override;

private:
	struct Data;

	void activate(int button); // NSWindowButton
	void resolveNative();
	void updateStyles(bool fullscreen);
	void refreshButtons(bool fullscreen);

	std::unique_ptr<Data> _data;

};

} // namespace Platform
