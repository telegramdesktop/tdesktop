/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_general_widget.h"

#include "styles/style_settings.h"
#include "lang/lang_keys.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "storage/localstorage.h"
#include "platform/platform_specific.h"
#include "mainwindow.h"
#include "application.h"
#include "boxes/language_box.h"
#include "boxes/confirm_box.h"
#include "boxes/about_box.h"
#include "core/file_utilities.h"
#include "lang/lang_file_parser.h"
#include "lang/lang_cloud_manager.h"
#include "messenger.h"
#include "core/update_checker.h"

namespace Settings {

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
UpdateStateRow::UpdateStateRow(QWidget *parent) : RpWidget(parent)
, _check(this, lang(lng_settings_check_now))
, _restart(this, lang(lng_settings_update_now)) {
	connect(_check, SIGNAL(clicked()), this, SLOT(onCheck()));
	connect(_restart, SIGNAL(clicked()), this, SIGNAL(restart()));

	_versionText = lng_settings_current_version_label(lt_version, currentVersionText());

	Core::UpdateChecker checker;
	checker.checking() | rpl::start_with_next([=] {
		onChecking();
	}, lifetime());
	checker.isLatest() | rpl::start_with_next([=] {
		onLatest();
	}, lifetime());
	checker.progress(
	) | rpl::start_with_next([=](Core::UpdateChecker::Progress progress) {
		onDownloading(progress.already, progress.size);
	}, lifetime());
	checker.failed() | rpl::start_with_next([=] {
		onFailed();
	}, lifetime());
	checker.ready() | rpl::start_with_next([=] {
		onReady();
	}, lifetime());

	switch (checker.state()) {
	case Core::UpdateChecker::State::Download:
		setState(State::Download, true);
		setDownloadProgress(checker.already(), checker.size());
		break;
	case Core::UpdateChecker::State::Ready:
		setState(State::Ready, true);
		break;
	default:
		setState(State::None, true);
		break;
	}
}

int UpdateStateRow::resizeGetHeight(int newWidth) {
	auto labelWidth = [](const QString &label) {
		return st::linkFont->width(label) + st::linkFont->spacew;
	};
	auto checkLeft = (_state == State::Latest) ? labelWidth(lang(lng_settings_latest_installed)) : labelWidth(_versionText);
	auto restartLeft = labelWidth(lang(lng_settings_update_ready));

	_check->resizeToWidth(qMin(newWidth, _check->naturalWidth()));
	_check->moveToLeft(checkLeft, 0, newWidth);

	_restart->resizeToWidth(qMin(newWidth, _restart->naturalWidth()));
	_restart->moveToLeft(restartLeft, 0, newWidth);

	return _check->height();
}

void UpdateStateRow::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto text = ([this]() -> QString {
		switch (_state) {
		case State::Check: return lang(lng_settings_update_checking);
		case State::Latest: return lang(lng_settings_latest_installed);
		case State::Download: return _downloadText;
		case State::Ready: return lang(lng_settings_update_ready);
		case State::Fail: return lang(lng_settings_update_fail);
		default: return _versionText;
		}
	})();
	p.setFont(st::linkFont);
	p.setPen((_state == State::None) ? st::windowFg : st::settingsUpdateFg);
	p.drawTextLeft(0, 0, width(), text);
}

void UpdateStateRow::onCheck() {
	if (!cAutoUpdate()) return;

	Core::UpdateChecker checker;

	setState(State::Check);
	cSetLastUpdateCheck(0);
	checker.start();
}

void UpdateStateRow::setState(State state, bool force) {
	if (_state != state || force) {
		_state = state;
		switch (state) {
		case State::None: _check->show(); _restart->hide(); break;
		case State::Ready: _check->hide(); _restart->show(); break;
		case State::Check:
		case State::Download:
		case State::Latest:
		case State::Fail: _check->hide(); _restart->hide(); break;
		}
		resizeToWidth(width());
		sendSynteticMouseEvent(this, QEvent::MouseMove, Qt::NoButton);
		update();
	}
}

void UpdateStateRow::setDownloadProgress(qint64 ready, qint64 total) {
	auto readyTenthMb = (ready * 10 / (1024 * 1024)), totalTenthMb = (total * 10 / (1024 * 1024));
	auto readyStr = QString::number(readyTenthMb / 10) + '.' + QString::number(readyTenthMb % 10);
	auto totalStr = QString::number(totalTenthMb / 10) + '.' + QString::number(totalTenthMb % 10);
	auto result = lng_settings_downloading(lt_ready, readyStr, lt_total, totalStr);
	if (_downloadText != result) {
		_downloadText = result;
		update();
	}
}

void UpdateStateRow::onChecking() {
	setState(State::Check);
}

void UpdateStateRow::onLatest() {
	setState(State::Latest);
}

void UpdateStateRow::onDownloading(qint64 ready, qint64 total) {
	setState(State::Download);
	setDownloadProgress(ready, total);
}

void UpdateStateRow::onReady() {
	setState(State::Ready);
}

void UpdateStateRow::onFailed() {
	setState(State::Fail);
}
#endif // !TDESKTOP_DISABLE_AUTOUPDATE

GeneralWidget::GeneralWidget(QWidget *parent, UserData *self) : BlockWidget(parent, self, lang(lng_settings_section_general))
, _changeLanguage(this, lang(lng_settings_change_lang), st::boxLinkButton) {
	connect(_changeLanguage, SIGNAL(clicked()), this, SLOT(onChangeLanguage()));
	refreshControls();
}

int GeneralWidget::getUpdateTop() const {
#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	return 0; // _updateRow->y(); // Just scroll to the top of the whole General widget
#else // !TDESKTOP_DISABLE_AUTOUPDATE
	return -1;
#endif // !TDESKTOP_DISABLE_AUTOUPDATE
}

int GeneralWidget::resizeGetHeight(int newWidth) {
	_changeLanguage->moveToRight(0, st::settingsBlockMarginTop + st::settingsBlockTitleTop + st::settingsBlockTitleFont->ascent - st::defaultLinkButton.font->ascent, newWidth);
	return BlockWidget::resizeGetHeight(newWidth);
}

void GeneralWidget::refreshControls() {
	style::margins marginSub(0, 0, 0, st::settingsSubSkip);
	style::margins marginLarge(0, 0, 0, st::settingsLargeSkip);
	style::margins marginSmall(0, 0, 0, st::settingsSmallSkip);
	style::margins slidedPadding(0, marginSmall.bottom() / 2, 0, marginSmall.bottom() - (marginSmall.bottom() / 2));

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	createChildRow(_updateAutomatically, marginSub, lang(lng_settings_update_automatically), [this](bool) { onUpdateAutomatically(); }, cAutoUpdate());
	style::margins marginLink(st::defaultCheck.diameter + st::defaultBoxCheckbox.textPosition.x(), 0, 0, st::settingsSkip);
	createChildRow(_updateRow, marginLink, slidedPadding);
	connect(_updateRow->entity(), SIGNAL(restart()), this, SLOT(onRestart()));
	if (!cAutoUpdate()) {
		_updateRow->hide(anim::type::instant);
	}
#endif // !TDESKTOP_DISABLE_AUTOUPDATE

	if (cPlatform() == dbipWindows || cSupportTray()) {
		auto workMode = Global::WorkMode().value();
		createChildRow(_enableTrayIcon, marginSmall, lang(lng_settings_workmode_tray), [this](bool) { onEnableTrayIcon(); }, (workMode == dbiwmTrayOnly || workMode == dbiwmWindowAndTray));
		if (cPlatform() == dbipWindows) {
			createChildRow(_enableTaskbarIcon, marginLarge, lang(lng_settings_workmode_window), [this](bool) { onEnableTaskbarIcon(); }, (workMode == dbiwmWindowOnly || workMode == dbiwmWindowAndTray));

#ifndef OS_WIN_STORE
			createChildRow(_autoStart, marginSmall, lang(lng_settings_auto_start), [this](bool) { onAutoStart(); }, cAutoStart());
			createChildRow(_startMinimized, marginLarge, slidedPadding, lang(lng_settings_start_min), [this](bool) { onStartMinimized(); }, (cStartMinimized() && !Global::LocalPasscode()));
			subscribe(Global::RefLocalPasscodeChanged(), [this] {
				_startMinimized->entity()->setChecked(cStartMinimized() && !Global::LocalPasscode());
			});
			if (!cAutoStart()) {
				_startMinimized->hide(anim::type::instant);
			}
			createChildRow(_addInSendTo, marginSmall, lang(lng_settings_add_sendto), [this](bool) { onAddInSendTo(); }, cSendToMenu());
#endif // OS_WIN_STORE
		}
	}
}

void GeneralWidget::onChangeLanguage() {
	if ((_changeLanguage->clickModifiers() & Qt::ShiftModifier) && (_changeLanguage->clickModifiers() & Qt::AltModifier)) {
		Lang::CurrentCloudManager().switchToLanguage(qsl("custom"));
		return;
	}
	auto manager = Messenger::Instance().langCloudManager();
	if (manager->languageList().isEmpty()) {
		_languagesLoadedSubscription = subscribe(manager->languageListChanged(), [this] {
			unsubscribe(base::take(_languagesLoadedSubscription));
			Ui::show(Box<LanguageBox>());
		});
	} else {
		unsubscribe(base::take(_languagesLoadedSubscription));
		Ui::show(Box<LanguageBox>());
	}
	manager->requestLanguageList();
}

void GeneralWidget::onRestart() {
#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	Core::checkReadyUpdate();
#endif // !TDESKTOP_DISABLE_AUTOUPDATE
	App::restart();
}

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
void GeneralWidget::onUpdateAutomatically() {
	cSetAutoUpdate(_updateAutomatically->checked());
	Local::writeSettings();
	_updateRow->toggle(
		cAutoUpdate(),
		anim::type::normal);
	Core::UpdateChecker checker;
	if (cAutoUpdate()) {
		checker.start();
	} else {
		checker.stop();
	}
}
#endif // !TDESKTOP_DISABLE_AUTOUPDATE

void GeneralWidget::onEnableTrayIcon() {
	if ((!_enableTrayIcon->checked() || cPlatform() != dbipWindows) && _enableTaskbarIcon && !_enableTaskbarIcon->checked()) {
		_enableTaskbarIcon->setChecked(true);
	} else {
		updateWorkmode();
	}
}

void GeneralWidget::onEnableTaskbarIcon() {
	if (!_enableTrayIcon->checked() && !_enableTaskbarIcon->checked()) {
		_enableTrayIcon->setChecked(true);
	} else {
		updateWorkmode();
	}
}

void GeneralWidget::updateWorkmode() {
	auto newMode = (_enableTrayIcon->checked() && (!_enableTaskbarIcon || _enableTaskbarIcon->checked())) ? dbiwmWindowAndTray : (_enableTrayIcon->checked() ? dbiwmTrayOnly : dbiwmWindowOnly);
	if (Global::WorkMode().value() != newMode && (newMode == dbiwmWindowAndTray || newMode == dbiwmTrayOnly)) {
		cSetSeenTrayTooltip(false);
	}
	Global::RefWorkMode().set(newMode);
	Local::writeSettings();
}

#if !defined OS_WIN_STORE
void GeneralWidget::onAutoStart() {
	cSetAutoStart(_autoStart->checked());
	if (cAutoStart()) {
		psAutoStart(true);
		Local::writeSettings();
	} else {
		psAutoStart(false);
		if (_startMinimized->entity()->checked()) {
			_startMinimized->entity()->setChecked(false);
		} else {
			Local::writeSettings();
		}
	}
	_startMinimized->toggle(cAutoStart(), anim::type::normal);
}

void GeneralWidget::onStartMinimized() {
	auto checked = _startMinimized->entity()->checked();
	if (Global::LocalPasscode()) {
		if (checked) {
			_startMinimized->entity()->setChecked(false);
			Ui::show(Box<InformBox>(lang(lng_error_start_minimized_passcoded)));
		}
		return;
	}
	if (cStartMinimized() != checked) {
		cSetStartMinimized(checked);
		Local::writeSettings();
	}
}

void GeneralWidget::onAddInSendTo() {
	cSetSendToMenu(_addInSendTo->checked());
	psSendToMenu(_addInSendTo->checked());
	Local::writeSettings();
}
#endif // !OS_WIN_STORE

} // namespace Settings
