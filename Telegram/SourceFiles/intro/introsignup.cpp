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
Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
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
	next(this, lang(lng_intro_finish), st::btnIntroNext),
	first(this, st::inpIntroName, lang(lng_signup_firstname)),
	last(this, st::inpIntroName, lang(lng_signup_lastname)),
	_invertOrder(langFirstNameGoesSecond()) {
	setVisible(false);
	setGeometry(parent->innerRect());

	connect(&next, SIGNAL(clicked()), this, SLOT(onSubmitName()));
	connect(&checkRequest, SIGNAL(timeout()), this, SLOT(onCheckRequest()));

	if (_invertOrder) {
		setTabOrder(&last, &first);
	}

	setMouseTracking(true);
}

void IntroSignup::mouseMoveEvent(QMouseEvent *e) {
	bool photoOver = QRect(_phLeft, _phTop, st::introPhotoSize, st::introPhotoSize).contains(e->pos());
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
	if (QRect(_phLeft, _phTop, st::introPhotoSize, st::introPhotoSize).contains(e->pos())) {
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
		p.setFont(st::introHeaderFont->f);
		p.drawText(textRect, lang(lng_signup_title), style::al_top);
		p.setFont(st::introFont->f);
		p.drawText(textRect, lang(lng_signup_desc), style::al_bottom);
	}
	if (animating() || error.length()) {
		p.setOpacity(errorAlpha.current());

		QRect errRect;
		if (_invertOrder) {
			errRect = QRect((width() - st::introErrWidth) / 2, (first.y() + first.height() + next.y() - st::introErrHeight) / 2, st::introErrWidth, st::introErrHeight);
		} else {
			errRect = QRect((width() - st::introErrWidth) / 2, (last.y() + last.height() + next.y() - st::introErrHeight) / 2, st::introErrWidth, st::introErrHeight);
		}
		p.setFont(st::introErrFont->f);
		p.setPen(st::introErrColor->p);
		p.drawText(errRect, error, QTextOption(style::al_center));

		p.setOpacity(1);
	}

	if (_photoSmall.isNull()) {
		if (a_photo.current() < 1) {
			QRect pix(st::setPhotoImg);
			pix.moveTo(pix.x() + (pix.width() - st::introPhotoSize) / 2, pix.y() + (pix.height() - st::introPhotoSize) / 2);
			pix.setSize(QSize(st::introPhotoSize, st::introPhotoSize));
			p.drawPixmap(QPoint(_phLeft, _phTop), App::sprite(), pix);
		}
		if (a_photo.current() > 0) {
			QRect pix(st::setOverPhotoImg);
			pix.moveTo(pix.x() + (pix.width() - st::introPhotoSize) / 2, pix.y() + (pix.height() - st::introPhotoSize) / 2);
			pix.setSize(QSize(st::introPhotoSize, st::introPhotoSize));
			p.setOpacity(a_photo.current());
			p.drawPixmap(QPoint(_phLeft, _phTop), App::sprite(), pix);
			p.setOpacity(1);
		}
	} else {
		p.drawPixmap(_phLeft, _phTop, _photoSmall);
	}
}

void IntroSignup::resizeEvent(QResizeEvent *e) {
	_phLeft = (width() - next.width()) / 2;
	_phTop = st::introTextTop + st::introTextSize.height() + st::introCountry.top;
	if (e->oldSize().width() != width()) {
		next.move((width() - next.width()) / 2, st::introBtnTop);
		if (_invertOrder) {
			last.move((width() - next.width()) / 2 + next.width() - last.width(), _phTop);
			first.move((width() - next.width()) / 2 + next.width() - first.width(), last.y() + st::introCountry.height + st::introCountry.ptrSize.height() + st::introPhoneTop);
		} else {
			first.move((width() - next.width()) / 2 + next.width() - first.width(), _phTop);
			last.move((width() - next.width()) / 2 + next.width() - last.width(), first.y() + st::introCountry.height + st::introCountry.ptrSize.height() + st::introPhoneTop);
		}
	}
	textRect = QRect((width() - st::introTextSize.width()) / 2, st::introTextTop, st::introTextSize.width(), st::introTextSize.height());
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
	if (_invertOrder) {
		last.setFocus();
	} else {
		first.setFocus();
	}
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
				if (_invertOrder) {
					first.setFocus();
				} else {
					last.setFocus();
				}
			}
		}
	}
	if (!sentRequest && status == MTP::RequestSent) {
		stopCheck();
	}
}

void IntroSignup::onPhotoReady(const QImage &img) {
	_photoBig = img;
	_photoSmall = QPixmap::fromImage(img.scaled(st::introPhotoSize * cIntRetinaFactor(), st::introPhotoSize * cIntRetinaFactor(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation), Qt::ColorOnly);
	_photoSmall.setDevicePixelRatio(cRetinaFactor());
}

void IntroSignup::nameSubmitDone(const MTPauth_Authorization &result) {
	stopCheck();
	first.setDisabled(false);
	last.setDisabled(false);
	const MTPDauth_authorization &d(result.c_auth_authorization());
	if (d.vuser.type() != mtpc_user || !(d.vuser.c_user().vflags.v & MTPDuser_flag_self)) { // wtf?
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
	} else if (mtpIsFlood(error)) {
		showError(lang(lng_flood_error));
		if (_invertOrder) {
			first.setFocus();
		} else {
			last.setFocus();
		}
		return true;
	}
	if (cDebug()) { // internal server error
		showError(err + ": " + error.description());
	} else {
		showError(lang(lng_server_error));
	}
	if (_invertOrder) {
		last.setFocus();
	} else {
		first.setFocus();
	}
	return false;
}

void IntroSignup::onInputChange() {
	showError("");
}

void IntroSignup::onSubmitName(bool force) {
	if (_invertOrder) {
		if ((last.hasFocus() || last.text().trimmed().length()) && !first.text().trimmed().length()) {
			first.setFocus();
			return;
		} else if (!last.text().trimmed().length()) {
			last.setFocus();
			return;
		}
	} else {
		if ((first.hasFocus() || first.text().trimmed().length()) && !last.text().trimmed().length()) {
			last.setFocus();
			return;
		} else if (!first.text().trimmed().length()) {
			first.setFocus();
			return;
		}
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
