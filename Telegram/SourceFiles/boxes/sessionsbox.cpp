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

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "lang.h"

#include "localstorage.h"

#include "sessionsbox.h"
#include "mainwidget.h"
#include "window.h"

#include "countries.h"
#include "confirmbox.h"

SessionsInner::SessionsInner(SessionsList *list) : _list(list), _terminating(0), _terminateBox(0) {
}

void SessionsInner::paintEvent(QPaintEvent *e) {
	QRect r(e->rect());
	Painter p(this);

	p.fillRect(r, st::white->b);
	p.setFont(st::linkFont->f);
	int32 x = st::sessionPadding.left(), xact = st::sessionTerminateSkip + st::sessionTerminate.width + st::sessionTerminateSkip;
	int32 w = width() - 2 * x, availw = width() - 2 * xact;
	int32 from = (r.top() >= 0) ? qFloor(r.top() / st::sessionHeight) : 0, count = _list->size();
	if (from < count) {
		int32 to = (r.bottom() >= 0 ? qFloor(r.bottom() / st::sessionHeight) : 0) + 1;
		if (to > count) to = count;
		p.translate(0, from * st::sessionHeight);
		for (int32 i = from; i < to; ++i) {
			const SessionData &auth(_list->at(i));

			p.setFont(st::sessionNameFont->f);
			p.setPen(st::black->p);
			p.drawTextLeft(x, st::sessionPadding.top(), w, auth.name, auth.nameWidth);

			p.setFont(st::sessionActiveFont->f);
			p.setPen(st::sessionActiveColor->p);
			p.drawTextRight(xact, st::sessionPadding.top(), availw, auth.active, auth.activeWidth);

			p.setFont(st::sessionInfoFont->f);
			p.setPen(st::sessionInfoColor->p);
			p.drawTextLeft(x, st::sessionPadding.top() + st::sessionNameFont->height, w, auth.info, auth.infoWidth);

			p.translate(0, st::sessionHeight);
		}
	}
}

void SessionsInner::onTerminate() {
	for (TerminateButtons::iterator i = _terminateButtons.begin(), e = _terminateButtons.end(); i != e; ++i) {
		if (i.value()->getState() & Button::StateOver) {
			_terminating = i.key();

			if (_terminateBox) _terminateBox->deleteLater();
			_terminateBox = new ConfirmBox(lang(lng_settings_reset_one_sure), lang(lng_settings_reset_button));
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
	if (error.type().startsWith(qsl("FLOOD_WAIT_"))) return false;

	TerminateButtons::iterator i = _terminateButtons.find(hash);
	if (i != _terminateButtons.end()) {
		i.value()->show();
		return true;
	}
	return false;
}

void SessionsInner::resizeEvent(QResizeEvent *e) {

}

void SessionsInner::listUpdated() {
	for (TerminateButtons::iterator i = _terminateButtons.begin(), e = _terminateButtons.end(); i != e; ++i) {
		i.value()->move(0, -1);
	}
	for (int32 i = 0, l = _list->size(); i < l; ++i) {
		TerminateButtons::iterator j = _terminateButtons.find(_list->at(i).hash);
		if (j == _terminateButtons.cend()) {
			j = _terminateButtons.insert(_list->at(i).hash, new IconedButton(this, st::sessionTerminate));
			connect(j.value(), SIGNAL(clicked()), this, SLOT(onTerminate()));
		}
		j.value()->moveToRight(st::sessionTerminateSkip, i * st::sessionHeight + st::sessionTerminateTop, width() - 2 * st::sessionTerminateSkip);
	}
	for (TerminateButtons::iterator i = _terminateButtons.begin(); i != _terminateButtons.cend();) {
		if (i.value()->y() >= 0) {
			++i;
		} else {
			delete i.value();
			i = _terminateButtons.erase(i);
		}
	}
	resize(width(), _list->isEmpty() ? st::noContactsHeight : (_list->size() * st::sessionHeight));
	if (parentWidget()) parentWidget()->update();
}

SessionsInner::~SessionsInner() {
	for (int32 i = 0, l = _terminateButtons.size(); i < l; ++i) {
		delete _terminateButtons[i];
	}
}

SessionsBox::SessionsBox() : ScrollableBox(st::boxScroll), _loading(true), _inner(&_list),
_done(this, lang(lng_about_done), st::sessionsCloseButton),
_terminateAll(this, lang(lng_sessions_terminate_all)), _terminateBox(0), _shortPollRequest(0) {
	setMaxHeight(st::sessionsHeight);

	connect(&_done, SIGNAL(clicked()), this, SLOT(onClose()));
	connect(&_terminateAll, SIGNAL(clicked()), this, SLOT(onTerminateAll()));
	connect(&_inner, SIGNAL(oneTerminated()), this, SLOT(onOneTerminated()));
	connect(App::wnd(), SIGNAL(newAuthorization()), this, SLOT(onNewAuthorization()));
	connect(&_shortPollTimer, SIGNAL(timeout()), this, SLOT(onShortPollAuthorizations()));

	init(&_inner, _done.height(), st::boxTitleHeight + st::sessionHeight + st::boxTitleHeight);
	_inner.resize(width(), st::noContactsHeight);

	prepare();

	_scroll.hide();
	MTP::send(MTPaccount_GetAuthorizations(), rpcDone(&SessionsBox::gotAuthorizations));
}

void SessionsBox::resizeEvent(QResizeEvent *e) {
	ScrollableBox::resizeEvent(e);
	_done.move(0, height() - _done.height());
	_terminateAll.moveToRight(st::sessionPadding.left(), st::boxTitleHeight + st::sessionHeight + st::boxTitlePos.y() + st::boxTitleFont->ascent - st::linkFont->ascent, width() - 2 * st::sessionPadding.left());
}

void SessionsBox::hideAll() {
	_done.hide();
	_terminateAll.hide();
	ScrollableBox::hideAll();
}

void SessionsBox::showAll() {
	_done.show();
	if (_list.isEmpty()) {
		_terminateAll.hide();
		_scroll.hide();
	} else {
		_terminateAll.show();
		if (_loading) {
			_scroll.hide();
		} else {
			_scroll.show();
		}
	}
}

void SessionsBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (paint(p)) return;

	paintTitle(p, lang(lng_sessions_header), true);
	p.translate(0, st::boxTitleHeight);

	if (_loading) {
		p.setFont(st::noContactsFont->f);
		p.setPen(st::noContactsColor->p);
		p.drawText(QRect(0, 0, width(), st::noContactsHeight), lang(lng_contacts_loading), style::al_center);
	} else {
		int32 x = st::sessionPadding.left();
		int32 w = width() - x - st::sessionPadding.right();

		p.setFont(st::sessionNameFont->f);
		p.setPen(st::black->p);
		p.drawTextLeft(x, st::sessionPadding.top(), w, _current.name, _current.nameWidth);

		p.setFont(st::sessionActiveFont->f);
		p.setPen(st::sessionActiveColor->p);
		p.drawTextRight(x, st::sessionPadding.top(), w, _current.active, _current.activeWidth);

		p.setFont(st::sessionInfoFont->f);
		p.setPen(st::sessionInfoColor->p);
		p.drawTextLeft(x, st::sessionPadding.top() + st::sessionNameFont->height, w, _current.info, _current.infoWidth);

		p.translate(0, st::sessionHeight);
		if (_list.isEmpty()) {
			paintTitle(p, lang(lng_sessions_no_other), true);

			p.setFont(st::sessionInfoFont->f);
			p.setPen(st::sessionInfoColor->p);
			p.drawText(QRect(st::sessionPadding.left(), st::boxTitleHeight + st::boxTitlePos.y(), width() - st::sessionPadding.left() - st::sessionPadding.right(), _scroll.height()), lang(lng_sessions_other_desc), style::al_topleft);

			// paint shadow
			p.fillRect(0, height() - st::sessionsCloseButton.height - st::scrollDef.bottomsh - st::sessionHeight - st::boxTitleHeight, width(), st::scrollDef.bottomsh, st::scrollDef.shColor->b);
		} else {
			paintTitle(p, lang(lng_sessions_other_header), false);
		}
	}
}

void SessionsBox::gotAuthorizations(const MTPaccount_Authorizations &result) {
	_loading = false;
	_shortPollRequest = 0;

	int32 availCurrent = st::boxWidth - st::sessionPadding.left() - st::sessionTerminateSkip;
	int32 availOther = availCurrent - st::sessionTerminate.width - st::sessionTerminateSkip;

	_list.clear();
	const QVector<MTPAuthorization> &v(result.c_account_authorizations().vauthorizations.c_vector().v);
	int32 l = v.size();
	if (l > 1) _list.reserve(l - 1);

	const CountriesByISO2 &countries(countriesByISO2());

	for (int32 i = 0; i < l; ++i) {
		const MTPDauthorization &d(v.at(i).c_authorization());
		SessionData data;
		data.hash = d.vhash.v;

		QString appName, systemVer = qs(d.vsystem_version), deviceModel = qs(d.vdevice_model);
		if (d.vapi_id.v == 17349) {
			appName = qs(d.vapp_name);// (d.vapi_id.v == 2040) ? qsl("Telegram Desktop") : qsl("Telegram Desktop (GitHub)");
			if (systemVer == QLatin1String("windows")) {
				deviceModel = qsl("Windows");
			} else if (systemVer == QLatin1String("os x")) {
				deviceModel = qsl("Mac OS X");
			} else if (systemVer == QLatin1String("linux")) {
				deviceModel = qsl("Linux");
			}
		} else {
			appName = qs(d.vapp_name);// +qsl(" for ") + qs(d.vplatform);
		}
		data.name = appName;
		data.nameWidth = st::sessionNameFont->m.width(data.name);

		QString country = qs(d.vcountry);
		CountriesByISO2::const_iterator j = countries.constFind(country);
		if (j != countries.cend()) country = QString::fromUtf8(j.value()->name);

		MTPint active = d.vdate_active.v ? d.vdate_active : d.vdate_created;
		data.activeTime = active.v;
		data.info = country + QLatin1String(" (") + qs(d.vip) + QLatin1String("), ") + deviceModel;
		if (!data.hash || (d.vflags.v & 1)) {
			data.active = QString();
			data.activeWidth = 0;
			if (data.nameWidth > availCurrent) {
				data.name = st::sessionNameFont->m.elidedText(data.name, Qt::ElideRight, availCurrent);
				data.nameWidth = st::sessionNameFont->m.width(data.name);
			}
			data.infoWidth = st::sessionInfoFont->m.width(data.info);
			if (data.infoWidth > availCurrent) {
				data.info = st::sessionInfoFont->m.elidedText(data.info, Qt::ElideRight, availCurrent);
				data.infoWidth = st::sessionInfoFont->m.width(data.info);
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
			data.activeWidth = st::sessionActiveFont->m.width(data.active);
			int32 availForName = availOther - st::sessionPadding.right() - data.activeWidth;
			if (data.nameWidth > availForName) {
				data.name = st::sessionNameFont->m.elidedText(data.name, Qt::ElideRight, availForName);
				data.nameWidth = st::sessionNameFont->m.width(data.name);
			}
			data.infoWidth = st::sessionInfoFont->m.width(data.info);
			if (data.infoWidth > availOther) {
				data.info = st::sessionInfoFont->m.elidedText(data.info, Qt::ElideRight, availOther);
				data.infoWidth = st::sessionInfoFont->m.width(data.info);
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

void SessionsBox::onTerminateAll() {
	if (_terminateBox) _terminateBox->deleteLater();
	_terminateBox = new ConfirmBox(lang(lng_settings_reset_sure), lang(lng_settings_reset_button));
	connect(_terminateBox, SIGNAL(confirmed()), this, SLOT(onTerminateAllSure()));
	connect(_terminateBox, SIGNAL(destroyed(QObject*)), this, SLOT(onNoTerminateBox(QObject*)));
	App::wnd()->replaceLayer(_terminateBox);
}

void SessionsBox::onTerminateAllSure() {
	if (_terminateBox) {
		_terminateBox->onClose();
		_terminateBox = 0;
	}
	MTP::send(MTPauth_ResetAuthorizations(), rpcDone(&SessionsBox::terminateAllDone), rpcFail(&SessionsBox::terminateAllFail));
	_loading = true;
	if (!_done.isHidden()) {
		showAll();
		update();
	}
}

void SessionsBox::onNoTerminateBox(QObject *obj) {
	if (obj == _terminateBox) _terminateBox = 0;
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

void SessionsBox::terminateAllDone(const MTPBool &result) {
	MTP::send(MTPaccount_GetAuthorizations(), rpcDone(&SessionsBox::gotAuthorizations));
	if (_shortPollRequest) {
		MTP::cancel(_shortPollRequest);
		_shortPollRequest = 0;
	}
}

bool SessionsBox::terminateAllFail(const RPCError &error) {
	if (error.type().startsWith(qsl("FLOOD_WAIT_"))) return false;

	MTP::send(MTPaccount_GetAuthorizations(), rpcDone(&SessionsBox::gotAuthorizations));
	if (_shortPollRequest) {
		MTP::cancel(_shortPollRequest);
		_shortPollRequest = 0;
	}
	return true;
}
