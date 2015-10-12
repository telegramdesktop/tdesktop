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

#include "localstorage.h"

#include "sessionsbox.h"
#include "mainwidget.h"
#include "window.h"

#include "countries.h"
#include "confirmbox.h"

SessionsInner::SessionsInner(SessionsList *list, SessionData *current) : TWidget()
, _list(list)
, _current(current)
, _terminating(0)
, _terminateAll(this, lang(lng_sessions_terminate_all), st::redBoxLinkButton)
, _terminateBox(0) {
	connect(&_terminateAll, SIGNAL(clicked()), this, SLOT(onTerminateAll()));
	_terminateAll.hide();
	setAttribute(Qt::WA_OpaquePaintEvent);
}

void SessionsInner::paintEvent(QPaintEvent *e) {
	QRect r(e->rect());
	Painter p(this);

	p.fillRect(r, st::white->b);
	int32 x = st::sessionPadding.left(), xact = st::sessionTerminateSkip + st::sessionTerminate.iconPos.x();// st::sessionTerminateSkip + st::sessionTerminate.width + st::sessionTerminateSkip;
	int32 w = width();

	if (_current->active.isEmpty() && _list->isEmpty()) {
		p.setFont(st::noContactsFont->f);
		p.setPen(st::noContactsColor->p);
		p.drawText(QRect(0, 0, width(), st::noContactsHeight), lang(lng_contacts_loading), style::al_center);
		return;
	}

	if (r.y() <= st::sessionCurrentHeight) {
		p.translate(0, st::sessionCurrentPadding.top());
		p.setFont(st::sessionNameFont->f);
		p.setPen(st::black->p);
		p.drawTextLeft(x, st::sessionPadding.top(), w, _current->name, _current->nameWidth);

		p.setFont(st::sessionActiveFont->f);
		p.setPen(st::sessionActiveColor->p);
		p.drawTextRight(x, st::sessionPadding.top(), w, _current->active, _current->activeWidth);

		p.setFont(st::sessionInfoFont->f);
		p.setPen(st::black->p);
		p.drawTextLeft(x, st::sessionPadding.top() + st::sessionNameFont->height, w, _current->info, _current->infoWidth);
		p.setPen(st::sessionInfoColor->p);
		p.drawTextLeft(x, st::sessionPadding.top() + st::sessionNameFont->height + st::sessionInfoFont->height, w, _current->ip, _current->ipWidth);
	}
	p.translate(0, st::sessionCurrentHeight - st::sessionCurrentPadding.top());
	if (_list->isEmpty()) {
		p.setFont(st::sessionInfoFont->f);
		p.setPen(st::sessionInfoColor->p);
		p.drawText(QRect(st::sessionPadding.left(), 0, width() - st::sessionPadding.left() - st::sessionPadding.right(), st::noContactsHeight), lang(lng_sessions_other_desc), style::al_topleft);
		return;
	}

	p.setFont(st::linkFont->f);
	int32 count = _list->size();
	int32 from = floorclamp(r.y() - st::sessionCurrentHeight, st::sessionHeight, 0, count);
	int32 to = ceilclamp(r.y() + r.height() - st::sessionCurrentHeight, st::sessionHeight, 0, count);
	p.translate(0, from * st::sessionHeight);
	for (int32 i = from; i < to; ++i) {
		const SessionData &auth(_list->at(i));

		p.setFont(st::sessionNameFont->f);
		p.setPen(st::black->p);
		p.drawTextLeft(x, st::sessionPadding.top(), w, auth.name, auth.nameWidth);

		p.setFont(st::sessionActiveFont->f);
		p.setPen(st::sessionActiveColor->p);
		p.drawTextRight(xact, st::sessionPadding.top(), w, auth.active, auth.activeWidth);

		p.setFont(st::sessionInfoFont->f);
		p.setPen(st::black->p);
		p.drawTextLeft(x, st::sessionPadding.top() + st::sessionNameFont->height, w, auth.info, auth.infoWidth);
		p.setPen(st::sessionInfoColor->p);
		p.drawTextLeft(x, st::sessionPadding.top() + st::sessionNameFont->height + st::sessionInfoFont->height, w, auth.ip, auth.ipWidth);

		p.translate(0, st::sessionHeight);
	}
}

void SessionsInner::onTerminate() {
	for (TerminateButtons::iterator i = _terminateButtons.begin(), e = _terminateButtons.end(); i != e; ++i) {
		if (i.value()->getState() & Button::StateOver) {
			_terminating = i.key();

			if (_terminateBox) _terminateBox->deleteLater();
			_terminateBox = new ConfirmBox(lang(lng_settings_reset_one_sure), lang(lng_settings_reset_button), st::attentionBoxButton);
			connect(_terminateBox, SIGNAL(confirmed()), this, SLOT(onTerminateSure()));
			connect(_terminateBox, SIGNAL(destroyed(QObject*)), this, SLOT(onNoTerminateBox(QObject*)));
			App::wnd()->replaceLayer(_terminateBox);
		}
	}
}

void SessionsInner::onTerminateSure() {
	if (_terminateBox) {
		_terminateBox->onClose();
		_terminateBox = 0;
	}
	MTP::send(MTPaccount_ResetAuthorization(MTP_long(_terminating)), rpcDone(&SessionsInner::terminateDone, _terminating), rpcFail(&SessionsInner::terminateFail, _terminating));
	TerminateButtons::iterator i = _terminateButtons.find(_terminating);
	if (i != _terminateButtons.cend()) {
		i.value()->clearState();
		i.value()->hide();
	}
}

void SessionsInner::onTerminateAll() {
	if (_terminateBox) _terminateBox->deleteLater();
	_terminateBox = new ConfirmBox(lang(lng_settings_reset_sure), lang(lng_settings_reset_button), st::attentionBoxButton);
	connect(_terminateBox, SIGNAL(confirmed()), this, SLOT(onTerminateAllSure()));
	connect(_terminateBox, SIGNAL(destroyed(QObject*)), this, SLOT(onNoTerminateBox(QObject*)));
	App::wnd()->replaceLayer(_terminateBox);
}

void SessionsInner::onTerminateAllSure() {
	if (_terminateBox) {
		_terminateBox->onClose();
		_terminateBox = 0;
	}
	MTP::send(MTPauth_ResetAuthorizations(), rpcDone(&SessionsInner::terminateAllDone), rpcFail(&SessionsInner::terminateAllFail));
	emit terminateAll();
}

void SessionsInner::onNoTerminateBox(QObject *obj) {
	if (obj == _terminateBox) _terminateBox = 0;
}

void SessionsInner::terminateDone(uint64 hash, const MTPBool &result) {
	for (int32 i = 0, l = _list->size(); i < l; ++i) {
		if (_list->at(i).hash == hash) {
			_list->removeAt(i);
			break;
		}
	}
	listUpdated();
	emit oneTerminated();
}

bool SessionsInner::terminateFail(uint64 hash, const RPCError &error) {
	if (mtpIsFlood(error)) return false;

	TerminateButtons::iterator i = _terminateButtons.find(hash);
	if (i != _terminateButtons.end()) {
		i.value()->show();
		return true;
	}
	return false;
}

void SessionsInner::terminateAllDone(const MTPBool &result) {
	emit allTerminated();
}

bool SessionsInner::terminateAllFail(const RPCError &error) {
	if (mtpIsFlood(error)) return false;
	emit allTerminated();
	return true;
}

void SessionsInner::resizeEvent(QResizeEvent *e) {
	_terminateAll.moveToLeft(st::sessionPadding.left(), st::sessionCurrentPadding.top() + st::sessionHeight + st::sessionCurrentPadding.bottom());
}

void SessionsInner::listUpdated() {
	if (_list->isEmpty()) {
		_terminateAll.hide();
	} else {
		_terminateAll.show();
	}
	for (TerminateButtons::iterator i = _terminateButtons.begin(), e = _terminateButtons.end(); i != e; ++i) {
		i.value()->move(0, -1);
	}
	for (int32 i = 0, l = _list->size(); i < l; ++i) {
		TerminateButtons::iterator j = _terminateButtons.find(_list->at(i).hash);
		if (j == _terminateButtons.cend()) {
			j = _terminateButtons.insert(_list->at(i).hash, new IconedButton(this, st::sessionTerminate));
			connect(j.value(), SIGNAL(clicked()), this, SLOT(onTerminate()));
		}
		j.value()->moveToRight(st::sessionTerminateSkip, st::sessionCurrentHeight + i * st::sessionHeight + st::sessionTerminateTop, width());
		j.value()->show();
	}
	for (TerminateButtons::iterator i = _terminateButtons.begin(); i != _terminateButtons.cend();) {
		if (i.value()->y() >= 0) {
			++i;
		} else {
			delete i.value();
			i = _terminateButtons.erase(i);
		}
	}
	resize(width(), _list->isEmpty() ? (st::sessionCurrentHeight + st::noContactsHeight) : (st::sessionCurrentHeight + _list->size() * st::sessionHeight));
	update();
}

SessionsInner::~SessionsInner() {
	for (int32 i = 0, l = _terminateButtons.size(); i < l; ++i) {
		delete _terminateButtons[i];
	}
}

SessionsBox::SessionsBox() : ScrollableBox(st::sessionsScroll)
, _loading(true)
, _inner(&_list, &_current)
, _shadow(this)
, _done(this, lang(lng_about_done), st::defaultBoxButton)
, _shortPollRequest(0) {
	setMaxHeight(st::sessionsHeight);

	connect(&_done, SIGNAL(clicked()), this, SLOT(onClose()));
	connect(&_inner, SIGNAL(oneTerminated()), this, SLOT(onOneTerminated()));
	connect(&_inner, SIGNAL(allTerminated()), this, SLOT(onAllTerminated()));
	connect(&_inner, SIGNAL(terminateAll()), this, SLOT(onTerminateAll()));
	connect(App::wnd(), SIGNAL(newAuthorization()), this, SLOT(onNewAuthorization()));
	connect(&_shortPollTimer, SIGNAL(timeout()), this, SLOT(onShortPollAuthorizations()));

	init(&_inner, st::boxButtonPadding.bottom() + _done.height() + st::boxButtonPadding.top(), st::boxTitleHeight);
	_inner.resize(width(), st::noContactsHeight);

	prepare();

	MTP::send(MTPaccount_GetAuthorizations(), rpcDone(&SessionsBox::gotAuthorizations));
}

void SessionsBox::resizeEvent(QResizeEvent *e) {
	ScrollableBox::resizeEvent(e);
	_shadow.setGeometry(0, height() - st::boxButtonPadding.bottom() - _done.height() - st::boxButtonPadding.top() - st::lineWidth, width(), st::lineWidth);
	_done.moveToRight(st::boxButtonPadding.right(), height() - st::boxButtonPadding.bottom() - _done.height());
}

void SessionsBox::hideAll() {
	_done.hide();
	ScrollableBox::hideAll();
}

void SessionsBox::showAll() {
	_done.show();
	if (_loading) {
		_scroll.hide();
		_shadow.hide();
	} else {
		_scroll.show();
		_shadow.show();
	}
	ScrollableBox::showAll();
}

void SessionsBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (paint(p)) return;

	paintTitle(p, lang(lng_sessions_other_header));
	p.translate(0, st::boxTitleHeight);

	if (_loading) {
		p.setFont(st::noContactsFont->f);
		p.setPen(st::noContactsColor->p);
		p.drawText(QRect(0, 0, width(), st::noContactsHeight), lang(lng_contacts_loading), style::al_center);
	}
}

void SessionsBox::gotAuthorizations(const MTPaccount_Authorizations &result) {
	_loading = false;
	_shortPollRequest = 0;

	int32 availCurrent = st::boxWideWidth - st::sessionPadding.left() - st::sessionTerminateSkip;
	int32 availOther = availCurrent - st::sessionTerminate.iconPos.x();// -st::sessionTerminate.width - st::sessionTerminateSkip;

	_list.clear();
	const QVector<MTPAuthorization> &v(result.c_account_authorizations().vauthorizations.c_vector().v);
	int32 l = v.size();
	if (l > 1) _list.reserve(l - 1);

	const CountriesByISO2 &countries(countriesByISO2());

	for (int32 i = 0; i < l; ++i) {
		const MTPDauthorization &d(v.at(i).c_authorization());
		SessionData data;
		data.hash = d.vhash.v;

		QString appName, appVer = qs(d.vapp_version), systemVer = qs(d.vsystem_version), deviceModel = qs(d.vdevice_model);
		if (d.vapi_id.v == 2040 || d.vapi_id.v == 17349) {
			appName = (d.vapi_id.v == 2040) ? qstr("Telegram Desktop") : qstr("Telegram Desktop (GitHub)");
		//	if (systemVer == qstr("windows")) {
		//		deviceModel = qsl("Windows");
		//	} else if (systemVer == qstr("os x")) {
		//		deviceModel = qsl("OS X");
		//	} else if (systemVer == qstr("linux")) {
		//		deviceModel = qsl("Linux");
		//	}
			if (appVer == QString::number(appVer.toInt())) {
				int32 ver = appVer.toInt();
				appVer = QString("%1.%2").arg(ver / 1000000).arg((ver % 1000000) / 1000) + ((ver % 1000) ? ('.' + QString::number(ver % 1000)) : QString());
			} else {
				appVer = QString();
			}
		} else {
			appName = qs(d.vapp_name);// +qsl(" for ") + qs(d.vplatform);
			if (appVer.indexOf('(') >= 0) appVer = appVer.mid(appVer.indexOf('('));
		}
		data.name = appName;
		if (!appVer.isEmpty()) data.name += ' ' + appVer;
		data.nameWidth = st::sessionNameFont->width(data.name);

		QString country = qs(d.vcountry), platform = qs(d.vplatform);
		//CountriesByISO2::const_iterator j = countries.constFind(country);
		//if (j != countries.cend()) country = QString::fromUtf8(j.value()->name);

		MTPint active = d.vdate_active.v ? d.vdate_active : d.vdate_created;
		data.activeTime = active.v;

		data.info = qs(d.vdevice_model) + qstr(", ") + (platform.isEmpty() ? QString() : platform + ' ') + qs(d.vsystem_version);
		data.ip = qs(d.vip) + (country.isEmpty() ? QString() : QString::fromUtf8(" \xe2\x80\x93 ") + country);
		if (!data.hash || (d.vflags.v & 1)) {
			data.active = lang(lng_sessions_header);
			data.activeWidth = st::sessionActiveFont->width(lang(lng_sessions_header));
			int32 availForName = availCurrent - st::sessionPadding.right() - data.activeWidth;
			if (data.nameWidth > availForName) {
				data.name = st::sessionNameFont->elided(data.name, availForName);
				data.nameWidth = st::sessionNameFont->width(data.name);
			}
			data.infoWidth = st::sessionInfoFont->width(data.info);
			if (data.infoWidth > availCurrent) {
				data.info = st::sessionInfoFont->elided(data.info, availCurrent);
				data.infoWidth = st::sessionInfoFont->width(data.info);
			}
			data.ipWidth = st::sessionInfoFont->width(data.ip);
			if (data.ipWidth > availCurrent) {
				data.ip = st::sessionInfoFont->elided(data.ip, availCurrent);
				data.ipWidth = st::sessionInfoFont->width(data.ip);
			}
			_current = data;
		} else {
			QDateTime now(QDateTime::currentDateTime()), lastTime(date(active));
			QDate nowDate(now.date()), lastDate(lastTime.date());
			QString dt;
			if (lastDate == nowDate) {
				data.active = lastTime.toString(cTimeFormat());
			} else if (lastDate.year() == nowDate.year() && lastDate.weekNumber() == nowDate.weekNumber()) {
				data.active = langDayOfWeek(lastDate);
			} else {
				data.active = lastDate.toString(qsl("d.MM.yy"));
			}
			data.activeWidth = st::sessionActiveFont->width(data.active);
			int32 availForName = availOther - st::sessionPadding.right() - data.activeWidth;
			if (data.nameWidth > availForName) {
				data.name = st::sessionNameFont->elided(data.name, availForName);
				data.nameWidth = st::sessionNameFont->width(data.name);
			}
			data.infoWidth = st::sessionInfoFont->width(data.info);
			if (data.infoWidth > availOther) {
				data.info = st::sessionInfoFont->elided(data.info, availOther);
				data.infoWidth = st::sessionInfoFont->width(data.info);
			}
			data.ipWidth = st::sessionInfoFont->width(data.ip);
			if (data.ipWidth > availOther) {
				data.ip = st::sessionInfoFont->elided(data.ip, availOther);
				data.ipWidth = st::sessionInfoFont->width(data.ip);
			}

			_list.push_back(data);
			for (int32 i = _list.size(); i > 1;) {
				--i;
				if (_list.at(i).activeTime > _list.at(i - 1).activeTime) {
					qSwap(_list[i], _list[i - 1]);
				}
			}
		}
	}
	_inner.listUpdated();
	if (!_done.isHidden()) {
		showAll();
		update();
	}

	_shortPollTimer.start(SessionsShortPollTimeout);
}

void SessionsBox::onOneTerminated() {
	if (_list.isEmpty()) {
		if (!_done.isHidden()) {
			showAll();
			update();
		}
	}
}

void SessionsBox::onShortPollAuthorizations() {
	if (!_shortPollRequest) {
		_shortPollRequest = MTP::send(MTPaccount_GetAuthorizations(), rpcDone(&SessionsBox::gotAuthorizations));
		if (!_done.isHidden()) {
			showAll();
			update();
		}
	}
}

void SessionsBox::onNewAuthorization() {
	onShortPollAuthorizations();
//	_shortPollTimer.start(1000);
}

void SessionsBox::onAllTerminated() {
	MTP::send(MTPaccount_GetAuthorizations(), rpcDone(&SessionsBox::gotAuthorizations));
	if (_shortPollRequest) {
		MTP::cancel(_shortPollRequest);
		_shortPollRequest = 0;
	}
}

void SessionsBox::onTerminateAll() {
	_loading = true;
	if (!_done.isHidden()) {
		showAll();
		update();
	}
}