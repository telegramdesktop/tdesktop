/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_notifications_widget.h"

#include "styles/style_settings.h"
#include "lang/lang_keys.h"
#include "storage/localstorage.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "mainwindow.h"
#include "window/notifications_manager.h"
#include "boxes/notifications_box.h"
#include "platform/platform_notifications_manager.h"
#include "auth_session.h"

namespace Settings {
namespace {

using ChangeType = Window::Notifications::ChangeType;

} // namespace

NotificationsWidget::NotificationsWidget(QWidget *parent, UserData *self) : BlockWidget(parent, self, lang(lng_settings_section_notify)) {
	createControls();

	subscribe(Auth().notifications().settingsChanged(), [this](ChangeType type) {
		if (type == ChangeType::DesktopEnabled) {
			desktopEnabledUpdated();
		} else if (type == ChangeType::ViewParams) {
			viewParamUpdated();
		} else if (type == ChangeType::SoundEnabled) {
			_playSound->setChecked(Global::SoundNotify());
		}
	});
}

void NotificationsWidget::createControls() {
	style::margins margin(0, 0, 0, st::settingsSkip);
	style::margins slidedPadding(0, margin.bottom() / 2, 0, margin.bottom() - (margin.bottom() / 2));
	createChildRow(_desktopNotifications, margin, lang(lng_settings_desktop_notify), [this](bool) { onDesktopNotifications(); }, Global::DesktopNotify());
	createChildRow(_showSenderName, margin, slidedPadding, lang(lng_settings_show_name), [this](bool) { onShowSenderName(); }, Global::NotifyView() <= dbinvShowName);
	createChildRow(_showMessagePreview, margin, slidedPadding, lang(lng_settings_show_preview), [this](bool) { onShowMessagePreview(); }, Global::NotifyView() <= dbinvShowPreview);
	if (!_showSenderName->entity()->checked()) {
		_showMessagePreview->hide(anim::type::instant);
	}
	if (!_desktopNotifications->checked()) {
		_showSenderName->hide(anim::type::instant);
		_showMessagePreview->hide(anim::type::instant);
	}
	createChildRow(_playSound, margin, lang(lng_settings_sound_notify), [this](bool) { onPlaySound(); }, Global::SoundNotify());
	createChildRow(_includeMuted, margin, lang(lng_settings_include_muted), [this](bool) { onIncludeMuted(); }, Global::IncludeMuted());

	if (cPlatform() != dbipMac) {
		createNotificationsControls();
	}
}

void NotificationsWidget::createNotificationsControls() {
	style::margins margin(0, 0, 0, st::settingsSkip);
	style::margins slidedPadding(0, margin.bottom() / 2, 0, margin.bottom() - (margin.bottom() / 2));

	auto nativeNotificationsLabel = QString();
	if (Platform::Notifications::Supported()) {
#ifdef Q_OS_WIN
		nativeNotificationsLabel = lang(lng_settings_use_windows);
#elif defined Q_OS_LINUX64 || defined Q_OS_LINUX32 // Q_OS_WIN
		nativeNotificationsLabel = lang(lng_settings_use_native_notifications);
#endif // Q_OS_WIN || Q_OS_LINUX64 || Q_OS_LINUX32
	}
	if (!nativeNotificationsLabel.isEmpty()) {
		createChildRow(_nativeNotifications, margin, nativeNotificationsLabel, [this](bool) { onNativeNotifications(); }, Global::NativeNotifications());
	}
	createChildRow(_advanced, margin, slidedPadding, lang(lng_settings_advanced_notifications), SLOT(onAdvanced()));
	if (!nativeNotificationsLabel.isEmpty() && Global::NativeNotifications()) {
		_advanced->hide(anim::type::instant);
	}
}

void NotificationsWidget::onDesktopNotifications() {
	if (Global::DesktopNotify() == _desktopNotifications->checked()) {
		return;
	}
	Global::SetDesktopNotify(_desktopNotifications->checked());
	Local::writeUserSettings();
	Auth().notifications().settingsChanged().notify(ChangeType::DesktopEnabled);
}

void NotificationsWidget::desktopEnabledUpdated() {
	_desktopNotifications->setChecked(Global::DesktopNotify());
	_showSenderName->toggle(
		Global::DesktopNotify(),
		anim::type::normal);
	_showMessagePreview->toggle(
		Global::DesktopNotify()
			&& _showSenderName->entity()->checked(),
		anim::type::normal);
}

void NotificationsWidget::onShowSenderName() {
	auto viewParam = ([this]() {
		if (!_showSenderName->entity()->checked()) {
			return dbinvShowNothing;
		} else if (!_showMessagePreview->entity()->checked()) {
			return dbinvShowName;
		}
		return dbinvShowPreview;
	})();
	if (viewParam == Global::NotifyView()) {
		return;
	}
	Global::SetNotifyView(viewParam);
	Local::writeUserSettings();
	Auth().notifications().settingsChanged().notify(ChangeType::ViewParams);
}

void NotificationsWidget::onShowMessagePreview() {
	auto viewParam = ([this]() {
		if (_showMessagePreview->entity()->checked()) {
			return dbinvShowPreview;
		} else if (_showSenderName->entity()->checked()) {
			return dbinvShowName;
		}
		return dbinvShowNothing;
	})();
	if (viewParam == Global::NotifyView()) {
		return;
	}

	Global::SetNotifyView(viewParam);
	Local::writeUserSettings();
	Auth().notifications().settingsChanged().notify(ChangeType::ViewParams);
}

void NotificationsWidget::viewParamUpdated() {
	_showMessagePreview->toggle(
		_showSenderName->entity()->checked(),
		anim::type::normal);
}

void NotificationsWidget::onNativeNotifications() {
	if (Global::NativeNotifications() == _nativeNotifications->checked()) {
		return;
	}

	Global::SetNativeNotifications(_nativeNotifications->checked());
	Local::writeUserSettings();

	Auth().notifications().createManager();

	_advanced->toggle(
		!Global::NativeNotifications(),
		anim::type::normal);
}

void NotificationsWidget::onAdvanced() {
	Ui::show(Box<NotificationsBox>());
}

void NotificationsWidget::onPlaySound() {
	if (_playSound->checked() == Global::SoundNotify()) {
		return;
	}

	Global::SetSoundNotify(_playSound->checked());
	Local::writeUserSettings();
	Auth().notifications().settingsChanged().notify(ChangeType::SoundEnabled);
}

void NotificationsWidget::onIncludeMuted() {
	Global::SetIncludeMuted(_includeMuted->checked());
	Local::writeUserSettings();
	Auth().notifications().settingsChanged().notify(ChangeType::IncludeMuted);
}

} // namespace Settings
