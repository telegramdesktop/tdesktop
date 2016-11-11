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
#include "window/window_main_menu.h"

#include "styles/style_window.h"
#include "ui/flatlabel.h"
#include "ui/widgets/menu.h"
#include "mainwindow.h"
#include "boxes/contactsbox.h"
#include "boxes/aboutbox.h"
#include "lang.h"
#include "core/click_handler_types.h"
#include "styles/style_dialogs.h"

namespace Window {
namespace {

class AboutClickHandler : public ClickHandler {
public:
	void onClick(Qt::MouseButton) const override {
		Ui::showLayer(new AboutBox());
	}

};

} // namespace

MainMenu::MainMenu(QWidget *parent) : TWidget(parent)
, _menu(this, st::dialogsMenu)
, _telegram(this, st::mainMenuTelegramLabel, st::mainMenuTelegramStyle)
, _version(this, st::mainMenuVersionLabel, st::mainMenuVersionStyle) {
	setAttribute(Qt::WA_OpaquePaintEvent);
	resize(st::mainMenuWidth, parentWidget()->height());
	_menu->setTriggeredCallback([](QAction *action, int actionTop, Ui::Menu::TriggeredSource source) {
		emit action->triggered();
	});
	_menu->addAction(lang(lng_create_group_title), [] {
		App::wnd()->onShowNewGroup();
	}, &st::dialogsMenuNewGroup, &st::dialogsMenuNewGroupOver);
	_menu->addAction(lang(lng_create_channel_title), [] {
		App::wnd()->onShowNewChannel();
	}, &st::dialogsMenuNewChannel, &st::dialogsMenuNewChannelOver);
	_menu->addAction(lang(lng_menu_contacts), [] {
		Ui::showLayer(new ContactsBox());
	}, &st::dialogsMenuContacts, &st::dialogsMenuContactsOver);
	_menu->addAction(lang(lng_menu_settings), [] {
		App::wnd()->showSettings();
	}, &st::dialogsMenuSettings, &st::dialogsMenuSettingsOver);
	_menu->addAction(lang(lng_settings_faq), [] {
		QDesktopServices::openUrl(telegramFaqLink());
	}, &st::dialogsMenuHelp, &st::dialogsMenuHelpOver);

	_telegram->setRichText(textcmdLink(1, qsl("Telegram Desktop")));
	_telegram->setLink(1, ClickHandlerPtr(new UrlClickHandler(qsl("https://desktop.telegram.org"))));
	_version->setRichText(textcmdLink(1, qsl("Version 1.2.3")) + QChar(' ') + QChar(8211) + QChar(' ') + textcmdLink(2, qsl("About")));
	_version->setLink(1, ClickHandlerPtr(new UrlClickHandler(qsl("https://desktop.telegram.org/?_hash=changelog"))));
	_version->setLink(2, ClickHandlerPtr(new AboutClickHandler()));
}

void MainMenu::resizeEvent(QResizeEvent *e) {
	_menu->setGeometry(0, st::mainMenuCoverHeight + st::mainMenuSkip, width(), _menu->height());
	_telegram->moveToLeft(st::mainMenuFooterLeft, height() - st::mainMenuTelegramBottom - _telegram->height());
	_version->moveToLeft(st::mainMenuFooterLeft, height() - st::mainMenuVersionBottom - _version->height());
}

void MainMenu::paintEvent(QPaintEvent *e) {
	Painter p(this);
	auto cover = QRect(0, 0, width(), st::mainMenuCoverHeight).intersected(e->rect());
	if (!cover.isEmpty()) {
		p.fillRect(cover, st::mainMenuCoverBg);
		p.setPen(st::mainMenuCoverFg);
		p.setFont(st::semiboldFont);
		if (auto self = App::self()) {
			self->paintUserpicLeft(p, st::mainMenuUserpicSize, st::mainMenuUserpicLeft, st::mainMenuUserpicTop, width());
			self->nameText.drawLeftElided(p, st::mainMenuCoverTextLeft, st::mainMenuCoverNameTop, width() - 2 * st::mainMenuCoverTextLeft, width());
			p.setFont(st::normalFont);
			p.drawTextLeft(st::mainMenuCoverTextLeft, st::mainMenuCoverStatusTop, width(), qsl("online"));
		}
	}
	auto other = QRect(0, st::mainMenuCoverHeight, width(), height() - st::mainMenuCoverHeight);
	if (!other.isEmpty()) {
		p.fillRect(other, st::mainMenuBg);
	}
}

} // namespace Window
