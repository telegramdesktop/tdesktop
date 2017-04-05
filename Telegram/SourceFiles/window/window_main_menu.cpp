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
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "window/window_main_menu.h"

#include "styles/style_window.h"
#include "styles/style_dialogs.h"
#include "profile/profile_userpic_button.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/menu.h"
#include "mainwindow.h"
#include "boxes/contactsbox.h"
#include "boxes/aboutbox.h"
#include "lang.h"
#include "core/click_handler_types.h"
#include "auth_session.h"

namespace Window {

MainMenu::MainMenu(QWidget *parent) : TWidget(parent)
, _menu(this, st::mainMenu)
, _telegram(this, st::mainMenuTelegramLabel)
, _version(this, st::mainMenuVersionLabel) {
	setAttribute(Qt::WA_OpaquePaintEvent);

	subscribe(Global::RefSelfChanged(), [this] {
		checkSelf();
	});
	checkSelf();

	resize(st::mainMenuWidth, parentWidget()->height());
	_menu->setTriggeredCallback([](QAction *action, int actionTop, Ui::Menu::TriggeredSource source) {
		emit action->triggered();
	});
	_menu->addAction(lang(lng_create_group_title), [] {
		App::wnd()->onShowNewGroup();
	}, &st::mainMenuNewGroup, &st::mainMenuNewGroupOver);
	_menu->addAction(lang(lng_create_channel_title), [] {
		App::wnd()->onShowNewChannel();
	}, &st::mainMenuNewChannel, &st::mainMenuNewChannelOver);
	_menu->addAction(lang(lng_menu_contacts), [] {
		Ui::show(Box<ContactsBox>());
	}, &st::mainMenuContacts, &st::mainMenuContactsOver);
	_menu->addAction(lang(lng_menu_settings), [] {
		App::wnd()->showSettings();
	}, &st::mainMenuSettings, &st::mainMenuSettingsOver);
	_menu->addAction(lang(lng_settings_faq), [] {
		QDesktopServices::openUrl(telegramFaqLink());
	}, &st::mainMenuHelp, &st::mainMenuHelpOver);

	_telegram->setRichText(textcmdLink(1, qsl("Telegram Desktop")));
	_telegram->setLink(1, MakeShared<UrlClickHandler>(qsl("https://desktop.telegram.org")));
	_version->setRichText(textcmdLink(1, lng_settings_current_version(lt_version, currentVersionText())) + QChar(' ') + QChar(8211) + QChar(' ') + textcmdLink(2, lang(lng_menu_about)));
	_version->setLink(1, MakeShared<UrlClickHandler>(qsl("https://desktop.telegram.org/changelog")));
	_version->setLink(2, MakeShared<LambdaClickHandler>([] { Ui::show(Box<AboutBox>()); }));

	subscribe(AuthSession::CurrentDownloaderTaskFinished(), [this] { update(); });
	subscribe(Global::RefConnectionTypeChanged(), [this] { updateConnectionState(); });
	updateConnectionState();
}

void MainMenu::checkSelf() {
	if (auto self = App::self()) {
		auto showSelfChat = [] {
			if (auto self = App::self()) {
				Ui::showPeerHistory(App::history(self), ShowAtUnreadMsgId);
			}
		};
		_userpicButton.create(this, self, st::mainMenuUserpicSize);
		_userpicButton->setClickedCallback(showSelfChat);
		_userpicButton->show();
		_cloudButton.create(this, st::mainMenuCloudButton);
		_cloudButton->setClickedCallback(showSelfChat);
		_cloudButton->show();
		update();
		updateControlsGeometry();
		if (_showFinished) {
			_userpicButton->showFinished();
		}
	} else {
		_userpicButton.destroy();
		_cloudButton.destroy();
	}
}

void MainMenu::showFinished() {
	_showFinished = true;
	if (_userpicButton) {
		_userpicButton->showFinished();
	}
}

void MainMenu::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void MainMenu::updateControlsGeometry() {
	if (_userpicButton) {
		_userpicButton->moveToLeft(st::mainMenuUserpicLeft, st::mainMenuUserpicTop);
	}
	if (_cloudButton) {
		_cloudButton->moveToRight(0, st::mainMenuCoverHeight - _cloudButton->height());
	}
	_menu->setGeometry(0, st::mainMenuCoverHeight + st::mainMenuSkip, width(), _menu->height());
	_telegram->moveToLeft(st::mainMenuFooterLeft, height() - st::mainMenuTelegramBottom - _telegram->height());
	_version->moveToLeft(st::mainMenuFooterLeft, height() - st::mainMenuVersionBottom - _version->height());
}

void MainMenu::updateConnectionState() {
	auto state = MTP::dcstate();
	if (state == MTP::ConnectingState || state == MTP::DisconnectedState || state < 0) {
		_connectionText = lang(lng_status_connecting);
	} else {
		_connectionText = lang(lng_status_online);
	}
	update();
}

void MainMenu::paintEvent(QPaintEvent *e) {
	Painter p(this);
	auto clip = e->rect();
	auto cover = QRect(0, 0, width(), st::mainMenuCoverHeight).intersected(clip);
	if (!cover.isEmpty()) {
		p.fillRect(cover, st::mainMenuCoverBg);
		p.setPen(st::mainMenuCoverFg);
		p.setFont(st::semiboldFont);
		if (auto self = App::self()) {
			self->nameText.drawLeftElided(p, st::mainMenuCoverTextLeft, st::mainMenuCoverNameTop, width() - 2 * st::mainMenuCoverTextLeft, width());
			p.setFont(st::normalFont);
			p.drawTextLeft(st::mainMenuCoverTextLeft, st::mainMenuCoverStatusTop, width(), _connectionText);
		}
		if (_cloudButton) {
			PainterHighQualityEnabler hq(p);
			p.setPen(Qt::NoPen);
			p.setBrush(st::mainMenuCloudBg);
			auto cloudBg = QRect(_cloudButton->x() + (_cloudButton->width() - st::mainMenuCloudSize) / 2,
				_cloudButton->y() + (_cloudButton->height() - st::mainMenuCloudSize) / 2,
				st::mainMenuCloudSize, st::mainMenuCloudSize);
			p.drawEllipse(cloudBg);
		}
	}
	auto other = QRect(0, st::mainMenuCoverHeight, width(), height() - st::mainMenuCoverHeight).intersected(clip);
	if (!other.isEmpty()) {
		p.fillRect(other, st::mainMenuBg);
	}
}

} // namespace Window
