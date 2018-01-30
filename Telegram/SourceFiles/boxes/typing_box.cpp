/*
This file is part of Telegreat,
modified by Sean.

For license and copyright information please follow this link:
https://git.io/TD
*/
#include "boxes/typing_box.h"

//#include "data/data_photo.h"
//#include "data/data_document.h"
//#include "boxes/confirm_box.h"
#include "lang/lang_keys.h"
#include "storage/localstorage.h"
//#include "mainwidget.h"
//#include "mainwindow.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
//#include "ui/widgets/input_fields.h"
//#include "history/history_location_manager.h"
#include "styles/style_boxes.h"

TypingBox::TypingBox(QWidget *parent)
: _onlineContact(this, lang(lng_edit_privacy_contacts), (cTyping() & 0x1), st::defaultBoxCheckbox)
, _onlineEveryone(this, lang(lng_edit_privacy_everyone), (cTyping() & 0x2), st::defaultBoxCheckbox)
, _typingPrivateContact(this, lang(lng_media_auto_private_chats), (cTyping() & 0x10), st::defaultBoxCheckbox)
, _typingGroupContact(this, lang(lng_telegreat_group), (cTyping() & 0x20), st::defaultBoxCheckbox)
, _typingSupergroupContact(this, lang(lng_telegreat_supergroup), (cTyping() & 0x40), st::defaultBoxCheckbox)
, _typingPrivate(this, lang(lng_media_auto_private_chats), (cTyping() & 0x100), st::defaultBoxCheckbox)
, _typingGroup(this, lang(lng_telegreat_group), (cTyping() & 0x200), st::defaultBoxCheckbox)
, _typingSupergroup(this, lang(lng_telegreat_supergroup), (cTyping() & 0x400), st::defaultBoxCheckbox)
, _about()
, _sectionHeight1(st::boxTitleHeight + 2 * (st::defaultCheck.diameter + st::setLittleSkip))
, _sectionHeight2(st::boxTitleHeight + 3 * (st::defaultCheck.diameter + st::setLittleSkip)) {
}

void TypingBox::prepare() {
	addButton(langFactory(lng_connection_save), [this] { onSave(); });
	addButton(langFactory(lng_cancel), [this] { closeBox(); });

	_about.setRichText(st::usernameTextStyle, lang(lng_telegreat_typing_desc));

	setDimensions(st::boxWidth, 3 * _sectionHeight2 - st::autoDownloadTopDelta + st::setLittleSkip + _typingSupergroup->heightNoMargins() + st::setLittleSkip);
}

void TypingBox::paintEvent(QPaintEvent *e) {
	BoxContent::paintEvent(e);

	Painter p(this);

	p.setPen(st::boxTitleFg);
	p.setFont(st::autoDownloadTitleFont);
	p.drawTextLeft(st::autoDownloadTitlePosition.x(), st::autoDownloadTitlePosition.y(), width(), lang(lng_telegreat_online_toast));
	p.drawTextLeft(st::autoDownloadTitlePosition.x(), _sectionHeight1 + st::autoDownloadTitlePosition.y(), width(), lang(lng_telegreat_typing_toast_contact));
	p.drawTextLeft(st::autoDownloadTitlePosition.x(), _sectionHeight1 + _sectionHeight2 + st::autoDownloadTitlePosition.y(), width(), lang(lng_telegreat_typing_toast_all));

	_about.drawLeft(p, st::autoDownloadTitlePosition.x(), _sectionHeight1 + 2 * _sectionHeight2 + st::autoDownloadTitlePosition.y(), width(), width());
}

void TypingBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	auto top = st::boxTitleHeight - st::autoDownloadTopDelta;
	_onlineContact->moveToLeft(st::boxTitlePosition.x(), top + st::setLittleSkip);
	_onlineEveryone->moveToLeft(st::boxTitlePosition.x(), _onlineContact->bottomNoMargins() + st::setLittleSkip);

	_typingPrivateContact->moveToLeft(st::boxTitlePosition.x(), _sectionHeight1 + top + st::setLittleSkip);
	_typingGroupContact->moveToLeft(st::boxTitlePosition.x(), _typingPrivateContact->bottomNoMargins() + st::setLittleSkip);
	_typingSupergroupContact->moveToLeft(st::boxTitlePosition.x(), _typingGroupContact->bottomNoMargins() + st::setLittleSkip);

	_typingPrivate->moveToLeft(st::boxTitlePosition.x(), _sectionHeight1 + _sectionHeight2 + top + st::setLittleSkip);
	_typingGroup->moveToLeft(st::boxTitlePosition.x(), _typingPrivate->bottomNoMargins() + st::setLittleSkip);
	_typingSupergroup->moveToLeft(st::boxTitlePosition.x(), _typingGroup->bottomNoMargins() + st::setLittleSkip);
}

void TypingBox::onSave() {
	int typing = 0;

	if (_onlineContact->checked()) typing |= 0x1;
	if (_onlineEveryone->checked()) typing |= 0x2;

	if (_typingPrivateContact->checked()) typing |= 0x10;
	if (_typingGroupContact->checked()) typing |= 0x20;
	if (_typingSupergroupContact->checked()) typing |= 0x40;

	if (_typingPrivate->checked()) typing |= 0x100;
	if (_typingGroup->checked()) typing |= 0x200;
	if (_typingSupergroup->checked()) typing |= 0x400;

	cSetTyping(typing);
	Local::writeUserSettings();
	closeBox();
}
