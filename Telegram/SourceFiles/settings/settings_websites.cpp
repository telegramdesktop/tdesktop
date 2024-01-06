/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_websites.h"

#include "api/api_websites.h"
#include "apiwrap.h"
#include "boxes/peer_list_box.h"
#include "boxes/sessions_box.h"
#include "data/data_user.h"
#include "ui/boxes/confirm_box.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/controls/userpic_button.h"
#include "ui/widgets/checkbox.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_menu_icons.h"

namespace {

constexpr auto kShortPollTimeout = 60 * crl::time(1000);

using EntryData = Api::Websites::Entry;

class Row;

class RowDelegate {
public:
	virtual void rowUpdateRow(not_null<Row*> row) = 0;
};

class Row final : public PeerListRow {
public:
	Row(not_null<RowDelegate*> delegate, const EntryData &data);

	void update(const EntryData &data);
	void updateName(const QString &name);

	[[nodiscard]] EntryData data() const;

	QString generateName() override;
	QString generateShortName() override;
	PaintRoundImageCallback generatePaintUserpicCallback(
		bool forceRound) override;

	QSize rightActionSize() const override {
		return elementGeometry(2, 0).size();
	}
	QMargins rightActionMargins() const override {
		const auto rect = elementGeometry(2, 0);
		return QMargins(0, rect.y(), -(rect.x() + rect.width()), 0);
	}

	int elementsCount() const override;
	QRect elementGeometry(int element, int outerWidth) const override;
	bool elementDisabled(int element) const override;
	bool elementOnlySelect(int element) const override;
	void elementAddRipple(
		int element,
		QPoint point,
		Fn<void()> updateCallback) override;
	void elementsStopLastRipple() override;
	void elementsPaint(
		Painter &p,
		int outerWidth,
		bool selected,
		int selectedElement) override;

private:
	const not_null<RowDelegate*> _delegate;
	QImage _emptyUserpic;
	Ui::PeerUserpicView _userpic;
	Ui::Text::String _location;
	EntryData _data;

};

[[nodiscard]] QString JoinNonEmpty(QStringList list) {
	list.erase(ranges::remove(list, QString()), list.end());
	return list.join(", ");
}

[[nodiscard]] QString LocationAndDate(const EntryData &entry) {
	return (entry.location.isEmpty() ? entry.ip : entry.location)
		+ (entry.hash
			? (QString::fromUtf8(" \xE2\x80\xA2 ") + entry.active)
			: QString());
}

void InfoBox(
		not_null<Ui::GenericBox*> box,
		const EntryData &data,
		Fn<void(uint64)> terminate) {
	box->setWidth(st::boxWideWidth);

	const auto shown = box->lifetime().make_state<rpl::event_stream<>>();
	box->setShowFinishedCallback([=] {
		shown->fire({});
	});

	const auto userpic = box->addRow(
		object_ptr<Ui::CenterWrap<Ui::UserpicButton>>(
			box,
			object_ptr<Ui::UserpicButton>(
				box,
				data.bot,
				st::websiteBigUserpic)),
		st::sessionBigCoverPadding)->entity();
	userpic->forceForumShape(true);
	userpic->setAttribute(Qt::WA_TransparentForMouseEvents);

	const auto nameWrap = box->addRow(
		object_ptr<Ui::FixedHeightWidget>(
			box,
			st::sessionBigName.maxHeight));
	const auto name = Ui::CreateChild<Ui::FlatLabel>(
		nameWrap,
		rpl::single(data.bot->name()),
		st::sessionBigName);
	nameWrap->widthValue(
	) | rpl::start_with_next([=](int width) {
		name->resizeToWidth(width);
		name->move((width - name->width()) / 2, 0);
	}, name->lifetime());

	const auto domainWrap = box->addRow(
		object_ptr<Ui::FixedHeightWidget>(
			box,
			st::sessionDateLabel.style.font->height),
		style::margins(0, 0, 0, st::sessionDateSkip));
	const auto domain = Ui::CreateChild<Ui::FlatLabel>(
		domainWrap,
		rpl::single(data.domain),
		st::sessionDateLabel);
	rpl::combine(
		domainWrap->widthValue(),
		domain->widthValue()
	) | rpl::start_with_next([=](int outer, int inner) {
		domain->move((outer - inner) / 2, 0);
	}, domain->lifetime());

	using namespace Settings;
	const auto container = box->verticalLayout();
	Ui::AddDivider(container);
	Ui::AddSkip(container, st::sessionSubtitleSkip);
	Ui::AddSubsectionTitle(container, tr::lng_sessions_info());

	AddSessionInfoRow(
		container,
		tr::lng_sessions_browser(),
		JoinNonEmpty({ data.browser, data.platform }),
		st::menuIconDevices);
	AddSessionInfoRow(
		container,
		tr::lng_sessions_ip(),
		data.ip,
		st::menuIconIpAddress);
	AddSessionInfoRow(
		container,
		tr::lng_sessions_location(),
		data.location,
		st::menuIconAddress);

	Ui::AddSkip(container, st::sessionValueSkip);
	if (!data.location.isEmpty()) {
		Ui::AddDividerText(container, tr::lng_sessions_location_about());
	}

	box->addButton(tr::lng_about_done(), [=] { box->closeBox(); });
	if (const auto hash = data.hash) {
		box->addLeftButton(tr::lng_settings_disconnect(), [=] {
			const auto weak = Ui::MakeWeak(box.get());
			terminate(hash);
			if (weak) {
				box->closeBox();
			}
		}, st::attentionBoxButton);
	}
}

Row::Row(not_null<RowDelegate*> delegate, const EntryData &data)
: PeerListRow(data.hash)
, _delegate(delegate)
, _location(st::defaultTextStyle, LocationAndDate(data))
, _data(data) {
	setCustomStatus(_data.ip);
}

void Row::update(const EntryData &data) {
	_data = data;
	setCustomStatus(
		JoinNonEmpty({ _data.domain, _data.browser, _data.platform }));
	refreshName(st::websiteListItem);
	_location.setText(st::defaultTextStyle, LocationAndDate(_data));
	_delegate->rowUpdateRow(this);
}

EntryData Row::data() const {
	return _data;
}

QString Row::generateName() {
	return _data.bot->name();
}

QString Row::generateShortName() {
	return _data.bot->shortName();
}

PaintRoundImageCallback Row::generatePaintUserpicCallback(bool forceRound) {
	const auto peer = _data.bot;
	auto userpic = _userpic = peer->createUserpicView();
	return [=](Painter &p, int x, int y, int outerWidth, int size) mutable {
		const auto ratio = style::DevicePixelRatio();
		if (const auto cloud = peer->userpicCloudImage(userpic)) {
			Ui::ValidateUserpicCache(
				userpic,
				cloud,
				nullptr,
				size * ratio,
				true);
			p.drawImage(QRect(x, y, size, size), userpic.cached);
		} else {
			if (_emptyUserpic.isNull()) {
				_emptyUserpic = peer->generateUserpicImage(
					_userpic,
					size * ratio,
					size * ratio * Ui::ForumUserpicRadiusMultiplier());
			}
			p.drawImage(QRect(x, y, size, size), _emptyUserpic);
		}
	};
}

int Row::elementsCount() const {
	return 2;
}

QRect Row::elementGeometry(int element, int outerWidth) const {
	switch (element) {
	case 1: {
		return QRect(
			st::websiteListItem.namePosition.x(),
			st::websiteLocationTop,
			outerWidth,
			st::normalFont->height);
	} break;
	case 2: {
		const auto size = QSize(
			st::sessionTerminate.width,
			st::sessionTerminate.height);
		const auto right = st::sessionTerminateSkip;
		const auto top = st::sessionTerminateTop;
		const auto left = outerWidth - right - size.width();
		return QRect(QPoint(left, top), size);
	} break;
	}
	return QRect();
}

bool Row::elementDisabled(int element) const {
	return !id() || (element == 1);
}

bool Row::elementOnlySelect(int element) const {
	return false;
}

void Row::elementAddRipple(
		int element,
		QPoint point,
		Fn<void()> updateCallback) {
}

void Row::elementsStopLastRipple() {
}

void Row::elementsPaint(
		Painter &p,
		int outerWidth,
		bool selected,
		int selectedElement) {
	const auto geometry = elementGeometry(2, outerWidth);
	const auto position = geometry.topLeft()
		+ st::sessionTerminate.iconPosition;
	const auto &icon = (selectedElement == 2)
		? st::sessionTerminate.iconOver
		: st::sessionTerminate.icon;
	icon.paint(p, position.x(), position.y(), outerWidth);

	p.setFont(st::normalFont);
	p.setPen(st::sessionInfoFg);
	const auto locationLeft = st::websiteListItem.namePosition.x();
	const auto available = outerWidth - locationLeft;
	_location.drawLeftElided(
		p,
		locationLeft,
		st::websiteLocationTop,
		available,
		outerWidth);
}

class Content : public Ui::RpWidget {
public:
	Content(
		QWidget*,
		not_null<Window::SessionController*> controller);

	void setupContent();

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private:
	class Inner;
	class ListController;

	void shortPoll();
	void parse(const Api::Websites::List &list);

	void terminate(
		Fn<void(bool block)> sendRequest,
		rpl::producer<QString> title,
		rpl::producer<QString> text,
		QString blockText = QString());
	void terminateOne(uint64 hash);
	void terminateAll();

	const not_null<Window::SessionController*> _controller;
	const not_null<Api::Websites*> _websites;

	rpl::variable<bool> _loading = false;
	Api::Websites::List _data;

	object_ptr<Inner> _inner;
	QPointer<Ui::BoxContent> _terminateBox;

	base::Timer _shortPollTimer;

};

class Content::ListController final
	: public PeerListController
	, public RowDelegate
	, public base::has_weak_ptr {
public:
	explicit ListController(not_null<Main::Session*> session);

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void rowElementClicked(not_null<PeerListRow*> row, int element) override;

	void rowUpdateRow(not_null<Row*> row) override;

	void showData(gsl::span<const EntryData> items);
	rpl::producer<int> itemsCount() const;
	rpl::producer<uint64> terminateRequests() const;
	[[nodiscard]] rpl::producer<EntryData> showRequests() const;

	[[nodiscard]] static std::unique_ptr<ListController> Add(
		not_null<Ui::VerticalLayout*> container,
		not_null<Main::Session*> session,
		style::margins margins = {});

private:
	const not_null<Main::Session*> _session;

	rpl::event_stream<uint64> _terminateRequests;
	rpl::event_stream<int> _itemsCount;
	rpl::event_stream<EntryData> _showRequests;

};

class Content::Inner : public Ui::RpWidget {
public:
	Inner(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	void showData(const Api::Websites::List &data);
	[[nodiscard]] rpl::producer<EntryData> showRequests() const;
	[[nodiscard]] rpl::producer<uint64> terminateOne() const;
	[[nodiscard]] rpl::producer<> terminateAll() const;

private:
	void setupContent();

	const not_null<Window::SessionController*> _controller;
	QPointer<Ui::SettingsButton> _terminateAll;
	std::unique_ptr<ListController> _list;

};

Content::Content(
	QWidget*,
	not_null<Window::SessionController*> controller)
: _controller(controller)
, _websites(&controller->session().api().websites())
, _inner(this, controller)
, _shortPollTimer([=] { shortPoll(); }) {
}

void Content::setupContent() {
	_inner->heightValue(
	) | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](int height) {
		resize(width(), height);
	}, _inner->lifetime());

	_inner->showRequests(
	) | rpl::start_with_next([=](const EntryData &data) {
		_controller->show(Box(
			InfoBox,
			data,
			[=](uint64 hash) { terminateOne(hash); }));
	}, lifetime());

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

	_websites->listValue(
	) | rpl::start_with_next([=](const Api::Websites::List &list) {
		parse(list);
	}, lifetime());

	_loading = true;
	shortPoll();
}

void Content::parse(const Api::Websites::List &list) {
	_loading = false;

	_data = list;

	ranges::sort(_data, std::greater<>(), &EntryData::activeTime);

	_inner->showData(_data);

	_shortPollTimer.callOnce(kShortPollTimeout);
}

void Content::resizeEvent(QResizeEvent *e) {
	RpWidget::resizeEvent(e);

	_inner->resize(width(), _inner->height());
}

void Content::paintEvent(QPaintEvent *e) {
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

void Content::shortPoll() {
	const auto left = kShortPollTimeout
		- (crl::now() - _websites->lastReceivedTime());
	if (left > 0) {
		parse(_websites->list());
		_shortPollTimer.cancel();
		_shortPollTimer.callOnce(left);
	} else {
		_websites->reload();
	}
	update();
}

void Content::terminate(
		Fn<void(bool block)> sendRequest,
		rpl::producer<QString> title,
		rpl::producer<QString> text,
		QString blockText) {
	if (const auto strong = _terminateBox.data()) {
		strong->deleteLater();
	}
	auto box = Box([=](not_null<Ui::GenericBox*> box) {
		auto &lifetime = box->lifetime();
		const auto block = lifetime.make_state<Ui::Checkbox*>(nullptr);
		const auto callback = crl::guard(this, [=] {
			const auto blocked = (*block) && (*block)->checked();
			if (_terminateBox) {
				_terminateBox->closeBox();
				_terminateBox = nullptr;
			}
			sendRequest(blocked);
		});
		Ui::ConfirmBox(box, {
			.text = rpl::duplicate(text),
			.confirmed = callback,
			.confirmText = tr::lng_settings_disconnect(),
			.confirmStyle = &st::attentionBoxButton,
			.title = rpl::duplicate(title),
		});
		if (!blockText.isEmpty()) {
			*block = box->addRow(object_ptr<Ui::Checkbox>(box, blockText));
		}
	});
	_terminateBox = Ui::MakeWeak(box.data());
	_controller->show(std::move(box));
}

void Content::terminateOne(uint64 hash) {
	const auto weak = Ui::MakeWeak(this);
	const auto i = ranges::find(_data, hash, &EntryData::hash);
	if (i == end(_data)) {
		return;
	}

	const auto bot = i->bot;
	auto callback = [=](bool block) {
		auto done = crl::guard(weak, [=](const MTPBool &result) {
			_data.erase(
				ranges::remove(_data, hash, &EntryData::hash),
				end(_data));
			_inner->showData(_data);
		});
		auto fail = crl::guard(weak, [=](const MTP::Error &error) {
		});
		_websites->requestTerminate(
			std::move(done),
			std::move(fail),
			hash,
			block ? bot.get() : nullptr);
	};
	terminate(
		std::move(callback),
		tr::lng_settings_disconnect_title(),
		tr::lng_settings_disconnect_sure(lt_domain, rpl::single(i->domain)),
		tr::lng_settings_disconnect_block(tr::now, lt_name, bot->name()));
}

void Content::terminateAll() {
	const auto weak = Ui::MakeWeak(this);
	auto callback = [=](bool block) {
		const auto reset = crl::guard(weak, [=] {
			_websites->cancelCurrentRequest();
			_websites->reload();
		});
		_websites->requestTerminate(
			[=](const MTPBool &result) { reset(); },
			[=](const MTP::Error &result) { reset(); });
		_loading = true;
	};
	terminate(
		std::move(callback),
		tr::lng_settings_disconnect_all_title(),
		tr::lng_settings_disconnect_all_sure());
}

Content::Inner::Inner(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _controller(controller) {
	resize(width(), st::noContactsHeight);
	setupContent();
}

void Content::Inner::setupContent() {
	using namespace Settings;
	using namespace rpl::mappers;

	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	const auto session = &_controller->session();
	const auto terminateWrap = content->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			content,
			object_ptr<Ui::VerticalLayout>(content)))->setDuration(0);
	const auto terminateInner = terminateWrap->entity();
	_terminateAll = terminateInner->add(
		CreateButtonWithIcon(
			terminateInner,
			tr::lng_settings_disconnect_all(),
			st::infoBlockButton,
			{ .icon = &st::infoIconBlock }));
	Ui::AddSkip(terminateInner);
	Ui::AddDividerText(
		terminateInner,
		tr::lng_settings_logged_in_description());

	const auto listWrap = content->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			content,
			object_ptr<Ui::VerticalLayout>(content)))->setDuration(0);
	const auto listInner = listWrap->entity();
	Ui::AddSkip(listInner, st::sessionSubtitleSkip);
	Ui::AddSubsectionTitle(listInner, tr::lng_settings_logged_in_title());
	_list = ListController::Add(listInner, session);
	Ui::AddSkip(listInner);

	const auto skip = st::noContactsHeight / 2;
	const auto placeholder = content->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			content,
			object_ptr<Ui::FlatLabel>(
				content,
				tr::lng_settings_logged_in_description(),
				st::boxDividerLabel),
			st::defaultBoxDividerLabelPadding + QMargins(0, skip, 0, skip))
	)->setDuration(0);

	terminateWrap->toggleOn(_list->itemsCount() | rpl::map(_1 > 0));
	listWrap->toggleOn(_list->itemsCount() | rpl::map(_1 > 0));
	placeholder->toggleOn(_list->itemsCount() | rpl::map(_1 == 0));

	Ui::ResizeFitChild(this, content);
}

void Content::Inner::showData(const Api::Websites::List &data) {
	_list->showData(data);
}

rpl::producer<> Content::Inner::terminateAll() const {
	return _terminateAll->clicks() | rpl::to_empty;
}

rpl::producer<uint64> Content::Inner::terminateOne() const {
	return _list->terminateRequests();
}

rpl::producer<EntryData> Content::Inner::showRequests() const {
	return _list->showRequests();
}

Content::ListController::ListController(
	not_null<Main::Session*> session)
: _session(session) {
}

Main::Session &Content::ListController::session() const {
	return *_session;
}

void Content::ListController::prepare() {
}

void Content::ListController::rowClicked(
		not_null<PeerListRow*> row) {
	_showRequests.fire_copy(static_cast<Row*>(row.get())->data());
}

void Content::ListController::rowElementClicked(
		not_null<PeerListRow*> row,
		int element) {
	if (element == 2) {
		if (const auto hash = static_cast<Row*>(row.get())->data().hash) {
			_terminateRequests.fire_copy(hash);
		}
	}
}

void Content::ListController::rowUpdateRow(not_null<Row*> row) {
	delegate()->peerListUpdateRow(row);
}

void Content::ListController::showData(
		gsl::span<const EntryData> items) {
	auto index = 0;
	auto positions = base::flat_map<uint64, int>();
	positions.reserve(items.size());
	for (const auto &entry : items) {
		const auto id = entry.hash;
		positions.emplace(id, index++);
		if (const auto row = delegate()->peerListFindRow(id)) {
			static_cast<Row*>(row)->update(entry);
		} else {
			delegate()->peerListAppendRow(
				std::make_unique<Row>(this, entry));
		}
	}
	for (auto i = 0; i != delegate()->peerListFullRowsCount();) {
		const auto row = delegate()->peerListRowAt(i);
		if (positions.contains(row->id())) {
			++i;
			continue;
		}
		delegate()->peerListRemoveRow(row);
	}
	delegate()->peerListSortRows([&](
			const PeerListRow &a,
			const PeerListRow &b) {
		return positions[a.id()] < positions[b.id()];
	});
	delegate()->peerListRefreshRows();
	_itemsCount.fire(delegate()->peerListFullRowsCount());
}

rpl::producer<int> Content::ListController::itemsCount() const {
	return _itemsCount.events_starting_with(
		delegate()->peerListFullRowsCount());
}

rpl::producer<uint64> Content::ListController::terminateRequests() const {
	return _terminateRequests.events();
}

rpl::producer<EntryData> Content::ListController::showRequests() const {
	return _showRequests.events();
}

auto Content::ListController::Add(
	not_null<Ui::VerticalLayout*> container,
	not_null<Main::Session*> session,
	style::margins margins)
-> std::unique_ptr<ListController> {
	auto &lifetime = container->lifetime();
	const auto delegate = lifetime.make_state<
		PeerListContentDelegateSimple
	>();
	auto controller = std::make_unique<ListController>(session);
	controller->setStyleOverrides(&st::websiteList);
	const auto content = container->add(
		object_ptr<PeerListContent>(
			container,
			controller.get()),
		margins);
	delegate->setContent(content);
	controller->setDelegate(delegate);
	return controller;
}

} // namespace

namespace Settings {

Websites::Websites(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent) {
	setupContent(controller);
}

rpl::producer<QString> Websites::title() {
	return tr::lng_settings_connected_title();
}

void Websites::setupContent(not_null<Window::SessionController*> controller) {
	const auto container = Ui::CreateChild<Ui::VerticalLayout>(this);
	Ui::AddSkip(container);
	const auto content = container->add(
		object_ptr<Content>(container, controller));
	content->setupContent();

	Ui::ResizeFitChild(this, container);
}

} // namespace Settings
