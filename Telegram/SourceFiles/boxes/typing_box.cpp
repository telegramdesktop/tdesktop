/*
This file is part of Telegreat,
modified by Sean.

For license and copyright information please follow this link:
https://git.io/TD
*/
#include "boxes/typing_box.h"

#include "lang/lang_keys.h"
#include "storage/localstorage.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "styles/style_boxes.h"

TypingBox::TypingBox(QWidget *parent)
: _onlineContact(this, tr::lng_edit_privacy_contacts(tr::now), (cTyping() & 0x1), st::defaultBoxCheckbox)
, _onlineEveryone(this, tr::lng_edit_privacy_everyone(tr::now), (cTyping() & 0x2), st::defaultBoxCheckbox)
, _typingPrivateContact(this, tr::lng_export_option_personal_chats(tr::now), (cTyping() & 0x10), st::defaultBoxCheckbox)
, _typingGroupContact(this, tr::lng_group_status(tr::now), (cTyping() & 0x20), st::defaultBoxCheckbox)
, _typingSupergroupContact(this, tr::lng_telegreat_supergroup(tr::now), (cTyping() & 0x40), st::defaultBoxCheckbox)
, _typingPrivate(this, tr::lng_export_option_personal_chats(tr::now), (cTyping() & 0x100), st::defaultBoxCheckbox)
, _typingGroup(this, tr::lng_group_status(tr::now), (cTyping() & 0x200), st::defaultBoxCheckbox)
, _typingSupergroup(this, tr::lng_telegreat_supergroup(tr::now), (cTyping() & 0x400), st::defaultBoxCheckbox)
, _about()
, _sectionHeight1(st::boxTitleHeight + 2 * (st::defaultCheck.diameter + st::setLittleSkip))
, _sectionHeight2(st::boxTitleHeight + 3 * (st::defaultCheck.diameter + st::setLittleSkip)) {
}

void TypingBox::prepare() {
	addButton(tr::lng_connection_save(), [this] { onSave(); });
	addButton(tr::lng_cancel(), [this] { closeBox(); });

	_about.setRichText(st::usernameTextStyle, tr::lng_telegreat_typing_desc(tr::now));

	setDimensions(st::boxWidth, 3 * _sectionHeight2 - st::autoDownloadTopDelta + st::setLittleSkip + _typingSupergroup->heightNoMargins() + st::setLittleSkip);
}

void TypingBox::paintEvent(QPaintEvent *e) {
	BoxContent::paintEvent(e);

	Painter p(this);

	p.setPen(st::boxTitleFg);
	p.setFont(st::autoDownloadTitleFont);
	p.drawTextLeft(st::autoDownloadTitlePosition.x(), st::autoDownloadTitlePosition.y(), width(), tr::lng_telegreat_online_toast(tr::now));
	p.drawTextLeft(st::autoDownloadTitlePosition.x(), _sectionHeight1 + st::autoDownloadTitlePosition.y(), width(), tr::lng_telegreat_typing_toast_contact(tr::now));
	p.drawTextLeft(st::autoDownloadTitlePosition.x(), _sectionHeight1 + _sectionHeight2 + st::autoDownloadTitlePosition.y(), width(), tr::lng_telegreat_typing_toast_all(tr::now));

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
