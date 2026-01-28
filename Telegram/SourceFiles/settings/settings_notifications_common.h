/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <cstdint>

#include "base/object_ptr.h"
#include "core/core_settings.h"
#include "settings/settings_type.h"
#include "ui/effects/animations.h"
#include "ui/rp_widget.h"

namespace Ui {
class Checkbox;
class SettingsButton;
class VerticalLayout;
template <typename Widget>
class SlideWrap;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Data {
enum class DefaultNotify : uint8_t;
} // namespace Data

namespace Settings {

constexpr auto kMaxNotificationsCount = 5;

[[nodiscard]] int CurrentNotificationsCount();

class NotificationsCount : public Ui::RpWidget {
public:
	NotificationsCount(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	void setCount(int count);

	~NotificationsCount();

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

	int resizeGetHeight(int newWidth) override;

private:
	using ScreenCorner = Core::Settings::ScreenCorner;
	void setOverCorner(ScreenCorner corner);
	void clearOverCorner();

	class SampleWidget;
	void removeSample(SampleWidget *widget);

	QRect getScreenRect() const;
	QRect getScreenRect(int width) const;
	int getContentLeft() const;
	void prepareNotificationSampleSmall();
	void prepareNotificationSampleLarge();
	void prepareNotificationSampleUserpic();

	const not_null<Window::SessionController*> _controller;

	QPixmap _notificationSampleUserpic;
	QPixmap _notificationSampleSmall;
	QPixmap _notificationSampleLarge;
	ScreenCorner _chosenCorner;
	std::vector<Ui::Animations::Simple> _sampleOpacities;

	bool _isOverCorner = false;
	ScreenCorner _overCorner = ScreenCorner::TopLeft;
	bool _isDownCorner = false;
	ScreenCorner _downCorner = ScreenCorner::TopLeft;

	int _oldCount;

	std::vector<SampleWidget*> _cornerSamples[4];

};

struct NotifyViewCheckboxes {
	not_null<Ui::SlideWrap<Ui::RpWidget>*> wrap;
	not_null<Ui::Checkbox*> name;
	not_null<Ui::Checkbox*> preview;
};

[[nodiscard]] NotifyViewCheckboxes SetupNotifyViewOptions(
	not_null<Window::SessionController*> controller,
	not_null<Ui::VerticalLayout*> container,
	bool nameShown,
	bool previewShown);

[[nodiscard]] not_null<Ui::SettingsButton*> AddTypeButton(
	not_null<Ui::VerticalLayout*> container,
	not_null<Window::SessionController*> controller,
	Data::DefaultNotify type,
	Fn<void(Type)> showOther);

} // namespace Settings
