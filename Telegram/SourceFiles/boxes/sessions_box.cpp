/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/sessions_box.h"

#include "apiwrap.h"
#include "api/api_authorizations.h"
#include "base/timer.h"
#include "base/unixtime.h"
#include "boxes/confirm_box.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

namespace {

constexpr auto kSessionsShortPollTimeout = 60 * crl::time(1000);

} // namespace

class SessionsContent : public Ui::RpWidget {
public:
	SessionsContent(QWidget*, not_null<Main::Session*> session);

	void setupContent();

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private:
	struct Entry {
		Entry() = default;
		Entry(const Api::Authorizations::Entry &entry)
		: hash(entry.hash)
		, incomplete(entry.incomplete)
		, activeTime(entry.activeTime)
		, name(st::sessionNameStyle, entry.name)
		, active(st::sessionWhenStyle, entry.active)
		, info(st::sessionInfoStyle, entry.info)
		, ip(st::sessionInfoStyle, entry.ip) {
		};

		uint64 hash = 0;

		bool incomplete = false;
		TimeId activeTime = 0;
		Ui::Text::String name, active, info, ip;
	};
	struct Full {
		Entry current;
		std::vector<Entry> incomplete;
		std::vector<Entry> list;
	};
	class Inner;
	class List;

	void shortPollSessions();
	void parse(const Api::Authorizations::List &list);

	void terminate(Fn<void()> terminateRequest, QString message);
	void terminateOne(uint64 hash);
	void terminateAll();

	const not_null<Api::Authorizations*> _authorizations;

	rpl::variable<bool> _loading = false;
	Full _data;

	object_ptr<Inner> _inner;
	QPointer<ConfirmBox> _terminateBox;

	base::Timer _shortPollTimer;

};

class SessionsContent::List : public Ui::RpWidget {
public:
	List(QWidget *parent);

	void showData(gsl::span<const Entry> items);
	rpl::producer<int> itemsCount() const;
	rpl::producer<uint64> terminate() const;

	void terminating(uint64 hash, bool terminating);

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

	int resizeGetHeight(int newWidth) override;

private:
	struct RowWidth {
		int available = 0;
		int info = 0;
	};
	RowWidth _rowWidth;

	void computeRowWidth();

	std::vector<Entry> _items;
	std::map<uint64, std::unique_ptr<Ui::IconButton>> _terminateButtons;
	rpl::event_stream<uint64> _terminate;
	rpl::event_stream<int> _itemsCount;

};

class SessionsContent::Inner : public Ui::RpWidget {
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

SessionsContent::SessionsContent(QWidget*, not_null<Main::Session*> session)
: _authorizations(&session->api().authorizations())
, _inner(this)
, _shortPollTimer([=] { shortPollSessions(); }) {
}

void SessionsContent::setupContent() {
	_inner->resize(width(), st::noContactsHeight);

	_inner->heightValue(
	) | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](int height) {
		resize(width(), height);
	}, _inner->lifetime());

	_inner->terminateOne(
	) | rpl::start_with_next([=](uint64 hash) {
		terminateOne(hash);
	}, lifetime());

	_inner->terminateAll(
	) | rpl::start_with_next([=] {
		terminateAll();
	}, lifetime());

	_loading.changes(
	) | rpl::start_with_next([=](bool value) {
		_inner->setVisible(!value);
	}, lifetime());

	_authorizations->listChanges(
	) | rpl::start_with_next([=](const Api::Authorizations::List &list) {
		parse(list);
	}, lifetime());

	_loading = true;
	shortPollSessions();
}

void SessionsContent::parse(const Api::Authorizations::List &list) {
	if (list.empty()) {
		return;
	}
	_data = Full();
	for (const auto &auth : list) {
		auto entry = Entry(auth);
		if (!entry.hash) {
			_data.current = std::move(entry);
		} else if (entry.incomplete) {
			_data.incomplete.push_back(std::move(entry));
		} else {
			_data.list.push_back(std::move(entry));
		}
	}

	_loading = false;

	ranges::sort(_data.list, std::greater<>(), &Entry::activeTime);
	ranges::sort(_data.incomplete, std::greater<>(), &Entry::activeTime);

	_inner->showData(_data);

	_shortPollTimer.callOnce(kSessionsShortPollTimeout);
}

void SessionsContent::resizeEvent(QResizeEvent *e) {
	RpWidget::resizeEvent(e);

	_inner->resize(width(), _inner->height());
}

void SessionsContent::paintEvent(QPaintEvent *e) {
	RpWidget::paintEvent(e);

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

void SessionsContent::shortPollSessions() {
	const auto left = kSessionsShortPollTimeout
		- (crl::now() - _authorizations->lastReceivedTime());
	if (left > 0) {
		parse(_authorizations->list());
		_shortPollTimer.cancel();
		_shortPollTimer.callOnce(left);
	} else {
		_authorizations->reload();
	}
	update();
}

void SessionsContent::terminate(Fn<void()> terminateRequest, QString message) {
	if (_terminateBox) {
		_terminateBox->deleteLater();
	}
	const auto callback = crl::guard(this, [=] {
		if (_terminateBox) {
			_terminateBox->closeBox();
			_terminateBox = nullptr;
		}
		terminateRequest();
	});
	_terminateBox = Ui::show(
		Box<ConfirmBox>(
			message,
			tr::lng_settings_reset_button(tr::now),
			st::attentionBoxButton,
			callback),
		Ui::LayerOption::KeepOther);
}

void SessionsContent::terminateOne(uint64 hash) {
	const auto weak = Ui::MakeWeak(this);
	auto callback = [=] {
		auto done = crl::guard(weak, [=](const MTPBool &result) {
			if (mtpIsFalse(result)) {
				return;
			}
			_inner->terminatingOne(hash, false);
			const auto removeByHash = [&](std::vector<Entry> &list) {
				list.erase(
					ranges::remove(list, hash, &Entry::hash),
					end(list));
			};
			removeByHash(_data.incomplete);
			removeByHash(_data.list);
			_inner->showData(_data);
		});
		auto fail = crl::guard(weak, [=](const MTP::Error &error) {
			_inner->terminatingOne(hash, false);
		});
		_authorizations->requestTerminate(
			std::move(done),
			std::move(fail),
			hash);
		_inner->terminatingOne(hash, true);
	};
	terminate(std::move(callback), tr::lng_settings_reset_one_sure(tr::now));
}

void SessionsContent::terminateAll() {
	const auto weak = Ui::MakeWeak(this);
	auto callback = [=] {
		const auto reset = crl::guard(weak, [=] {
			_authorizations->cancelCurrentRequest();
			_authorizations->reload();
		});
		_authorizations->requestTerminate(
			[=](const MTPBool &result) { reset(); },
			[=](const MTP::Error &result) { reset(); });
		_loading = true;
	};
	terminate(std::move(callback), tr::lng_settings_reset_sure(tr::now));
}

SessionsContent::Inner::Inner(QWidget *parent)
: RpWidget(parent) {
	setupContent();
}

void SessionsContent::Inner::setupContent() {
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

void SessionsContent::Inner::showData(const Full &data) {
	_current->showData({ &data.current, &data.current + 1 });
	_list->showData(data.list);
	_incomplete->showData(data.incomplete);
}

rpl::producer<> SessionsContent::Inner::terminateAll() const {
	return _terminateAll->clicks() | rpl::to_empty;
}

rpl::producer<uint64> SessionsContent::Inner::terminateOne() const {
	return rpl::merge(
		_incomplete->terminate(),
		_list->terminate());
}

void SessionsContent::Inner::terminatingOne(uint64 hash, bool terminating) {
	_incomplete->terminating(hash, terminating);
	_list->terminating(hash, terminating);
}

SessionsContent::List::List(QWidget *parent) : RpWidget(parent) {
	setAttribute(Qt::WA_OpaquePaintEvent);
}

void SessionsContent::List::resizeEvent(QResizeEvent *e) {
	RpWidget::resizeEvent(e);

	computeRowWidth();
}

void SessionsContent::List::showData(gsl::span<const Entry> items) {
	computeRowWidth();

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
		const auto number = _terminateButtons.size() - 1;
		widthValue(
		) | rpl::start_with_next([=] {
			button->moveToRight(
				st::sessionTerminateSkip,
				(number * st::sessionHeight + st::sessionTerminateTop));
		}, lifetime());
	}
	resizeToWidth(width());
	_itemsCount.fire(_items.size());
}

rpl::producer<int> SessionsContent::List::itemsCount() const {
	return _itemsCount.events_starting_with(_items.size());
}

rpl::producer<uint64> SessionsContent::List::terminate() const {
	return _terminate.events();
}

void SessionsContent::List::terminating(uint64 hash, bool terminating) {
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

int SessionsContent::List::resizeGetHeight(int newWidth) {
	return _items.size() * st::sessionHeight;
}

void SessionsContent::List::computeRowWidth() {
	const auto available = width()
		- st::sessionPadding.left()
		- st::sessionTerminateSkip;
	_rowWidth = {
		.available = available,
		.info = available - st::sessionTerminate.width,
	};
}

void SessionsContent::List::paintEvent(QPaintEvent *e) {
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

	const auto available = _rowWidth.available;
	const auto x = st::sessionPadding.left();
	const auto y = st::sessionPadding.top();
	const auto w = width();
	const auto xact = st::sessionTerminateSkip
		+ st::sessionTerminate.iconPosition.x();
	p.translate(0, from * st::sessionHeight);
	for (auto i = from; i != till; ++i) {
		const auto &entry = _items[i];

		const auto activeW = entry.active.maxWidth();
		const auto nameW = available
			- activeW
			- st::sessionNamePadding.right();
		const auto nameH = entry.name.style()->font->height;
		const auto infoW = entry.hash ? _rowWidth.info : available;
		const auto infoH = entry.info.style()->font->height;

		p.setPen(entry.hash ? st::sessionWhenFg : st::contactsStatusFgOnline);
		entry.active.drawRight(p, xact, y, activeW, w);

		p.setPen(st::sessionNameFg);
		entry.name.drawLeftElided(p, x, y, nameW, w);

		p.setPen(st::boxTextFg);
		entry.info.drawLeftElided(p, x, y + nameH, infoW, w);

		p.setPen(st::sessionInfoFg);
		entry.ip.drawLeftElided(p, x, y + nameH + infoH, available, w);

		p.translate(0, st::sessionHeight);
	}
}

SessionsBox::SessionsBox(QWidget*, not_null<Main::Session*> session)
: _session(session) {
}

void SessionsBox::prepare() {
	setTitle(tr::lng_sessions_other_header());

	addButton(tr::lng_close(), [=] { closeBox(); });

	const auto w = st::boxWideWidth;

	const auto content = setInnerWidget(
		object_ptr<SessionsContent>(this, _session),
		st::sessionsScroll);
	content->resize(w, st::noContactsHeight);
	content->setupContent();

	setDimensions(w, st::sessionsHeight);
}

namespace Settings {

Sessions::Sessions(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent) {
	setupContent(controller);
}

void Sessions::setupContent(not_null<Window::SessionController*> controller) {
	const auto container = Ui::CreateChild<Ui::VerticalLayout>(this);
	const auto content = container->add(
		object_ptr<SessionsContent>(container, &controller->session()));
	content->setupContent();

	Ui::ResizeFitChild(this, container);
}

} // namespace Settings
