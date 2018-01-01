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
#include "passcodewidget.h"

#include "lang/lang_keys.h"
#include "storage/localstorage.h"
#include "mainwindow.h"
#include "messenger.h"
#include "ui/text/text.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "styles/style_boxes.h"
#include "window/window_slide_animation.h"
#include "window/window_controller.h"
#include "auth_session.h"

PasscodeWidget::PasscodeWidget(QWidget *parent) : TWidget(parent)
, _passcode(this, st::passcodeInput, langFactory(lng_passcode_ph))
, _submit(this, langFactory(lng_passcode_submit), st::passcodeSubmit)
, _logout(this, lang(lng_passcode_logout)) {
	connect(_passcode, SIGNAL(changed()), this, SLOT(onChanged()));
	connect(_passcode, SIGNAL(submitted(bool)), this, SLOT(onSubmit()));

	_submit->setClickedCallback([this] { onSubmit(); });
	_logout->setClickedCallback([] { App::wnd()->onLogout(); });

	show();
}

void PasscodeWidget::onSubmit() {
	if (_passcode->text().isEmpty()) {
		_passcode->showError();
		return;
	}
	if (!passcodeCanTry()) {
		_error = lang(lng_flood_error);
		_passcode->showError();
		update();
		return;
	}

	if (App::main()) {
		if (Local::checkPasscode(_passcode->text().toUtf8())) {
			Messenger::Instance().clearPasscode(); // Destroys this widget.
			return;
		} else {
			cSetPasscodeBadTries(cPasscodeBadTries() + 1);
			cSetPasscodeLastTry(getms(true));
			onError();
			return;
		}
	} else {
		if (Local::readMap(_passcode->text().toUtf8()) != Local::ReadMapPassNeeded) {
			cSetPasscodeBadTries(0);

			Messenger::Instance().startMtp();
			if (AuthSession::Exists()) {
				App::wnd()->setupMain();
			} else {
				App::wnd()->setupIntro();
			}
		} else {
			cSetPasscodeBadTries(cPasscodeBadTries() + 1);
			cSetPasscodeLastTry(getms(true));
			onError();
			return;
		}
	}
}

void PasscodeWidget::onError() {
	_error = lang(lng_passcode_wrong);
	_passcode->selectAll();
	_passcode->showError();
	update();
}

void PasscodeWidget::onChanged() {
	if (!_error.isEmpty()) {
		_error = QString();
		update();
	}
}

void PasscodeWidget::showAnimated(const QPixmap &bgAnimCache, bool back) {
	_showBack = back;
	(_showBack ? _cacheOver : _cacheUnder) = bgAnimCache;

	_a_show.finish();

	showAll();
	setInnerFocus();
	_passcode->finishAnimating();
	(_showBack ? _cacheUnder : _cacheOver) = Ui::GrabWidget(this);
	hideAll();

	_a_show.start(
		[this] { animationCallback(); },
		0.,
		1.,
		st::slideDuration,
		Window::SlideAnimation::transition());
	show();
}

void PasscodeWidget::animationCallback() {
	update();
	if (!_a_show.animating()) {
		showAll();
		if (App::wnd()) App::wnd()->setInnerFocus();

		Ui::showChatsList();

		_cacheUnder = _cacheOver = QPixmap();
	}
}

void PasscodeWidget::showAll() {
	_passcode->show();
	_submit->show();
	_logout->show();
}

void PasscodeWidget::hideAll() {
	_passcode->hide();
	_submit->hide();
	_logout->hide();
}

void PasscodeWidget::paintEvent(QPaintEvent *e) {
	bool trivial = (rect() == e->rect());
	setMouseTracking(true);

	Painter p(this);
	if (!trivial) {
		p.setClipRect(e->rect());
	}

	auto progress = _a_show.current(getms(), 1.);
	if (_a_show.animating()) {
		auto coordUnder = _showBack ? anim::interpolate(-st::slideShift, 0, progress) : anim::interpolate(0, -st::slideShift, progress);
		auto coordOver = _showBack ? anim::interpolate(0, width(), progress) : anim::interpolate(width(), 0, progress);
		auto shadow = _showBack ? (1. - progress) : progress;
		if (coordOver > 0) {
			p.drawPixmap(QRect(0, 0, coordOver, height()), _cacheUnder, QRect(-coordUnder * cRetinaFactor(), 0, coordOver * cRetinaFactor(), height() * cRetinaFactor()));
			p.setOpacity(shadow);
			p.fillRect(0, 0, coordOver, height(), st::slideFadeOutBg);
			p.setOpacity(1);
		}
		p.drawPixmap(coordOver, 0, _cacheOver);
		p.setOpacity(shadow);
		st::slideShadow.fill(p, QRect(coordOver - st::slideShadow.width(), 0, st::slideShadow.width(), height()));
	} else {
		p.fillRect(rect(), st::windowBg);

		p.setFont(st::passcodeHeaderFont);
		p.setPen(st::windowFg);
		p.drawText(QRect(0, _passcode->y() - st::passcodeHeaderHeight, width(), st::passcodeHeaderHeight), lang(lng_passcode_enter), style::al_center);

		if (!_error.isEmpty()) {
			p.setFont(st::boxTextFont);
			p.setPen(st::boxTextFgError);
			p.drawText(QRect(0, _passcode->y() + _passcode->height(), width(), st::passcodeSubmitSkip), _error, style::al_center);
		}
	}
}

void PasscodeWidget::resizeEvent(QResizeEvent *e) {
	_passcode->move((width() - _passcode->width()) / 2, (height() / 3));
	_submit->move(_passcode->x(), _passcode->y() + _passcode->height() + st::passcodeSubmitSkip);
	_logout->move(_passcode->x() + (_passcode->width() - _logout->width()) / 2, _submit->y() + _submit->height() + st::linkFont->ascent);
}

void PasscodeWidget::setInnerFocus() {
	if (auto controller = App::wnd()->controller()) {
		controller->dialogsListFocused().set(false, true);
	}
	_passcode->setFocusFast();
}
