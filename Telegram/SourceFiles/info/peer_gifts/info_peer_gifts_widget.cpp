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
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/box_content_divider.h"
#include "ui/widgets/labels.h"
#include "ui/ui_utility.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mtproto/sender.h"
#include "window/window_session_controller.h"
#include "settings/settings_credits_graphics.h"
#include "styles/style_info.h"
#include "styles/style_credits.h" // giftBoxPadding

namespace Info::PeerGifts {
namespace {

constexpr auto kPreloadPages = 2;
constexpr auto kPerPage = 50;

[[nodiscard]] GiftDescriptor DescriptorForGift(
		not_null<UserData*> to,
		const Api::UserStarGift &gift) {
	return GiftTypeStars{
		.id = gift.gift.id,
		.stars = gift.gift.stars,
		.convertStars = gift.gift.convertStars,
		.document = gift.gift.document,
		.from = ((gift.anonymous || !gift.fromId)
			? nullptr
			: to->owner().peer(gift.fromId).get()),
		.limitedCount = gift.gift.limitedCount,
		.userpic = true,
		.hidden = gift.hidden,
		.mine = to->isSelf(),
	};
}

} // namespace

class InnerWidget final : public Ui::BoxContentDivider {
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
		int entry = 0;
	};

	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;
	void paintEvent(QPaintEvent *e) override;

	void subscribeToUpdates();
	void loadMore();
	void refreshButtons();
	void validateButtons();
	void showGift(int index);

	int resizeGetHeight(int width) override;

	const not_null<Window::SessionController*> _window;
	Delegate _delegate;
	not_null<Controller*> _controller;
	std::unique_ptr<Ui::FlatLabel> _about;
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
: BoxContentDivider(parent)
, _window(controller->parentController())
, _delegate(_window)
, _controller(controller)
, _about(std::make_unique<Ui::FlatLabel>(
	this,
	(user->isSelf()
		? tr::lng_peer_gifts_about_mine(Ui::Text::RichLangValue)
		: tr::lng_peer_gifts_about(
			lt_user,
			rpl::single(Ui::Text::Bold(user->shortName())),
			Ui::Text::RichLangValue)),
	st::giftListAbout))
, _user(user)
, _totalCount(_user->peerGiftsCount())
, _api(&_user->session().mtp()) {
	_singleMin = _delegate.buttonSize();

	if (user->isSelf()) {
		subscribeToUpdates();
	}
}

void InnerWidget::subscribeToUpdates() {
	_user->owner().giftUpdates(
	) | rpl::start_with_next([=](const Data::GiftUpdate &update) {
		const auto itemId = [](const Entry &entry) {
			return FullMsgId(entry.gift.fromId, entry.gift.messageId);
		};
		const auto i = ranges::find(_entries, update.itemId, itemId);
		if (i == end(_entries)) {
			return;
		}
		const auto index = int(i - begin(_entries));
		using Action = Data::GiftUpdate::Action;
		if (update.action == Action::Convert
			|| update.action == Action::Delete) {
			_entries.erase(i);
			if (_totalCount > 0) {
				--_totalCount;
			}
			for (auto &view : _views) {
				if (view.entry >= index) {
					--view.entry;
				}
			}
		} else if (update.action == Action::Save
			|| update.action == Action::Unsave) {
			i->gift.hidden = (update.action == Action::Unsave);
			v::match(i->descriptor, [](GiftTypePremium &) {
			}, [&](GiftTypeStars &data) {
				data.hidden = i->gift.hidden;
			});
			for (auto &view : _views) {
				if (view.entry == index) {
					view.entry = -1;
				}
			}
		} else {
			return;
		}
		refreshButtons();
	}, lifetime());
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

	const auto aboutSize = _about->size().grownBy(st::giftListAboutMargin);
	const auto skips = QMargins(0, 0, 0, aboutSize.height());
	p.fillRect(rect().marginsRemoved(skips), st::boxDividerBg->c);
	paintTop(p);
	paintBottom(p, skips.bottom());
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

		_entries.reserve(_entries.size() + data.vgifts().v.size());
		for (const auto &gift : data.vgifts().v) {
			if (auto parsed = Api::FromTL(_user, gift)) {
				auto descriptor = DescriptorForGift(_user, *parsed);
				_entries.push_back({
					.gift = std::move(*parsed),
					.descriptor = std::move(descriptor),
				});
			}
		}
		refreshButtons();
	}).fail([=] {
		_loadMoreRequestId = 0;
		_allLoaded = true;
	}).send();
}

void InnerWidget::refreshButtons() {
	_viewsForWidth = 0;
	_viewsFromRow = 0;
	_viewsTillRow = 0;
	resizeToWidth(width());
	validateButtons();
}

void InnerWidget::validateButtons() {
	if (!_perRow) {
		return;
	}
	const auto padding = st::giftBoxPadding;
	const auto vskip = padding.bottom();
	const auto row = _single.height() + st::giftBoxGiftSkip.y();
	const auto fromRow = std::max(_visibleFrom - vskip, 0) / row;
	const auto tillRow = (_visibleTill - vskip + row - 1) / row;
	Assert(tillRow >= fromRow);
	if (_viewsFromRow == fromRow
		&& _viewsTillRow == tillRow
		&& _viewsForWidth == width()) {
		return;
	}
	_viewsFromRow = fromRow;
	_viewsTillRow = tillRow;
	_viewsForWidth = width();

	const auto available = _viewsForWidth - padding.left() - padding.right();
	const auto skipw = st::giftBoxGiftSkip.x();
	const auto fullw = _perRow * (_single.width() + skipw) - skipw;
	const auto left = padding.left() + (available - fullw) / 2;
	const auto oneh = _single.height() + st::giftBoxGiftSkip.y();
	auto x = left;
	auto y = vskip + fromRow * oneh;
	auto views = std::vector<View>();
	views.reserve((tillRow - fromRow) * _perRow);
	const auto add = [&](int index) {
		const auto already = ranges::find(_views, index, &View::entry);
		if (already != end(_views)) {
			views.push_back(base::take(*already));
			return;
		}
		const auto &descriptor = _entries[index].descriptor;
		const auto callback = [=] {
			showGift(index);
		};
		const auto unused = ranges::find_if(_views, [&](const View &v) {
			return v.button
				&& ((v.entry < fromRow * _perRow)
					|| (v.entry >= tillRow * _perRow));
		});
		if (unused != end(_views)) {
			views.push_back(base::take(*unused));
			views.back().entry = index;
		} else {
			auto button = std::make_unique<GiftButton>(this, &_delegate);
			button->show();
			views.push_back({
				.button = std::move(button),
				.entry = index,
			});
		}
		views.back().button->setDescriptor(descriptor);
		views.back().button->setClickedCallback(callback);
 	};
	for (auto j = fromRow; j != tillRow; ++j) {
		for (auto i = 0; i != _perRow; ++i) {
			const auto index = j * _perRow + i;
			if (index >= _entries.size()) {
				break;
			}
			add(index);
			views.back().button->setGeometry(
				QRect(QPoint(x, y), _single),
				_delegate.buttonExtend());
			x += _single.width() + skipw;
		}
		x = left;
		y += oneh;
	}
	std::swap(_views, views);
}

void InnerWidget::showGift(int index) {
	_window->show(Box(
		::Settings::UserStarGiftBox,
		_window,
		_entries[index].gift));
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

	auto result = padding.bottom() * 2 + rows * (singleh + skiph) - skiph;

	const auto margin = st::giftListAboutMargin;
	_about->resizeToWidth(width - margin.left() - margin.right());
	_about->moveToLeft(margin.left(), result + margin.top());
	result += margin.top() + _about->height() + margin.bottom();

	return result;
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
