/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/sessions_box.h"

#include "lang/lang_keys.h"
#include "storage/localstorage.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "countries.h"
#include "boxes/confirm_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/scroll_area.h"
#include "styles/style_boxes.h"

SessionsBox::SessionsBox(QWidget*)
: _shortPollTimer(this) {
}

void SessionsBox::prepare() {
	setTitle(langFactory(lng_sessions_other_header));

	addButton(langFactory(lng_close), [this] { closeBox(); });

	setDimensions(st::boxWideWidth, st::sessionsHeight);

	connect(_inner, SIGNAL(oneTerminated()), this, SLOT(onOneTerminated()));
	connect(_inner, SIGNAL(allTerminated()), this, SLOT(onAllTerminated()));
	connect(_inner, SIGNAL(terminateAll()), this, SLOT(onTerminateAll()));
	connect(App::wnd(), SIGNAL(checkNewAuthorization()), this, SLOT(onCheckNewAuthorization()));
	connect(_shortPollTimer, SIGNAL(timeout()), this, SLOT(onShortPollAuthorizations()));

	_inner = setInnerWidget(object_ptr<Inner>(this, &_list, &_current), st::sessionsScroll);
	_inner->resize(width(), st::noContactsHeight);

	setLoading(true);

	MTP::send(MTPaccount_GetAuthorizations(), rpcDone(&SessionsBox::gotAuthorizations));
}

void SessionsBox::setLoading(bool loading) {
	if (_loading != loading) {
		_loading = loading;
		setInnerVisible(!_loading);
	}
}

void SessionsBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	_inner->resize(width(), _inner->height());
}

void SessionsBox::paintEvent(QPaintEvent *e) {
	BoxContent::paintEvent(e);

	Painter p(this);

	if (_loading) {
		p.setFont(st::noContactsFont);
		p.setPen(st::noContactsColor);
		p.drawText(QRect(0, 0, width(), st::noContactsHeight), lang(lng_contacts_loading), style::al_center);
	}
}

void SessionsBox::gotAuthorizations(const MTPaccount_Authorizations &result) {
	_shortPollRequest = 0;
	setLoading(false);

	auto availCurrent = st::boxWideWidth - st::sessionPadding.left() - st::sessionTerminateSkip;
	auto availOther = availCurrent - st::sessionTerminate.iconPosition.x();// -st::sessionTerminate.width - st::sessionTerminateSkip;

	_list.clear();
	if (result.type() != mtpc_account_authorizations) {
		return;
	}
	auto &v = result.c_account_authorizations().vauthorizations.v;
	_list.reserve(v.size());

	const CountriesByISO2 &countries(countriesByISO2());

	for_const (auto &auth, v) {
		if (auth.type() != mtpc_authorization) {
			continue;
		}
		auto &d = auth.c_authorization();
		Data data;
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
			//} else {
			//	appVer = QString();
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
			data.activeWidth = st::sessionWhenFont->width(lang(lng_sessions_header));
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
			data.activeWidth = st::sessionWhenFont->width(data.active);
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
	_inner->listUpdated();

	update();

	_shortPollTimer->start(SessionsShortPollTimeout);
}

void SessionsBox::onOneTerminated() {
	update();
}

void SessionsBox::onShortPollAuthorizations() {
	if (!_shortPollRequest) {
		_shortPollRequest = MTP::send(MTPaccount_GetAuthorizations(), rpcDone(&SessionsBox::gotAuthorizations));
		update();
	}
}

void SessionsBox::onCheckNewAuthorization() {
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
	setLoading(true);
}

SessionsBox::Inner::Inner(QWidget *parent, SessionsBox::List *list, SessionsBox::Data *current) : TWidget(parent)
, _list(list)
, _current(current)
, _terminateAll(this, lang(lng_sessions_terminate_all), st::sessionTerminateAllButton) {
	connect(_terminateAll, SIGNAL(clicked()), this, SLOT(onTerminateAll()));
	_terminateAll->hide();
	setAttribute(Qt::WA_OpaquePaintEvent);
}

void SessionsBox::Inner::paintEvent(QPaintEvent *e) {
	QRect r(e->rect());
	Painter p(this);

	p.fillRect(r, st::boxBg);
	int32 x = st::sessionPadding.left(), xact = st::sessionTerminateSkip + st::sessionTerminate.iconPosition.x();// st::sessionTerminateSkip + st::sessionTerminate.width + st::sessionTerminateSkip;
	int32 w = width();

	if (_current->active.isEmpty() && _list->isEmpty()) {
		p.setFont(st::noContactsFont->f);
		p.setPen(st::noContactsColor->p);
		p.drawText(QRect(0, 0, width(), st::noContactsHeight), lang(lng_contacts_loading), style::al_center);
		return;
	}

	if (r.y() <= st::sessionCurrentHeight) {
		p.translate(0, st::sessionCurrentPadding.top());
		p.setFont(st::sessionNameFont);
		p.setPen(st::sessionNameFg);
		p.drawTextLeft(x, st::sessionPadding.top(), w, _current->name, _current->nameWidth);

		p.setFont(st::sessionWhenFont);
		p.setPen(st::sessionWhenFg);
		p.drawTextRight(x, st::sessionPadding.top(), w, _current->active, _current->activeWidth);

		p.setFont(st::sessionInfoFont);
		p.setPen(st::boxTextFg);
		p.drawTextLeft(x, st::sessionPadding.top() + st::sessionNameFont->height, w, _current->info, _current->infoWidth);
		p.setPen(st::sessionInfoFg);
		p.drawTextLeft(x, st::sessionPadding.top() + st::sessionNameFont->height + st::sessionInfoFont->height, w, _current->ip, _current->ipWidth);
	}
	p.translate(0, st::sessionCurrentHeight - st::sessionCurrentPadding.top());
	if (_list->isEmpty()) {
		p.setFont(st::sessionInfoFont);
		p.setPen(st::sessionInfoFg);
		p.drawText(QRect(st::sessionPadding.left(), 0, width() - st::sessionPadding.left() - st::sessionPadding.right(), st::noContactsHeight), lang(lng_sessions_other_desc), style::al_topleft);
		return;
	}

	p.setFont(st::linkFont->f);
	int32 count = _list->size();
	int32 from = floorclamp(r.y() - st::sessionCurrentHeight, st::sessionHeight, 0, count);
	int32 to = ceilclamp(r.y() + r.height() - st::sessionCurrentHeight, st::sessionHeight, 0, count);
	p.translate(0, from * st::sessionHeight);
	for (int32 i = from; i < to; ++i) {
		const SessionsBox::Data &auth(_list->at(i));

		p.setFont(st::sessionNameFont);
		p.setPen(st::sessionNameFg);
		p.drawTextLeft(x, st::sessionPadding.top(), w, auth.name, auth.nameWidth);

		p.setFont(st::sessionWhenFont);
		p.setPen(st::sessionWhenFg);
		p.drawTextRight(xact, st::sessionPadding.top(), w, auth.active, auth.activeWidth);

		p.setFont(st::sessionInfoFont);
		p.setPen(st::boxTextFg);
		p.drawTextLeft(x, st::sessionPadding.top() + st::sessionNameFont->height, w, auth.info, auth.infoWidth);
		p.setPen(st::sessionInfoFg);
		p.drawTextLeft(x, st::sessionPadding.top() + st::sessionNameFont->height + st::sessionInfoFont->height, w, auth.ip, auth.ipWidth);

		p.translate(0, st::sessionHeight);
	}
}

void SessionsBox::Inner::onTerminate() {
	for (auto i = _terminateButtons.begin(), e = _terminateButtons.end(); i != e; ++i) {
		if (i.value()->isOver()) {
			if (_terminateBox) _terminateBox->deleteLater();
			_terminateBox = Ui::show(Box<ConfirmBox>(lang(lng_settings_reset_one_sure), lang(lng_settings_reset_button), st::attentionBoxButton, base::lambda_guarded(this, [this, terminating = i.key()] {
				if (_terminateBox) {
					_terminateBox->closeBox();
					_terminateBox = nullptr;
				}
				MTP::send(MTPaccount_ResetAuthorization(MTP_long(terminating)), rpcDone(&Inner::terminateDone, terminating), rpcFail(&Inner::terminateFail, terminating));
				auto i = _terminateButtons.find(terminating);
				if (i != _terminateButtons.cend()) {
					i.value()->clearState();
					i.value()->hide();
				}
			})), LayerOption::KeepOther);
		}
	}
}

void SessionsBox::Inner::onTerminateAll() {
	if (_terminateBox) _terminateBox->deleteLater();
	_terminateBox = Ui::show(Box<ConfirmBox>(lang(lng_settings_reset_sure), lang(lng_settings_reset_button), st::attentionBoxButton, base::lambda_guarded(this, [this] {
		if (_terminateBox) {
			_terminateBox->closeBox();
			_terminateBox = nullptr;
		}
		MTP::send(MTPauth_ResetAuthorizations(), rpcDone(&Inner::terminateAllDone), rpcFail(&Inner::terminateAllFail));
		emit terminateAll();
	})), LayerOption::KeepOther);
}

void SessionsBox::Inner::terminateDone(uint64 hash, const MTPBool &result) {
	for (int32 i = 0, l = _list->size(); i < l; ++i) {
		if (_list->at(i).hash == hash) {
			_list->removeAt(i);
			break;
		}
	}
	listUpdated();
	emit oneTerminated();
}

bool SessionsBox::Inner::terminateFail(uint64 hash, const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	TerminateButtons::iterator i = _terminateButtons.find(hash);
	if (i != _terminateButtons.end()) {
		i.value()->show();
		return true;
	}
	return false;
}

void SessionsBox::Inner::terminateAllDone(const MTPBool &result) {
	emit allTerminated();
}

bool SessionsBox::Inner::terminateAllFail(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;
	emit allTerminated();
	return true;
}

void SessionsBox::Inner::resizeEvent(QResizeEvent *e) {
	_terminateAll->moveToLeft(st::sessionPadding.left(), st::sessionCurrentPadding.top() + st::sessionHeight + st::sessionCurrentPadding.bottom());
}

void SessionsBox::Inner::listUpdated() {
	if (_list->isEmpty()) {
		_terminateAll->hide();
	} else {
		_terminateAll->show();
	}
	for (TerminateButtons::iterator i = _terminateButtons.begin(), e = _terminateButtons.end(); i != e; ++i) {
		i.value()->move(0, -1);
	}
	for (int32 i = 0, l = _list->size(); i < l; ++i) {
		TerminateButtons::iterator j = _terminateButtons.find(_list->at(i).hash);
		if (j == _terminateButtons.cend()) {
			j = _terminateButtons.insert(_list->at(i).hash, new Ui::IconButton(this, st::sessionTerminate));
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
