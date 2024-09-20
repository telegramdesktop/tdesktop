/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/peer_gifts/info_peer_gifts_widget.h"

#include "data/data_session.h"
#include "data/data_user.h"
#include "info/peer_gifts/info_peer_gifts_common.h"
#include "info/info_controller.h"
#include "ui/ui_utility.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mtproto/sender.h"
#include "window/window_session_controller.h"
#include "styles/style_info.h"
#include "styles/style_credits.h" // giftBoxPadding

namespace Info::PeerGifts {
namespace {

constexpr auto kPreloadPages = 2;
constexpr auto kPerPage = 50;

[[nodiscard]] GiftDescriptor DescriptorForGift(
		not_null<Data::Session*> owner,
		const Api::UserStarGift &gift) {
	return GiftTypeStars{
		.id = gift.gift.id,
		.stars = gift.gift.stars,
		.convertStars = gift.gift.convertStars,
		.document = gift.gift.document,
		.from = ((gift.hidden || !gift.fromId)
			? nullptr
			: owner->peer(gift.fromId).get()),
		.limited = (gift.gift.limitedCount > 0),
		.userpic = true,
	};
}

} // namespace

class InnerWidget final : public Ui::RpWidget {
public:
	InnerWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		not_null<UserData*> user);

	[[nodiscard]] not_null<UserData*> user() const {
		return _user;
	}

	void saveState(not_null<Memento*> memento);
	void restoreState(not_null<Memento*> memento);

private:
	struct Entry {
		Api::UserStarGift gift;
		GiftDescriptor descriptor;
	};
	struct View {
		std::unique_ptr<GiftButton> button;
		Api::UserStarGift gift;
	};

	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;
	void paintEvent(QPaintEvent *e) override;

	void loadMore();
	void validateButtons();

	int resizeGetHeight(int width) override;

	Delegate _delegate;
	const std::shared_ptr<Main::SessionShow> _show;
	not_null<Controller*> _controller;
	const not_null<UserData*> _user;
	std::vector<Entry> _entries;
	int _totalCount = 0;

	MTP::Sender _api;
	mtpRequestId _loadMoreRequestId = 0;
	QString _offset;
	bool _allLoaded = false;

	std::vector<View> _views;
	int _viewsForWidth = 0;
	int _viewsFromRow = 0;
	int _viewsTillRow = 0;

	QSize _singleMin;
	QSize _single;
	int _perRow = 0;
	int _visibleFrom = 0;
	int _visibleTill = 0;

};

InnerWidget::InnerWidget(
	QWidget *parent,
	not_null<Controller*> controller,
	not_null<UserData*> user)
: RpWidget(parent)
, _delegate(controller->parentController())
, _show(controller->uiShow())
, _controller(controller)
, _user(user)
, _totalCount(_user->peerGiftsCount())
, _api(&_user->session().mtp()) {
	_singleMin = _delegate.buttonSize();
}

void InnerWidget::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	const auto page = (visibleBottom - visibleTop);
	if (visibleBottom + page * kPreloadPages >= height()) {
		loadMore();
	}
	_visibleFrom = visibleTop;
	_visibleTill = visibleBottom;
	validateButtons();
}

void InnerWidget::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	p.fillRect(e->rect(), st::boxDividerBg);
}

void InnerWidget::loadMore() {
	if (_allLoaded || _loadMoreRequestId) {
		return;
	}
	_loadMoreRequestId = _api.request(MTPpayments_GetUserStarGifts(
		_user->inputUser,
		MTP_string(_offset),
		MTP_int(kPerPage)
	)).done([=](const MTPpayments_UserStarGifts &result) {
		_loadMoreRequestId = 0;
		const auto &data = result.data();
		if (const auto next = data.vnext_offset()) {
			_offset = qs(*next);
		} else {
			_allLoaded = true;
		}
		_totalCount = data.vcount().v;

		const auto owner = &_user->owner();
		owner->processUsers(data.vusers());

		const auto session = &_show->session();
		_entries.reserve(_entries.size() + data.vgifts().v.size());
		for (const auto &gift : data.vgifts().v) {
			if (auto parsed = Api::FromTL(session, gift)) {
				auto descriptor = DescriptorForGift(owner, *parsed);
				_entries.push_back({
					.gift = std::move(*parsed),
					.descriptor = std::move(descriptor),
				});
			}
		}
		_viewsForWidth = 0;
		_viewsFromRow = 0;
		_viewsTillRow = 0;
		resizeToWidth(width());
		validateButtons();
	}).fail([=] {
		_loadMoreRequestId = 0;
		_allLoaded = true;
	}).send();
}

void InnerWidget::validateButtons() {
	if (!_perRow) {
		return;
	}
	const auto row = _single.height() + st::giftBoxGiftSkip.y();
	const auto fromRow = _visibleFrom / row;
	const auto tillRow = (_visibleTill + row - 1) / row;
	Assert(tillRow >= fromRow);
	if (_viewsFromRow == fromRow
		&& _viewsTillRow == tillRow
		&& _viewsForWidth == width()) {
		return;
	}
	_viewsFromRow = fromRow;
	_viewsTillRow = tillRow;
	_viewsForWidth = width();

	const auto padding = st::giftBoxPadding;
	const auto available = _viewsForWidth - padding.left() - padding.right();
	const auto skipw = st::giftBoxGiftSkip.x();
	const auto fullw = _perRow * (_single.width() + skipw) - skipw;
	const auto left = padding.left() + (available - fullw) / 2;
	auto x = left;
	auto y = padding.bottom();
	auto entry = 0;
	for (auto j = fromRow; j != tillRow; ++j) {
		for (auto i = 0; i != _perRow; ++i) {
			const auto index = j * _perRow + i;
			if (index >= _entries.size()) {
				break;
			}
			const auto &descriptor = _entries[index].descriptor;
			if (entry < _views.size()) {
				_views[entry].button->setDescriptor(descriptor);
			} else {
				auto button = std::make_unique<GiftButton>(this, &_delegate);
				button->setDescriptor(descriptor);
				_views.push_back({
					.button = std::move(button),
					.gift = _entries[index].gift,
				});
			}
			_views[entry].button->show();
			_views[entry].button->setGeometry(
				QRect(QPoint(x, y), _single),
				_delegate.buttonExtend());
			++entry;
			x += _single.width() + skipw;
		}
		x = left;
		y += _single.height() + st::giftBoxGiftSkip.y();
	}
	for (auto k = entry; k != int(_views.size()); ++k) {
		_views[k].button->hide();
	}
}

int InnerWidget::resizeGetHeight(int width) {
	const auto count = int(_entries.size());
	const auto padding = st::giftBoxPadding;
	const auto available = width - padding.left() - padding.right();
	const auto skipw = st::giftBoxGiftSkip.x();
	_perRow = std::min(
		(available + skipw) / (_singleMin.width() + skipw),
		count);
	if (!_perRow) {
		return 0;
	}
	const auto singlew = std::min(
		((available + skipw) / _perRow) - skipw,
		2 * _singleMin.width());
	Assert(singlew >= _singleMin.width());
	const auto singleh = _singleMin.height();

	_single = QSize(singlew, singleh);
	const auto rows = (count + _perRow - 1) / _perRow;
	const auto skiph = st::giftBoxGiftSkip.y();

	return padding.bottom() * 2 + rows * (singleh + skiph) - skiph;
}

void InnerWidget::saveState(not_null<Memento*> memento) {
	auto state = std::make_unique<ListState>();
	memento->setListState(std::move(state));
}

void InnerWidget::restoreState(not_null<Memento*> memento) {
	if (const auto state = memento->listState()) {

	}
}

Memento::Memento(not_null<UserData*> user)
: ContentMemento(user, nullptr, PeerId()) {
}

Section Memento::section() const {
	return Section(Section::Type::PeerGifts);
}

not_null<UserData*> Memento::user() const {
	return peer()->asUser();
}

object_ptr<ContentWidget> Memento::createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) {
	auto result = object_ptr<Widget>(parent, controller, user());
	result->setInternalState(geometry, this);
	return result;
}

void Memento::setListState(std::unique_ptr<ListState> state) {
	_listState = std::move(state);
}

std::unique_ptr<ListState> Memento::listState() {
	return std::move(_listState);
}

Memento::~Memento() = default;

Widget::Widget(
	QWidget *parent,
	not_null<Controller*> controller,
	not_null<UserData*> user)
: ContentWidget(parent, controller) {
	_inner = setInnerWidget(object_ptr<InnerWidget>(
		this,
		controller,
		user));
}

rpl::producer<QString> Widget::title() {
	return tr::lng_peer_gifts_title();
}

not_null<UserData*> Widget::user() const {
	return _inner->user();
}

bool Widget::showInternal(not_null<ContentMemento*> memento) {
	if (!controller()->validateMementoPeer(memento)) {
		return false;
	}
	if (auto similarMemento = dynamic_cast<Memento*>(memento.get())) {
		if (similarMemento->user() == user()) {
			restoreState(similarMemento);
			return true;
		}
	}
	return false;
}

void Widget::setInternalState(
		const QRect &geometry,
		not_null<Memento*> memento) {
	setGeometry(geometry);
	Ui::SendPendingMoveResizeEvents(this);
	restoreState(memento);
}

std::shared_ptr<ContentMemento> Widget::doCreateMemento() {
	auto result = std::make_shared<Memento>(user());
	saveState(result.get());
	return result;
}

void Widget::saveState(not_null<Memento*> memento) {
	memento->setScrollTop(scrollTopSave());
	_inner->saveState(memento);
}

void Widget::restoreState(not_null<Memento*> memento) {
	_inner->restoreState(memento);
	scrollTopRestore(memento->scrollTop());
}

} // namespace Info::PeerGifts
