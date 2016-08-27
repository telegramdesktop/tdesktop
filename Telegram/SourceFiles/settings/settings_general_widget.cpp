/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "settings/settings_general_widget.h"

#include "styles/style_settings.h"
#include "lang.h"
#include "ui/widgets/widget_slide_wrap.h"
#include "ui/flatbutton.h"
#include "ui/flatcheckbox.h"
#include "localstorage.h"
#include "pspecific.h"
#include "mainwindow.h"
#include "boxes/languagebox.h"
#include "boxes/confirmbox.h"
#include "ui/filedialog.h"
#include "langloaderplain.h"

namespace Settings {
namespace {

QString currentVersion() {
	auto result = QString::fromLatin1(AppVersionStr.c_str());
	if (cAlphaVersion()) {
		result += " alpha";
	}
	if (cBetaVersion()) {
		result += qsl(" beta %1").arg(cBetaVersion());
	}
	return result;
}

} // namespace

GeneralWidget::GeneralWidget(QWidget *parent, UserData *self) : BlockWidget(parent, self, lang(lng_settings_section_general))
, _changeLanguage(this, lang(lng_settings_change_lang)) {
	connect(_changeLanguage, SIGNAL(clicked()), this, SLOT(onChangeLanguage()));
	refreshControls();
}

int GeneralWidget::resizeGetHeight(int newWidth) {
	_changeLanguage->moveToRight(contentLeft(), st::settingsBlockMarginTop + st::settingsBlockTitleTop + st::settingsBlockTitleFont->ascent - st::btnDefLink.font->ascent);
	return BlockWidget::resizeGetHeight(newWidth);
}

void GeneralWidget::refreshControls() {
	style::margins marginSub(0, 0, 0, st::settingsSubSkip);
	style::margins marginLarge(0, 0, 0, st::settingsLargeSkip);
	style::margins marginSmall(0, 0, 0, st::settingsSmallSkip);
	style::margins slidedPadding(0, marginSmall.bottom() / 2, 0, marginSmall.bottom() - (marginSmall.bottom() / 2));

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	addChildRow(_updateAutomatically, marginSub, lng_settings_update_automatically(lt_version, currentVersion()), SLOT(onUpdateAutomatically()), cAutoUpdate());
	style::margins marginLink(st::defaultCheckbox.textPosition.x(), 0, 0, st::settingsSkip);
	addChildRow(_checkForUpdates, marginLink, lang(lng_settings_check_now), SLOT(onCheckForUpdates()));
#endif // TDESKTOP_DISABLE_AUTOUPDATE

	if (cPlatform() == dbipWindows || cSupportTray()) {
		addChildRow(_enableTrayIcon, marginSmall, lang(lng_settings_workmode_tray), SLOT(onEnableTrayIcon()), (cWorkMode() == dbiwmTrayOnly || cWorkMode() == dbiwmWindowAndTray));
		if (cPlatform() == dbipWindows) {
			addChildRow(_enableTaskbarIcon, marginLarge, lang(lng_settings_workmode_window), SLOT(onEnableTaskbarIcon()), (cWorkMode() == dbiwmWindowOnly || cWorkMode() == dbiwmWindowAndTray));

			addChildRow(_autoStart, marginSmall, lang(lng_settings_auto_start), SLOT(onAutoStart()), cAutoStart());
			addChildRow(_startMinimized, marginLarge, slidedPadding, lang(lng_settings_start_min), SLOT(onStartMinimized()), cStartMinimized());
			if (!cAutoStart()) {
				_startMinimized->hideFast();
			}
			addChildRow(_addInSendTo, marginSmall, lang(lng_settings_add_sendto), SLOT(onAddInSendTo()), cSendToMenu());
		}
	}
}

void GeneralWidget::chooseCustomLang() {
	auto filter = qsl("Language files (*.strings)");
	auto title = qsl("Choose language .strings file");

	_chooseLangFileQueryId = FileDialog::queryReadFile(title, filter);
}

void GeneralWidget::notifyFileQueryUpdated(const FileDialog::QueryUpdate &update) {
	if (_chooseLangFileQueryId != update.queryId) {
		return;
	}
	_chooseLangFileQueryId = 0;

	if (update.filePaths.isEmpty()) {
		return;
	}

	_testLanguage = QFileInfo(update.filePaths.front()).absoluteFilePath();
	LangLoaderPlain loader(_testLanguage, LangLoaderRequest(lng_sure_save_language, lng_cancel, lng_box_ok));
	if (loader.errors().isEmpty()) {
		LangLoaderResult result = loader.found();
		QString text = result.value(lng_sure_save_language, langOriginal(lng_sure_save_language)),
			save = result.value(lng_box_ok, langOriginal(lng_box_ok)),
			cancel = result.value(lng_cancel, langOriginal(lng_cancel));
		auto box = new ConfirmBox(text, save, st::defaultBoxButton, cancel);
		connect(box, SIGNAL(confirmed()), this, SLOT(onSaveTestLanguage()));
		Ui::showLayer(box);
	} else {
		Ui::showLayer(new InformBox("Custom lang failed :(\n\nError: " + loader.errors()));
	}
}

void GeneralWidget::onChangeLanguage() {
	if ((_changeLanguage->clickModifiers() & Qt::ShiftModifier) && (_changeLanguage->clickModifiers() & Qt::AltModifier)) {
		chooseCustomLang();
	} else {
		Ui::showLayer(new LanguageBox());
	}
}

void GeneralWidget::onSaveTestLanguage() {
	cSetLangFile(_testLanguage);
	cSetLang(languageTest);
	Local::writeSettings();
	cSetRestarting(true);
	App::quit();
}

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
void GeneralWidget::onUpdateAutomatically() {

}

void GeneralWidget::onCheckForUpdates() {

}
#endif // TDESKTOP_DISABLE_AUTOUPDATE

void GeneralWidget::onEnableTrayIcon() {
	if ((!_enableTrayIcon->checked() || cPlatform() != dbipWindows) && !_enableTaskbarIcon->checked()) {
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
	DBIWorkMode newMode = (_enableTrayIcon->checked() && _enableTaskbarIcon->checked()) ? dbiwmWindowAndTray : (_enableTrayIcon->checked() ? dbiwmTrayOnly : dbiwmWindowOnly);
	if (cWorkMode() != newMode && (newMode == dbiwmWindowAndTray || newMode == dbiwmTrayOnly)) {
		cSetSeenTrayTooltip(false);
	}
	cSetWorkMode(newMode);
	App::wnd()->psUpdateWorkmode();
	Local::writeSettings();
}

void GeneralWidget::onAutoStart() {
	cSetAutoStart(_autoStart->checked());
	if (cAutoStart()) {
		psAutoStart(true);
		_startMinimized->slideDown();
		Local::writeSettings();
	} else {
		psAutoStart(false);
		if (_startMinimized->entity()->checked()) {
			_startMinimized->entity()->setChecked(false);
		} else {
			Local::writeSettings();
		}
		_startMinimized->slideUp();
	}
}

void GeneralWidget::onStartMinimized() {
	cSetStartMinimized(_startMinimized->entity()->checked());
	Local::writeSettings();
}

void GeneralWidget::onAddInSendTo() {
	cSetSendToMenu(_addInSendTo->checked());
	psSendToMenu(_addInSendTo->checked());
	Local::writeSettings();
}

} // namespace Settings
