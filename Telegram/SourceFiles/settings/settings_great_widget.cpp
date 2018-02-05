/*
This file is part of Telegreat,
modified by Sean.

For license and copyright information please follow this link:
https://git.io/TD
*/
#include "settings/settings_great_widget.h"

#include "styles/style_settings.h"
#include "lang/lang_keys.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "storage/localstorage.h"
#include "platform/platform_specific.h"
#include "mainwindow.h"
#include "application.h"
#include "boxes/confirm_box.h"
#include "boxes/about_box.h"
#include "boxes/typing_box.h"
#include "core/file_utilities.h"
#include "messenger.h"
#include "autoupdater.h"

namespace Settings {

GreatWidget::GreatWidget(QWidget *parent, UserData *self) : BlockWidget(parent, self, lang(lng_telegreat_setting)) {
	refreshControls();
}

//int GreatWidget::resizeGetHeight(int newWidth) {
//	return BlockWidget::resizeGetHeight(newWidth);
//}

void GreatWidget::refreshControls() {
	style::margins marginSmall(0, 0, 0, st::settingsSmallSkip);
	style::margins marginLarge(0, 0, 0, st::settingsLargeSkip);
	style::margins marginSub(0, 0, 0, st::settingsSubSkip);


	createChildRow(_enableCallbackData, marginSmall, lang(lng_telegreat_setting_callback), [this](bool) { onCallbackData(); }, cShowCallbackData());
	createChildRow(_enableUsername, marginSmall, lang(lng_telegreat_setting_username), [this](bool) { onUsername(); }, cShowUsername());
	createChildRow(_enableIgnore, marginSmall, lang(lng_telegreat_setting_ignore), [this](bool) { onIgnore(); }, cIgnoreBlocked());
	createChildRow(_enableTagMention, marginSmall, lang(lng_telegreat_setting_everyuser), [this](bool) { onTagMention(); }, cTagMention());
	createChildRow(_enableAutoCopy, marginSmall, lang(lng_telegreat_setting_auto_copy), [this](bool) { onAutoCopy(); }, cAutoCopy());
	createChildRow(_enableUnstable, marginSmall, lang(lng_telegreat_setting_unstable), [this](bool) { onUnstable(); }, cUnstableFeature());
	createChildRow(_typing, marginSmall, lang(lng_telegreat_setting_typing), SLOT(onTyping()));
}

void GreatWidget::onRestart() {
	App::restart();
}
	
void GreatWidget::onUnstable() {
	cSetUnstableFeature(_enableUnstable->checked());
	Local::writeUserSettings();
}

void GreatWidget::onCallbackData() {
	cSetShowCallbackData(_enableCallbackData->checked());
	Local::writeUserSettings();
}

void GreatWidget::onUsername() {
	cSetShowUsername(_enableUsername->checked());
	Local::writeUserSettings();
}

void GreatWidget::onIgnore() {
	cSetIgnoreBlocked(_enableIgnore->checked());
	Local::writeUserSettings();
}
	
void GreatWidget::onTagMention() {
	cSetTagMention(_enableTagMention->checked());
	Local::writeUserSettings();
}

void GreatWidget::onAutoCopy() {
	cSetAutoCopy(_enableAutoCopy->checked());
	Local::writeUserSettings();
}

void GreatWidget::onTyping() {
	Ui::show(Box<TypingBox>());
}

} // namespace Settings
