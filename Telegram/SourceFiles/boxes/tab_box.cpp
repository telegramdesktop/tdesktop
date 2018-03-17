/*
This file is part of Telegreat,
modified by Sean.

For license and copyright information please follow this link:
https://git.io/TD
*/
#include "boxes/tab_box.h"

#include "lang/lang_keys.h"
#include "storage/localstorage.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "ui/widgets/checkbox.h"
#include "styles/style_boxes.h"
#include "history/history.h"

TabBox::TabBox(QWidget *parent)
: _hideMuted(this, lang(lng_dialogs_hide_muted_chats), (Global::DialogsMode() == Dialogs::Mode::Important), st::defaultBoxCheckbox)
, _showUser(this, lang(lng_telegreat_user), (cDialogsType() & 0x1), st::defaultBoxCheckbox)
, _showGroup(this, lang(lng_telegreat_group), (cDialogsType() & 0x2), st::defaultBoxCheckbox)
, _showChannel(this, lang(lng_telegreat_channel), (cDialogsType() & 0x4), st::defaultBoxCheckbox)
, _showBot(this, lang(lng_telegreat_bot), (cDialogsType() & 0x8), st::defaultBoxCheckbox)
, _sectionHeight(st::boxTitleHeight + 1 * (st::defaultCheck.diameter + st::setLittleSkip)) {
	connect(_hideMuted, SIGNAL(clicked()), this, SLOT(onHideMute()));
	connect(_showUser, SIGNAL(clicked()), this, SLOT(onSave()));
	connect(_showGroup, SIGNAL(clicked()), this, SLOT(onSave()));
	connect(_showChannel, SIGNAL(clicked()), this, SLOT(onSave()));
	connect(_showBot, SIGNAL(clicked()), this, SLOT(onSave()));
}

void TabBox::prepare() {
	addButton(langFactory(lng_about_done), [this] { closeBox(); });

	setDimensions(st::boxWidth, 3 * _sectionHeight - st::autoDownloadTopDelta + st::setLittleSkip + _showBot->heightNoMargins() + st::setLittleSkip);
}

void TabBox::paintEvent(QPaintEvent *e) {
	BoxContent::paintEvent(e);

	Painter p(this);

	p.setPen(st::boxTitleFg);
	p.setFont(st::autoDownloadTitleFont);
	p.drawTextLeft(st::autoDownloadTitlePosition.x(), st::autoDownloadTitlePosition.y(), width(), "Telegram Desktop");
	p.drawTextLeft(st::autoDownloadTitlePosition.x(), _sectionHeight + st::autoDownloadTitlePosition.y(), width(), lang(lng_telegreat_chat_type));
}

void TabBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	auto top = st::boxTitleHeight - st::autoDownloadTopDelta;
	_hideMuted->moveToLeft(st::boxTitlePosition.x(), top + st::setLittleSkip);

	_showUser->moveToLeft(st::boxTitlePosition.x(), _sectionHeight + top + st::setLittleSkip);
	_showGroup->moveToLeft(st::boxTitlePosition.x(), _showUser->bottomNoMargins() + st::setLittleSkip);
	_showChannel->moveToLeft(st::boxTitlePosition.x(), _showGroup->bottomNoMargins() + st::setLittleSkip);
	_showBot->moveToLeft(st::boxTitlePosition.x(), _showChannel->bottomNoMargins() + st::setLittleSkip);
}

void TabBox::onHideMute() {
	if (_hideMuted->checked()) {
		Global::SetDialogsMode(Dialogs::Mode::Important);
	} else {
		Global::SetDialogsMode(Dialogs::Mode::All);
	}
	Local::writeUserSettings();

	auto &peersData = App::peersData();
	auto c = peersData.constFind(0x2409A2230ULL);
	if (c != peersData.cend()) {
		PeerId peerId = c.value()->id;
		auto history = App::history(peerId);
		App::main()->removeDialog(history);
		history->updateChatListSortPosition(); // Refresh
	}
}

void TabBox::onSave() {
	int type = 0;
	if (_showUser->checked())
		type |= 0x1;
	if (_showGroup->checked())
		type |= 0x2;
	if (_showChannel->checked())
		type |= 0x4;
	if (_showBot->checked())
		type |= 0x8;
	int oriType = cDialogsType();
	if (oriType == 0)
		oriType = 0xf;
	cSetDialogsType(type);
	if (!type)
		type = 0xf;

	Local::writeUserSettings();

	auto &peersData = App::peersData();
	auto m = App::main();
	for_const (auto peer, peersData) {
		int bit = 0;
		if (peer->isUser()) {
			if (peer->asUser()->botInfo)
				bit = 0x8;
			else
				bit = 0x1;
		} else if (peer->isMegagroup() || peer->isChat())
			bit = 0x2;
		else if (peer->isChannel())
			bit = 0x4;
		
		if ((oriType ^ type) & bit) {
			PeerId peerId = peer->id;
			auto history = App::history(peerId);
			if (type & bit)
				history->updateChatListExistence();
			else
				App::main()->removeDialog(history);
		}
	}
}
