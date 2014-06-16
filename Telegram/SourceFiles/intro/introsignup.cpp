/*
This file is part of Telegram Desktop,
an unofficial desktop messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://tdesktop.com
*/
#include "stdafx.h"
#include "lang.h"
#include "style.h"

#include "gui/filedialog.h"
#include "boxes/photocropbox.h"

#include "application.h"

#include "intro/introsignup.h"
#include "intro/intro.h"

IntroSignup::IntroSignup(IntroWidget *parent) : IntroStage(parent),
	errorAlpha(0), a_photo(0),
    next(this, lang(lng_intro_finish), st::btnIntroFinish), 
	first(this, st::inpIntroName, lang(lng_signup_firstname)),
	last(this, st::inpIntroName, lang(lng_signup_lastname)) {
	setVisible(false);
	setGeometry(parent->innerRect());

	connect(&next, SIGNAL(clicked()), this, SLOT(onSubmitName()));
	connect(&checkRequest, SIGNAL(timeout()), this, SLOT(onCheckRequest()));

	setMouseTracking(true);
}

void IntroSignup::mouseMoveEvent(QMouseEvent *e) {
	bool photoOver = QRect(_phLeft, _phTop, st::setPhotoSize, st::setPhotoSize).contains(e->pos());
	if (photoOver != _photoOver) {
		_photoOver = photoOver;
		if (_photoSmall.isNull()) {
			a_photo.start(_photoOver ? 1 : 0);
			errorAlpha.restart();
			anim::start(this);
		}
	}

	setCursor(_photoOver ? style::cur_pointer : style::cur_default);
}

void IntroSignup::mousePressEvent(QMouseEvent *e) {
	mouseMoveEvent(e);
	if (QRect(_phLeft, _phTop, st::setPhotoSize, st::setPhotoSize).contains(e->pos())) {
		QStringList imgExtensions(cImgExtensions());
		QString filter(qsl("Image files (*") + imgExtensions.join(qsl(" *")) + qsl(");;All files (*.*)"));

		QImage img;
		QString file;
		QByteArray remoteContent;
		if (filedialogGetOpenFile(file, remoteContent, lang(lng_choose_images), filter)) {
			if (!remoteContent.isEmpty()) {
				img = App::readImage(remoteContent);
			} else {
				if (!file.isEmpty()) {
					img = App::readImage(file);
				}
			}
		} else {
			return;
		}

		if (img.isNull() || img.width() > 10 * img.height() || img.height() > 10 * img.width()) {
			showError(lang(lng_bad_photo));
			return;
		}
		PhotoCropBox *box = new PhotoCropBox(img, 0);
		connect(box, SIGNAL(ready(const QImage &)), this, SLOT(onPhotoReady(const QImage &)));
		App::wnd()->showLayer(box);
	}
}

void IntroSignup::paintEvent(QPaintEvent *e) {
	bool trivial = (rect() == e->rect());

	QPainter p(this);
	if (!trivial) {
		p.setClipRect(e->rect());
	}
	if (trivial || e->rect().intersects(textRect)) {
		p.setFont(st::introTitleFont->f);
		p.drawText(textRect, lang(lng_signup_title), QTextOption(Qt::AlignHCenter | Qt::AlignTop));
	}
	if (animating() || error.length()) {
		p.setOpacity(errorAlpha.current());

		QRect errRect((width() - st::introErrWidth) / 2, (last.y() + last.height() + next.y() - st::introErrHeight) / 2, st::introErrWidth, st::introErrHeight);
		p.fillRect(errRect, st::introErrBG->b);
		p.setFont(st::introErrFont->f);
		p.setPen(st::introErrColor->p);
		p.drawText(errRect, error, QTextOption(style::al_center));

		p.setOpacity(1);
	}

	if (_photoSmall.isNull()) {
		if (a_photo.current() < 1) {
			p.drawPixmap(QPoint(_phLeft, _phTop), App::sprite(), st::setPhotoImg);
		}
		if (a_photo.current() > 0) {
			p.setOpacity(a_photo.current());
			p.drawPixmap(QPoint(_phLeft, _phTop), App::sprite(), st::setOverPhotoImg);
			p.setOpacity(1);
		}
	} else {
		p.drawPixmap(_phLeft, _phTop, _photoSmall);
	}
}

void IntroSignup::resizeEvent(QResizeEvent *e) {
	textRect = QRect((width() - st::introTextSize.width()) / 2, 0, st::introTextSize.width(), st::introTextSize.height());
	_phLeft = (width() - st::setPhotoImg.pxWidth()) / 2;
	_phTop = st::introHeaderFont->height + st::introFinishSkip;
	if (e->oldSize().width() != width()) {
		int sumNext = st::btnIntroNext.width - st::btnIntroBack.width - st::btnIntroSep;
		next.move((width() - sumNext) / 2, st::introSize.height() - st::btnIntroNext.height);
	}
	if (e->oldSize().width() != width()) {
		next.move((width() - next.width()) / 2, st::introSize.height() - st::btnIntroNext.height);
		first.move((width() - first.width()) / 2, _phTop + st::setPhotoImg.pxHeight() + st::introFinishSkip);
		last.move((width() - last.width()) / 2, first.y() + first.height() + st::introFinishSkip);
	}
}

void IntroSignup::showError(const QString &err) {
	if (!animating() && err == error) return;

	if (err.length()) {
		error = err;
		errorAlpha.start(1);
	} else {
		errorAlpha.start(0);
	}
	a_photo.restart();
	anim::start(this);
}

bool IntroSignup::animStep(float64 ms) {
	float64 dt = ms / st::introErrDuration;

	bool res = true;
	if (dt >= 1) {
		res = false;
		errorAlpha.finish();
		if (!errorAlpha.current()) {
			error = "";
		}
		a_photo.finish();
	} else {
		errorAlpha.update(dt, st::introErrFunc);
		a_photo.update(dt, anim::linear);
	}
	update();
	return res;
}

void IntroSignup::activate() {
	show();
	first.setFocus();
}

void IntroSignup::deactivate() {
	hide();
}

void IntroSignup::stopCheck() {
	checkRequest.stop();
}

void IntroSignup::onCheckRequest() {
	int32 status = MTP::state(sentRequest);
	if (status < 0) {
		int32 leftms = -status;
		if (leftms >= 1000) {
			MTP::cancel(sentRequest);
			sentRequest = 0;
			if (!first.isEnabled()) {
				first.setDisabled(false);
				last.setDisabled(false);
				last.setFocus();
			}
		}
	}
	if (!sentRequest && status == MTP::RequestSent) {
		stopCheck();
	}
}

void IntroSignup::onPhotoReady(const QImage &img) {
	_photoBig = img;
	_photoSmall = QPixmap::fromImage(img.scaled(st::setPhotoSize, st::setPhotoSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
	App::wnd()->hideLayer();
}

void IntroSignup::nameSubmitDone(const MTPauth_Authorization &result) {
	stopCheck();
	first.setDisabled(false);
	last.setDisabled(false);
	const MTPDauth_authorization &d(result.c_auth_authorization());
	if (d.vuser.type() != mtpc_userSelf) { // wtf?
		showError(lang(lng_server_error));
		return;
	}
	intro()->finish(d.vuser, _photoBig);
}

bool IntroSignup::nameSubmitFail(const RPCError &error) {
	stopCheck();
	first.setDisabled(false);
	last.setDisabled(false);
	const QString &err = error.type();
	if (err == "PHONE_NUMBER_INVALID" || err == "PHONE_CODE_EXPIRED" || err == "PHONE_CODE_EMPTY" || err == "PHONE_CODE_INVALID" || err == "PHONE_NUMBER_OCCUPIED") {
		intro()->onIntroBack();
		return true;
	} else if (err == "FIRSTNAME_INVALID") {
		showError(lang(lng_bad_name));
		first.setFocus();
		return true;
	} else if (err == "LASTNAME_INVALID") {
		showError(lang(lng_bad_name));
		last.setFocus();
		return true;
	}
	if (QRegularExpression("^FLOOD_WAIT_(\\d+)$").match(err).hasMatch()) {
		showError(lang(lng_flood_error));
		last.setFocus();
		return true;
	}
	if (cDebug()) { // internal server error
		showError(err + ": " + error.description());
	} else {
		showError(lang(lng_server_error));
	}
	first.setFocus();
	return false;
}

void IntroSignup::onInputChange() {
	showError("");
}

void IntroSignup::onSubmitName(bool force) {
	if ((first.hasFocus() || first.text().trimmed().length()) && !last.text().trimmed().length()) {
		last.setFocus();
		return;
	} else if (!first.text().trimmed().length()) {
		first.setFocus();
		return;
	}
	if (!force && !first.isEnabled()) return;

	first.setDisabled(true);
	last.setDisabled(true);
	setFocus();

	showError("");

	firstName = first.text().trimmed();
	lastName = last.text().trimmed();
	sentRequest = MTP::send(MTPauth_SignUp(MTP_string(intro()->getPhone()), MTP_string(intro()->getPhoneHash()), MTP_string(intro()->getCode()), MTP_string(firstName), MTP_string(lastName)), rpcDone(&IntroSignup::nameSubmitDone), rpcFail(&IntroSignup::nameSubmitFail));
}

void IntroSignup::onNext() {
	onSubmitName();
}

void IntroSignup::onBack() {
}
