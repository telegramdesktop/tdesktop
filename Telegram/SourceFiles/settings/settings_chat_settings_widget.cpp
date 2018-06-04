/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_chat_settings_widget.h"

#include "styles/style_settings.h"
#include "lang/lang_keys.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "storage/localstorage.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "boxes/stickers_box.h"
#include "boxes/download_path_box.h"
#include "boxes/connection_box.h"
#include "boxes/confirm_box.h"

namespace Settings {

LabeledLink::LabeledLink(QWidget *parent, const QString &label, const QString &text, Type type, const char *slot) : RpWidget(parent)
, _label(this, label, Ui::FlatLabel::InitType::Simple, (type == Type::Primary) ? st::settingsPrimaryLabel : st::defaultFlatLabel)
, _link(this, text, (type == Type::Primary) ? st::boxLinkButton : st::defaultLinkButton) {
	connect(_link, SIGNAL(clicked()), parent, slot);
}

Ui::LinkButton *LabeledLink::link() const {
	return _link;
}

int LabeledLink::naturalWidth() const {
	return _label->naturalWidth() + st::normalFont->spacew + _link->naturalWidth();
}

int LabeledLink::resizeGetHeight(int newWidth) {
	_label->moveToLeft(0, 0, newWidth);
	_link->resizeToWidth(newWidth - st::normalFont->spacew - _label->width());
	_link->moveToLeft(_label->width() + st::normalFont->spacew, 0, newWidth);
	return _label->height();
}

#ifndef OS_WIN_STORE
DownloadPathState::DownloadPathState(QWidget *parent) : RpWidget(parent)
, _path(this, lang(lng_download_path_label), downloadPathText(), LabeledLink::Type::Secondary, SLOT(onDownloadPath()))
, _clear(this, lang(lng_download_path_clear)) {
	connect(_clear, SIGNAL(clicked()), this, SLOT(onClear()));
	connect(App::wnd(), SIGNAL(tempDirCleared(int)), this, SLOT(onTempDirCleared(int)));
	connect(App::wnd(), SIGNAL(tempDirClearFailed(int)), this, SLOT(onTempDirClearFailed(int)));
	subscribe(Global::RefDownloadPathChanged(), [this]() {
		_path->link()->setText(downloadPathText());
		resizeToWidth(width());
	});
	switch (App::wnd()->tempDirState()) {
	case MainWindow::TempDirEmpty: _state = State::Empty; break;
	case MainWindow::TempDirExists: _state = State::Exists; break;
	case MainWindow::TempDirRemoving: _state = State::Clearing; break;
	}
	updateControls();
}

int DownloadPathState::resizeGetHeight(int newWidth) {
	_path->resizeToWidth(qMin(newWidth, _path->naturalWidth()));
	_path->moveToLeft(0, 0, newWidth);
	_clear->moveToRight(0, 0, newWidth);
	return _path->height();
}

void DownloadPathState::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto text = ([this]() -> QString {
		switch (_state) {
		case State::Clearing: return lang(lng_download_path_clearing);
		case State::Cleared: return lang(lng_download_path_cleared);
		case State::ClearFailed: return Lang::Hard::ClearPathFailed();
		}
		return QString();
	})();
	if (!text.isEmpty()) {
		p.setFont(st::linkFont);
		p.setPen(st::windowFg);
		p.drawTextRight(0, 0, width(), text);
	}
}

void DownloadPathState::updateControls() {
	_clear->setVisible(_state == State::Exists);
	update();
}

QString DownloadPathState::downloadPathText() const {
	if (Global::DownloadPath().isEmpty()) {
		return lang(lng_download_path_default);
	} else if (Global::DownloadPath() == qsl("tmp")) {
		return lang(lng_download_path_temp);
	}
	return QDir::toNativeSeparators(Global::DownloadPath());
};

void DownloadPathState::onDownloadPath() {
	Ui::show(Box<DownloadPathBox>());
}

void DownloadPathState::onClear() {
	Ui::show(Box<ConfirmBox>(lang(lng_sure_clear_downloads), crl::guard(this, [this] {
		Ui::hideLayer();
		App::wnd()->tempDirDelete(Local::ClearManagerDownloads);
		_state = State::Clearing;
		updateControls();
	})));
}

void DownloadPathState::onTempDirCleared(int task) {
	if (task & Local::ClearManagerDownloads) {
		_state = State::Cleared;
	}
	updateControls();
}

void DownloadPathState::onTempDirClearFailed(int task) {
	if (task & Local::ClearManagerDownloads) {
		_state = State::ClearFailed;
	}
	updateControls();
}
#endif // OS_WIN_STORE

ChatSettingsWidget::ChatSettingsWidget(QWidget *parent, UserData *self) : BlockWidget(parent, self, lang(lng_settings_section_chat_settings)) {
	createControls();
}

void ChatSettingsWidget::createControls() {
	style::margins marginSmall(0, 0, 0, st::settingsSmallSkip);
	style::margins marginSkip(0, 0, 0, st::settingsSkip);
	style::margins marginSub(0, 0, 0, st::settingsSubSkip);
	style::margins slidedPadding(0, marginSub.bottom() / 2, 0, marginSub.bottom() - (marginSub.bottom() / 2));

	createChildRow(_replaceEmoji, marginSmall, lang(lng_settings_replace_emojis), [this](bool) { toggleReplaceEmoji(); }, Global::ReplaceEmoji());
	createChildRow(_suggestEmoji, marginSmall, lang(lng_settings_suggest_emoji), [this](bool) { toggleSuggestEmoji(); }, Global::SuggestEmoji());
	createChildRow(_suggestByEmoji, marginSkip, lang(lng_settings_suggest_by_emoji), [this](bool) { toggleSuggestStickersByEmoji(); }, Global::SuggestStickersByEmoji());

#ifndef OS_WIN_STORE
	auto pathMargin = marginSub;
#else // OS_WIN_STORE
	auto pathMargin = marginSkip;
#endif // OS_WIN_STORE
	createChildRow(_dontAskDownloadPath, pathMargin, lang(lng_download_path_dont_ask), [this](bool) { onDontAskDownloadPath(); }, !Global::AskDownloadPath());

#ifndef OS_WIN_STORE
	style::margins marginPath(st::defaultCheck.diameter + st::defaultBoxCheckbox.textPosition.x(), 0, 0, st::settingsSkip);
	createChildRow(_downloadPath, marginPath, slidedPadding);
	if (Global::AskDownloadPath()) {
		_downloadPath->hide(anim::type::instant);
	}
#endif // OS_WIN_STORE

	auto group = std::make_shared<Ui::RadioenumGroup<SendByType>>(cCtrlEnter() ? SendByType::CtrlEnter : SendByType::Enter);
	createChildRow(_sendByEnter, marginSmall, group, SendByType::Enter, lang(lng_settings_send_enter));
	createChildRow(_sendByCtrlEnter, marginSkip, group, SendByType::CtrlEnter, lang((cPlatform() == dbipMac || cPlatform() == dbipMacOld) ? lng_settings_send_cmdenter : lng_settings_send_ctrlenter));
	group->setChangedCallback([this](SendByType value) {
		sendByChanged(value);
	});

	createChildRow(_automaticMediaDownloadSettings, marginSmall, lang(lng_media_auto_settings), SLOT(onAutomaticMediaDownloadSettings()));
	createChildRow(_manageStickerSets, marginSmall, lang(lng_stickers_you_have), SLOT(onManageStickerSets()));
}

void ChatSettingsWidget::toggleReplaceEmoji() {
	Global::SetReplaceEmoji(_replaceEmoji->checked());
	Global::RefReplaceEmojiChanged().notify();
	Local::writeUserSettings();
}

void ChatSettingsWidget::toggleSuggestEmoji() {
	Global::SetSuggestEmoji(_suggestEmoji->checked());
	Local::writeUserSettings();
}

void ChatSettingsWidget::toggleSuggestStickersByEmoji() {
	Global::SetSuggestStickersByEmoji(_suggestByEmoji->checked());
	Local::writeUserSettings();
}

void ChatSettingsWidget::onDontAskDownloadPath() {
	Global::SetAskDownloadPath(!_dontAskDownloadPath->checked());
	Local::writeUserSettings();
#ifndef OS_WIN_STORE
	_downloadPath->toggle(
		_dontAskDownloadPath->checked(),
		anim::type::normal);
#endif // OS_WIN_STORE
}

void ChatSettingsWidget::sendByChanged(SendByType value) {
	cSetCtrlEnter(value == SendByType::CtrlEnter);
	if (App::main()) App::main()->ctrlEnterSubmitUpdated();
	Local::writeUserSettings();
}

void ChatSettingsWidget::onAutomaticMediaDownloadSettings() {
	Ui::show(Box<AutoDownloadBox>());
}

void ChatSettingsWidget::onManageStickerSets() {
	Ui::show(Box<StickersBox>(StickersBox::Section::Installed));
}

} // namespace Settings
