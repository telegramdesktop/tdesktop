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
#include "settings/settings_chat_settings_widget.h"

#include "styles/style_settings.h"
#include "lang.h"
#include "ui/widgets/widget_slide_wrap.h"
#include "ui/flatlabel.h"
#include "localstorage.h"
#include "mainwidget.h"
#include "boxes/emojibox.h"
#include "boxes/stickersetbox.h"
#include "boxes/downloadpathbox.h"
#include "boxes/connectionbox.h"

namespace Settings {

ChatSettingsWidget::ChatSettingsWidget(QWidget *parent, UserData *self) : BlockWidget(parent, self, lang(lng_settings_section_chat_settings)) {
	createControls();

	subscribe(Global::RefDownloadPathChanged(), [this]() {
		_downloadPath->entity()->link()->setText(downloadPathText());
		resizeToWidth(width());
	});
}

QString ChatSettingsWidget::downloadPathText() const {
	if (Global::DownloadPath().isEmpty()) {
		return lang(lng_download_path_default);
	} else if (Global::DownloadPath() == qsl("tmp")) {
		return lang(lng_download_path_temp);
	}
	return QDir::toNativeSeparators(Global::DownloadPath());
};


void ChatSettingsWidget::createControls() {
	style::margins marginSmall(0, 0, 0, st::settingsSmallSkip);
	style::margins marginSkip(0, 0, 0, st::settingsSkip);
	style::margins marginSub(0, 0, 0, st::settingsSubSkip);
	style::margins slidedPadding(0, marginSub.bottom() / 2, 0, marginSub.bottom() - (marginSub.bottom() / 2));

	addChildRow(_replaceEmoji, marginSub, lang(lng_settings_replace_emojis), SLOT(onReplaceEmoji()), cReplaceEmojis());
	style::margins marginList(st::defaultCheckbox.textPosition.x(), 0, 0, st::settingsSkip);
	addChildRow(_viewList, marginList, slidedPadding, lang(lng_settings_view_emojis), SLOT(onViewList()));

	addChildRow(_dontAskDownloadPath, marginSub, lang(lng_download_path_dont_ask), SLOT(onDontAskDownloadPath()), !Global::AskDownloadPath());
	style::margins marginPath(st::defaultCheckbox.textPosition.x(), 0, 0, st::settingsSkip);
	addChildRow(_downloadPath, marginPath, slidedPadding, lang(lng_download_path_label), downloadPathText());
	connect(_downloadPath->entity()->link(), SIGNAL(clicked()), this, SLOT(onDownloadPath()));
	if (Global::AskDownloadPath()) {
		_downloadPath->hideFast();
	}

	addChildRow(_sendByEnter, marginSmall, qsl("send_key"), 0, lang(lng_settings_send_enter), SLOT(onSendByEnter()), !cCtrlEnter());
	addChildRow(_sendByCtrlEnter, marginSkip, qsl("send_key"), 1, lang((cPlatform() == dbipMac || cPlatform() == dbipMacOld) ? lng_settings_send_cmdenter : lng_settings_send_ctrlenter), SLOT(onSendByCtrlEnter()), cCtrlEnter());
	addChildRow(_automaticMediaDownloadSettings, marginSmall, lang(lng_media_auto_settings), SLOT(onAutomaticMediaDownloadSettings()));
	addChildRow(_manageStickerSets, marginSmall, lang(lng_stickers_you_have), SLOT(onManageStickerSets()));
}

LabeledLink::LabeledLink(QWidget *parent, const QString &label, const QString &text) : TWidget(parent)
, _label(this, label, FlatLabel::InitType::Simple)
, _link(this, text) {
}

void LabeledLink::setLink(const QString &text) {
	_link.destroy();
	_link = new LinkButton(this, text);
}

int LabeledLink::naturalWidth() const {
	return _label->naturalWidth() + st::normalFont->spacew + _link->naturalWidth();
}

int LabeledLink::resizeGetHeight(int newWidth) {
	_label->moveToLeft(0, 0);
	_link->resizeToWidth(newWidth - st::normalFont->spacew - _label->width());
	_link->moveToLeft(_label->width() + st::normalFont->spacew, 0);
	return _label->height();
}

void ChatSettingsWidget::onReplaceEmoji() {
	cSetReplaceEmojis(_replaceEmoji->checked());
	Local::writeUserSettings();

	if (_replaceEmoji->checked()) {
		_viewList->slideDown();
	} else {
		_viewList->slideUp();
	}
}

void ChatSettingsWidget::onViewList() {
	Ui::showLayer(new EmojiBox());
}

void ChatSettingsWidget::onDontAskDownloadPath() {
	Global::SetAskDownloadPath(!_dontAskDownloadPath->checked());
	Local::writeUserSettings();
	if (_dontAskDownloadPath->checked()) {
		_downloadPath->slideDown();
	} else {
		_downloadPath->slideUp();
	}
}

void ChatSettingsWidget::onDownloadPath() {
	Ui::showLayer(new DownloadPathBox());
}

void ChatSettingsWidget::onSendByEnter() {
	if (_sendByEnter->checked()) {
		cSetCtrlEnter(false);
		if (App::main()) App::main()->ctrlEnterSubmitUpdated();
		Local::writeUserSettings();
	}
}

void ChatSettingsWidget::onSendByCtrlEnter() {
	if (_sendByCtrlEnter->checked()) {
		cSetCtrlEnter(true);
		if (App::main()) App::main()->ctrlEnterSubmitUpdated();
		Local::writeUserSettings();
	}
}

void ChatSettingsWidget::onAutomaticMediaDownloadSettings() {
	Ui::showLayer(new AutoDownloadBox());
}

void ChatSettingsWidget::onManageStickerSets() {
	Ui::showLayer(new StickersBox());
}

} // namespace Settings
