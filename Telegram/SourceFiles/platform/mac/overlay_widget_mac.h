/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "platform/platform_overlay_widget.h"

template <typename Object>
class object_ptr;

namespace Ui {
class AbstractButton;
} // namespace Ui

namespace Ui::Platform {
enum class TitleControl;
} // namespace Ui::Platform

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
	void clearState() override;
	void setControlsOpacity(float64 opacity) override;
	rpl::producer<bool> controlsSideRightValue() override;
	rpl::producer<int> topNotchSkipValue() override;

private:
	using Control = Ui::Platform::TitleControl;
	struct Data;

	void activate(Control control);
	void resolveNative();
	void updateStyles(bool fullscreen);
	void refreshButtons(bool fullscreen);

	object_ptr<Ui::AbstractButton> create(
		not_null<QWidget*> parent,
		Control control);

	std::unique_ptr<Data> _data;

};

} // namespace Platform
