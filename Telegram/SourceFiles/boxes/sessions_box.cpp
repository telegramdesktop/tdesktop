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
#include "main/main_session.h"
#include "data/data_session.h"
#include "base/unixtime.h"
#include "boxes/confirm_box.h"
#include "settings/settings_common.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"
#include "styles/style_settings.h"

namespace {

constexpr auto kSessionsShortPollTimeout = 60 * crl::time(1000);

} // namespace

class SessionsBox::List : public Ui::RpWidget {
public:
	List(QWidget *parent);

	void showData(gsl::span<const Entry> items);
	rpl::producer<int> itemsCount() const;
	rpl::producer<uint64> terminate() const;

	void terminating(uint64 hash, bool terminating);

protected:
	void paintEvent(QPaintEvent *e) override;

	int resizeGetHeight(int newWidth) override;

private:
	std::vector<Entry> _items;
	std::map<uint64, std::unique_ptr<Ui::IconButton>> _terminateButtons;
	rpl::event_stream<uint64> _terminate;
	rpl::event_stream<int> _itemsCount;

};

class SessionsBox::Inner : public Ui::RpWidget {
public:
	Inner(QWidget *parent);

	void showData(const Full &data);
	rpl::producer<uint64> terminateOne() const;
	rpl::producer<> terminateAll() const;

	void terminatingOne(uint64 hash, bool terminating);

private:
	void setupContent();

	QPointer<List> _current;
	QPointer<Ui::SettingsButton> _terminateAll;
	QPointer<List> _incomplete;
	QPointer<List> _list;

};

SessionsBox::SessionsBox(QWidget*, not_null<Main::Session*> session)
: _session(session)
, _api(&_session->mtp())
, _shortPollTimer([=] { shortPollSessions(); }) {
}

void SessionsBox::prepare() {
	setTitle(tr::lng_sessions_other_header());

	addButton(tr::lng_close(), [=] { closeBox(); });

	setDimensions(st::boxWideWidth, st::sessionsHeight);

	_inner = setInnerWidget(object_ptr<Inner>(this), st::sessionsScroll);
	_inner->resize(width(), st::noContactsHeight);

	_inner->terminateOne(
	) | rpl::start_with_next([=](uint64 hash) {
		terminateOne(hash);
	}, lifetime());

	_inner->terminateAll(
	) | rpl::start_with_next([=] {
		terminateAll();
	}, lifetime());

	_session->data().newAuthorizationChecks(
	) | rpl::start_with_next([=] {
		shortPollSessions();
	}, lifetime());

	_loading.changes(
	) | rpl::start_with_next([=](bool value) {
		setInnerVisible(!value);
	}, lifetime());

	_loading = true;
	shortPollSessions();
}

void SessionsBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	_inner->resize(width(), _inner->height());
}

void SessionsBox::paintEvent(QPaintEvent *e) {
	BoxContent::paintEvent(e);

	Painter p(this);

	if (_loading.current()) {
		p.setFont(st::noContactsFont);
		p.setPen(st::noContactsColor);
		p.drawText(
			QRect(0, 0, width(), st::noContactsHeight),
			tr::lng_contacts_loading(tr::now),
			style::al_center);
	}
}

void SessionsBox::got(const MTPaccount_Authorizations &result) {
	_shortPollRequest = 0;
	_loading = false;
	_data = Full();

	result.match([&](const MTPDaccount_authorizations &data) {
		const auto &list = data.vauthorizations().v;
		for (const auto &auth : list) {
			auth.match([&](const MTPDauthorization &data) {
				auto entry = ParseEntry(data);
				if (!entry.hash) {
					_data.current = std::move(entry);
				} else if (entry.incomplete) {
					_data.incomplete.push_back(std::move(entry));
				} else {
					_data.list.push_back(std::move(entry));
				}
			});
		}
	});

	const auto getActiveTime = [](const Entry &entry) {
		return entry.activeTime;
	};
	ranges::sort(_data.list, std::greater<>(), getActiveTime);
	ranges::sort(_data.incomplete, std::greater<>(), getActiveTime);

	_inner->showData(_data);

	_shortPollTimer.callOnce(kSessionsShortPollTimeout);
}

SessionsBox::Entry SessionsBox::ParseEntry(const MTPDauthorization &data) {
	auto result = Entry();

	result.hash = data.is_current() ? 0 : data.vhash().v;
	result.incomplete = data.is_password_pending();

	auto appName = QString();
	auto appVer = qs(data.vapp_version());
	const auto systemVer = qs(data.vsystem_version());
	const auto deviceModel = qs(data.vdevice_model());
	const auto apiId = data.vapi_id().v;
	if (apiId == 2040 || apiId == 17349) {
		appName = (apiId == 2040)
			? qstr("Telegram Desktop")
			: qstr("Telegram Desktop (GitHub)");
		//if (systemVer == qstr("windows")) {
		//	deviceModel = qsl("Windows");
		//} else if (systemVer == qstr("os x")) {
		//	deviceModel = qsl("OS X");
		//} else if (systemVer == qstr("linux")) {
		//	deviceModel = qsl("Linux");
		//}
		if (appVer == QString::number(appVer.toInt())) {
			const auto ver = appVer.toInt();
			appVer = QString("%1.%2"
			).arg(ver / 1000000
			).arg((ver % 1000000) / 1000)
				+ ((ver % 1000)
					? ('.' + QString::number(ver % 1000))
					: QString());
		//} else {
		//	appVer = QString();
		}
	} else {
		appName = qs(data.vapp_name());// +qsl(" for ") + qs(d.vplatform());
		if (appVer.indexOf('(') >= 0) {
			appVer = appVer.mid(appVer.indexOf('('));
		}
	}
	result.name = appName;
	if (!appVer.isEmpty()) {
		result.name += ' ' + appVer;
	}

	const auto country = qs(data.vcountry());
	const auto platform = qs(data.vplatform());
	//const auto &countries = countriesByISO2();
	//const auto j = countries.constFind(country);
	//if (j != countries.cend()) {
	//	country = QString::fromUtf8(j.value()->name);
	//}

	result.activeTime = data.vdate_active().v
		? data.vdate_active().v
		: data.vdate_created().v;
	result.info = qs(data.vdevice_model()) + qstr(", ") + (platform.isEmpty() ? QString() : platform + ' ') + qs(data.vsystem_version());
	result.ip = qs(data.vip()) + (country.isEmpty() ? QString() : QString::fromUtf8(" \xe2\x80\x93 ") + country);
	if (!result.hash) {
		result.active = tr::lng_status_online(tr::now);
		result.activeWidth = st::sessionWhenFont->width(tr::lng_status_online(tr::now));
	} else {
		const auto now = QDateTime::currentDateTime();
		const auto lastTime = base::unixtime::parse(result.activeTime);
		const auto nowDate = now.date();
		const auto lastDate = lastTime.date();
		if (lastDate == nowDate) {
			result.active = lastTime.toString(cTimeFormat());
		} else if (lastDate.year() == nowDate.year()
			&& lastDate.weekNumber() == nowDate.weekNumber()) {
			result.active = langDayOfWeek(lastDate);
		} else {
			result.active = lastDate.toString(qsl("d.MM.yy"));
		}
		result.activeWidth = st::sessionWhenFont->width(result.active);
	}

	ResizeEntry(result);

	return result;
}

void SessionsBox::ResizeEntry(Entry &entry) {
	const auto available = st::boxWideWidth
		- st::sessionPadding.left()
		- st::sessionTerminateSkip;
	const auto availableInList = available
		- st::sessionTerminate.iconPosition.x();
	const auto availableListInfo = available - st::sessionTerminate.width;

	const auto resize = [](
			const style::font &font,
			QString &string,
			int &stringWidth,
			int available) {
		stringWidth = font->width(string);
		if (stringWidth > available) {
			string = font->elided(string, available);
			stringWidth = font->width(string);
		}
	};
	const auto forName = entry.hash ? availableInList : available;
	const auto forInfo = entry.hash ? availableListInfo : available;
	resize(st::sessionNameFont, entry.name, entry.nameWidth, forName);
	resize(st::sessionInfoFont, entry.info, entry.infoWidth, forInfo);
	resize(st::sessionInfoFont, entry.ip, entry.ipWidth, available);
}

void SessionsBox::shortPollSessions() {
	if (_shortPollRequest) {
		return;
	}
	_shortPollRequest = _api.request(MTPaccount_GetAuthorizations(
	)).done([=](const MTPaccount_Authorizations &result) {
		got(result);
	}).send();
	update();
}

void SessionsBox::terminateOne(uint64 hash) {
	if (_terminateBox) _terminateBox->deleteLater();
	const auto callback = crl::guard(this, [=] {
		if (_terminateBox) {
			_terminateBox->closeBox();
			_terminateBox = nullptr;
		}
		_api.request(MTPaccount_ResetAuthorization(
			MTP_long(hash)
		)).done([=](const MTPBool &result) {
			_inner->terminatingOne(hash, false);
			const auto getHash = [](const Entry &entry) {
				return entry.hash;
			};
			const auto removeByHash = [&](std::vector<Entry> &list) {
				list.erase(
					ranges::remove(list, hash, getHash),
					end(list));
			};
			removeByHash(_data.incomplete);
			removeByHash(_data.list);
			_inner->showData(_data);
		}).fail([=](const RPCError &error) {
			_inner->terminatingOne(hash, false);
		}).send();
		_inner->terminatingOne(hash, true);
	});
	_terminateBox = Ui::show(
		Box<ConfirmBox>(
			tr::lng_settings_reset_one_sure(tr::now),
			tr::lng_settings_reset_button(tr::now),
			st::attentionBoxButton,
			callback),
		Ui::LayerOption::KeepOther);
}

void SessionsBox::terminateAll() {
	if (_terminateBox) _terminateBox->deleteLater();
	const auto callback = crl::guard(this, [=] {
		if (_terminateBox) {
			_terminateBox->closeBox();
			_terminateBox = nullptr;
		}
		_api.request(MTPauth_ResetAuthorizations(
		)).done([=](const MTPBool &result) {
			_api.request(base::take(_shortPollRequest)).cancel();
			shortPollSessions();
		}).fail([=](const RPCError &result) {
			_api.request(base::take(_shortPollRequest)).cancel();
			shortPollSessions();
		}).send();
		_loading = true;
	});
	_terminateBox = Ui::show(
		Box<ConfirmBox>(
			tr::lng_settings_reset_sure(tr::now),
			tr::lng_settings_reset_button(tr::now),
			st::attentionBoxButton,
			callback),
		Ui::LayerOption::KeepOther);
}

SessionsBox::Inner::Inner(QWidget *parent)
: RpWidget(parent) {
	setupContent();
}

void SessionsBox::Inner::setupContent() {
	using namespace Settings;
	using namespace rpl::mappers;

	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	AddSubsectionTitle(content, tr::lng_sessions_header());
	_current = content->add(object_ptr<List>(content));
	const auto terminateWrap = content->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			content,
			object_ptr<Ui::VerticalLayout>(content)))->setDuration(0);
	const auto terminateInner = terminateWrap->entity();
	_terminateAll = terminateInner->add(
		object_ptr<Ui::SettingsButton>(
			terminateInner,
			tr::lng_sessions_terminate_all(),
			st::terminateSessionsButton));
	AddSkip(terminateInner);
	AddDividerText(terminateInner, tr::lng_sessions_terminate_all_about());

	const auto incompleteWrap = content->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			content,
			object_ptr<Ui::VerticalLayout>(content)))->setDuration(0);
	const auto incompleteInner = incompleteWrap->entity();
	AddSkip(incompleteInner);
	AddSubsectionTitle(incompleteInner, tr::lng_sessions_incomplete());
	_incomplete = incompleteInner->add(object_ptr<List>(incompleteInner));
	AddSkip(incompleteInner);
	AddDividerText(incompleteInner, tr::lng_sessions_incomplete_about());

	const auto listWrap = content->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			content,
			object_ptr<Ui::VerticalLayout>(content)))->setDuration(0);
	const auto listInner = listWrap->entity();
	AddSkip(listInner);
	AddSubsectionTitle(listInner, tr::lng_sessions_other_header());
	_list = listInner->add(object_ptr<List>(listInner));
	AddSkip(listInner);

	const auto placeholder = content->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			content,
			object_ptr<Ui::FlatLabel>(
				content,
				tr::lng_sessions_other_desc(),
				st::boxDividerLabel),
			st::settingsDividerLabelPadding))->setDuration(0);

	terminateWrap->toggleOn(
		rpl::combine(
			_incomplete->itemsCount(),
			_list->itemsCount(),
			(_1 + _2) > 0));
	incompleteWrap->toggleOn(_incomplete->itemsCount() | rpl::map(_1 > 0));
	listWrap->toggleOn(_list->itemsCount() | rpl::map(_1 > 0));
	placeholder->toggleOn(_list->itemsCount() | rpl::map(_1 == 0));

	Ui::ResizeFitChild(this, content);
}

void SessionsBox::Inner::showData(const Full &data) {
	_current->showData({ &data.current, &data.current + 1 });
	_list->showData(data.list);
	_incomplete->showData(data.incomplete);
}

rpl::producer<> SessionsBox::Inner::terminateAll() const {
	return _terminateAll->clicks() | rpl::to_empty;
}

rpl::producer<uint64> SessionsBox::Inner::terminateOne() const {
	return rpl::merge(
		_incomplete->terminate(),
		_list->terminate());
}

void SessionsBox::Inner::terminatingOne(uint64 hash, bool terminating) {
	_incomplete->terminating(hash, terminating);
	_list->terminating(hash, terminating);
}

SessionsBox::List::List(QWidget *parent) : RpWidget(parent) {
	setAttribute(Qt::WA_OpaquePaintEvent);
}

void SessionsBox::List::showData(gsl::span<const Entry> items) {
	auto buttons = base::take(_terminateButtons);
	_items.clear();
	_items.insert(begin(_items), items.begin(), items.end());
	for (const auto &entry : _items) {
		const auto hash = entry.hash;
		if (!hash) {
			continue;
		}
		const auto button = [&] {
			const auto i = buttons.find(hash);
			return _terminateButtons.emplace(
				hash,
				(i != end(buttons)
					? std::move(i->second)
					: std::make_unique<Ui::IconButton>(
						this,
						st::sessionTerminate))).first->second.get();
		}();
		button->setClickedCallback([=] {
			_terminate.fire_copy(hash);
		});
		button->show();
		button->moveToRight(
			st::sessionTerminateSkip,
			((_terminateButtons.size() - 1) * st::sessionHeight
				+ st::sessionTerminateTop));
	}
	resizeToWidth(width());
	_itemsCount.fire(_items.size());
}

rpl::producer<int> SessionsBox::List::itemsCount() const {
	return _itemsCount.events_starting_with(_items.size());
}

rpl::producer<uint64> SessionsBox::List::terminate() const {
	return _terminate.events();
}

void SessionsBox::List::terminating(uint64 hash, bool terminating) {
	const auto i = _terminateButtons.find(hash);
	if (i != _terminateButtons.cend()) {
		if (terminating) {
			i->second->clearState();
			i->second->hide();
		} else {
			i->second->show();
		}
	}
}

int SessionsBox::List::resizeGetHeight(int newWidth) {
	return _items.size() * st::sessionHeight;
}

void SessionsBox::List::paintEvent(QPaintEvent *e) {
	QRect r(e->rect());
	Painter p(this);

	p.fillRect(r, st::boxBg);
	p.setFont(st::linkFont);
	const auto count = int(_items.size());
	const auto from = floorclamp(r.y(), st::sessionHeight, 0, count);
	const auto till = ceilclamp(
		r.y() + r.height(),
		st::sessionHeight,
		0,
		count);

	const auto x = st::sessionPadding.left();
	const auto y = st::sessionPadding.top();
	const auto w = width();
	const auto xact = st::sessionTerminateSkip
		+ st::sessionTerminate.iconPosition.x();
	p.translate(0, from * st::sessionHeight);
	for (auto i = from; i != till; ++i) {
		const auto &entry = _items[i];

		p.setFont(st::sessionNameFont);
		p.setPen(st::sessionNameFg);
		p.drawTextLeft(x, y, w, entry.name, entry.nameWidth);

		p.setFont(st::sessionWhenFont);
		p.setPen(st::sessionWhenFg);
		p.drawTextRight(xact, y, w, entry.active, entry.activeWidth);

		const auto name = st::sessionNameFont->height;
		p.setFont(st::sessionInfoFont);
		p.setPen(st::boxTextFg);
		p.drawTextLeft(x, y + name, w, entry.info, entry.infoWidth);

		const auto info = st::sessionInfoFont->height;
		p.setPen(st::sessionInfoFg);
		p.drawTextLeft(x, y + name + info, w, entry.ip, entry.ipWidth);

		p.translate(0, st::sessionHeight);
	}
}
