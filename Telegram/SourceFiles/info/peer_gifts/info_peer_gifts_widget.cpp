/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/peer_gifts/info_peer_gifts_widget.h"

#include "api/api_premium.h"
#include "apiwrap.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "info/peer_gifts/info_peer_gifts_common.h"
#include "info/info_controller.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/box_content_divider.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/ui_utility.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mtproto/sender.h"
#include "window/window_session_controller.h"
#include "settings/settings_credits_graphics.h"
#include "styles/style_info.h"
#include "styles/style_layers.h" // boxRadius
#include "styles/style_media_player.h" // mediaPlayerMenuCheck
#include "styles/style_menu_icons.h"
#include "styles/style_credits.h" // giftBoxPadding

namespace Info::PeerGifts {
namespace {

constexpr auto kPreloadPages = 2;
constexpr auto kPerPage = 50;

[[nodiscard]] GiftDescriptor DescriptorForGift(
		not_null<PeerData*> to,
		const Data::SavedStarGift &gift) {
	return GiftTypeStars{
		.info = gift.info,
		.from = ((gift.anonymous || !gift.fromId)
			? nullptr
			: to->owner().peer(gift.fromId).get()),
		.userpic = !gift.info.unique,
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
		not_null<PeerData*> peer,
		rpl::producer<Filter> filter);

	[[nodiscard]] not_null<PeerData*> peer() const {
		return _peer;
	}
	[[nodiscard]] rpl::producer<bool> notifyEnabled() const {
		return _notifyEnabled.events();
	}
	[[nodiscard]] rpl::producer<> scrollToTop() const {
		return _scrollToTop.events();
	}

	void saveState(not_null<Memento*> memento);
	void restoreState(not_null<Memento*> memento);

private:
	struct Entry {
		Data::SavedStarGift gift;
		GiftDescriptor descriptor;
	};
	struct View {
		std::unique_ptr<GiftButton> button;
		Data::SavedStarGiftId manageId;
		uint64 giftId = 0;
		int index = 0;
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
	void refreshAbout();

	int resizeGetHeight(int width) override;

	const not_null<Window::SessionController*> _window;
	rpl::variable<Filter> _filter;
	Delegate _delegate;
	not_null<Controller*> _controller;
	std::unique_ptr<Ui::FlatLabel> _about;
	const not_null<PeerData*> _peer;
	std::vector<Entry> _entries;
	int _totalCount = 0;
	rpl::event_stream<> _scrollToTop;

	MTP::Sender _api;
	mtpRequestId _loadMoreRequestId = 0;
	QString _offset;
	bool _allLoaded = false;
	bool _reloading = false;

	rpl::event_stream<bool> _notifyEnabled;
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
	not_null<PeerData*> peer,
	rpl::producer<Filter> filter)
: BoxContentDivider(parent)
, _window(controller->parentController())
, _filter(std::move(filter))
, _delegate(_window, GiftButtonMode::Minimal)
, _controller(controller)
, _peer(peer)
, _totalCount(_peer->peerGiftsCount())
, _api(&_peer->session().mtp()) {
	_singleMin = _delegate.buttonSize();

	if (peer->canManageGifts()) {
		subscribeToUpdates();
	}

	_filter.value() | rpl::start_with_next([=] {
		_reloading = true;
		_api.request(base::take(_loadMoreRequestId)).cancel();
		_allLoaded = false;
		refreshAbout();
		loadMore();
	}, lifetime());
}

void InnerWidget::subscribeToUpdates() {
	_peer->owner().giftUpdates(
	) | rpl::start_with_next([=](const Data::GiftUpdate &update) {
		const auto savedId = [](const Entry &entry) {
			return entry.gift.manageId;
		};
		const auto i = ranges::find(_entries, update.id, savedId);
		if (i == end(_entries)) {
			return;
		}
		const auto index = int(i - begin(_entries));
		using Action = Data::GiftUpdate::Action;
		if (update.action == Action::Convert
			|| update.action == Action::Transfer
			|| update.action == Action::Delete) {
			_entries.erase(i);
			if (_totalCount > 0) {
				--_totalCount;
			}
			for (auto &view : _views) {
				if (view.index >= index) {
					--view.index;
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
				if (view.index == index) {
					view.index = -1;
					view.manageId = {};
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

	const auto aboutSize = _about
		? _about->size().grownBy(st::giftListAboutMargin)
		: QSize();
	const auto skips = QMargins(0, 0, 0, aboutSize.height());
	p.fillRect(rect().marginsRemoved(skips), st::boxDividerBg->c);
	paintTop(p);
	if (const auto bottom = skips.bottom()) {
		paintBottom(p, bottom);
	}
}

void InnerWidget::loadMore() {
	if (_allLoaded || _loadMoreRequestId) {
		return;
	}
	using Flag = MTPpayments_GetSavedStarGifts::Flag;
	const auto filter = _filter.current();
	_loadMoreRequestId = _api.request(MTPpayments_GetSavedStarGifts(
		MTP_flags((filter.sortByValue ? Flag::f_sort_by_value : Flag())
			| (filter.skipLimited ? Flag::f_exclude_limited : Flag())
			| (filter.skipUnlimited ? Flag::f_exclude_unlimited : Flag())
			| (filter.skipUnique ? Flag::f_exclude_unique : Flag())
			| (filter.skipSaved ? Flag::f_exclude_saved : Flag())
			| (filter.skipUnsaved ? Flag::f_exclude_unsaved : Flag())),
		_peer->input,
		MTP_string(_reloading ? QString() : _offset),
		MTP_int(kPerPage)
	)).done([=](const MTPpayments_SavedStarGifts &result) {
		_loadMoreRequestId = 0;
		const auto &data = result.data();
		if (const auto enabled = data.vchat_notifications_enabled()) {
			_notifyEnabled.fire(mtpIsTrue(*enabled));
		}
		if (const auto next = data.vnext_offset()) {
			_offset = qs(*next);
		} else {
			_allLoaded = true;
		}
		_totalCount = data.vcount().v;

		const auto owner = &_peer->owner();
		owner->processUsers(data.vusers());
		owner->processChats(data.vchats());

		if (base::take(_reloading)) {
			_entries.clear();
		}
		_entries.reserve(_entries.size() + data.vgifts().v.size());
		for (const auto &gift : data.vgifts().v) {
			if (auto parsed = Api::FromTL(_peer, gift)) {
				auto descriptor = DescriptorForGift(_peer, *parsed);
				_entries.push_back({
					.gift = std::move(*parsed),
					.descriptor = std::move(descriptor),
				});
			}
		}
		refreshButtons();
		refreshAbout();
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
	const auto mode = GiftButton::Mode::Minimal;
	auto x = left;
	auto y = vskip + fromRow * oneh;
	auto views = std::vector<View>();
	views.reserve((tillRow - fromRow) * _perRow);
	const auto idUsed = [&](uint64 giftId, int column, int row) {
		for (auto j = row; j != tillRow; ++j) {
			for (auto i = column; i != _perRow; ++i) {
				const auto index = j * _perRow + i;
				if (index >= _entries.size()) {
					return false;
				} else if (_entries[index].gift.info.id == giftId) {
					return true;
				}
			}
			column = 0;
		}
		return false;
	};
	const auto add = [&](int column, int row) {
		const auto index = row * _perRow + column;
		if (index >= _entries.size()) {
			return false;
		}
		const auto giftId = _entries[index].gift.info.id;
		const auto manageId = _entries[index].gift.manageId;
		const auto &descriptor = _entries[index].descriptor;
		const auto already = ranges::find(_views, giftId, &View::giftId);
		if (already != end(_views)) {
			views.push_back(base::take(*already));
		} else {
			const auto unused = ranges::find_if(_views, [&](const View &v) {
				return v.button && !idUsed(v.giftId, column, row);
			});
			if (unused != end(_views)) {
				views.push_back(base::take(*unused));
			} else {
				auto button = std::make_unique<GiftButton>(this, &_delegate);
				button->show();
				views.push_back({ .button = std::move(button) });
			}
		}
		auto &view = views.back();
		const auto callback = [=] {
			showGift(index);
		};
		view.index = index;
		view.manageId = manageId;
		view.giftId = giftId;
		view.button->setDescriptor(descriptor, mode);
		view.button->setClickedCallback(callback);
		return true;
	};
	for (auto j = fromRow; j != tillRow; ++j) {
		for (auto i = 0; i != _perRow; ++i) {
			if (!add(i, j)) {
				break;
			}
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
	Expects(index >= 0 && index < _entries.size());

	_window->show(Box(
		::Settings::SavedStarGiftBox,
		_window,
		_peer,
		_entries[index].gift));
}

void InnerWidget::refreshAbout() {
	if (!_peer->isSelf() && _peer->canManageGifts() && !_entries.empty()) {
		if (_about) {
			_about = nullptr;
			resizeToWidth(width());
		}
	} else if (!_about) {
		_about = std::make_unique<Ui::FlatLabel>(
			this,
			(_peer->isSelf()
				? tr::lng_peer_gifts_about_mine(Ui::Text::RichLangValue)
				: tr::lng_peer_gifts_about(
					lt_user,
					rpl::single(Ui::Text::Bold(_peer->shortName())),
					Ui::Text::RichLangValue)),
			st::giftListAbout);
		_about->show();
		resizeToWidth(width());
	}
}

int InnerWidget::resizeGetHeight(int width) {
	const auto count = int(_entries.size());
	const auto padding = st::giftBoxPadding;
	const auto available = width - padding.left() - padding.right();
	const auto skipw = st::giftBoxGiftSkip.x();
	_perRow = std::min(
		(available + skipw) / (_singleMin.width() + skipw),
		std::max(count, 1));
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

	auto result = rows
		? (padding.bottom() * 2 + rows * (singleh + skiph) - skiph)
		: 0;

	if (const auto about = _about.get()) {
		const auto margin = st::giftListAboutMargin;
		about->resizeToWidth(width - margin.left() - margin.right());
		about->moveToLeft(margin.left(), result + margin.top());
		result += margin.top() + about->height() + margin.bottom();
	}

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

Memento::Memento(not_null<PeerData*> peer)
: ContentMemento(peer, nullptr, PeerId()) {
}

Section Memento::section() const {
	return Section(Section::Type::PeerGifts);
}

object_ptr<ContentWidget> Memento::createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) {
	auto result = object_ptr<Widget>(parent, controller, peer());
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
	not_null<PeerData*> peer)
: ContentWidget(parent, controller) {
	_inner = setInnerWidget(
		object_ptr<InnerWidget>(this, controller, peer, _filter.value()));
	_inner->notifyEnabled(
	) | rpl::take(1) | rpl::start_with_next([=](bool enabled) {
		setupNotifyCheckbox(enabled);
	}, _inner->lifetime());
	_inner->scrollToTop() | rpl::start_with_next([=] {
		scrollTo({ 0, 0 });
	}, _inner->lifetime());
}

void Widget::showFinished() {
	_shown = true;
	if (const auto bottom = _pinnedToBottom.data()) {
		bottom->toggle(true, anim::type::normal);
	}
}

void Widget::setupNotifyCheckbox(bool enabled) {
	_pinnedToBottom = Ui::CreateChild<Ui::SlideWrap<Ui::RpWidget>>(
		this,
		object_ptr<Ui::RpWidget>(this));
	const auto wrap = _pinnedToBottom.data();
	wrap->toggle(false, anim::type::instant);

	const auto bottom = wrap->entity();
	bottom->show();

	const auto notify = Ui::CreateChild<Ui::Checkbox>(
		bottom,
		tr::lng_peer_gifts_notify(),
		enabled);
	notify->show();

	notify->checkedChanges() | rpl::start_with_next([=](bool checked) {
		const auto api = &controller()->session().api();
		const auto show = controller()->uiShow();
		using Flag = MTPpayments_ToggleChatStarGiftNotifications::Flag;
		api->request(MTPpayments_ToggleChatStarGiftNotifications(
			MTP_flags(checked ? Flag::f_enabled : Flag()),
			_inner->peer()->input
		)).send();
		if (checked) {
			show->showToast(tr::lng_peer_gifts_notify_enabled(tr::now));
		}
	}, notify->lifetime());

	const auto &checkSt = st::defaultCheckbox;
	const auto checkTop = st::boxRadius + checkSt.margin.top();
	bottom->widthValue() | rpl::start_with_next([=](int width) {
		const auto normal = notify->naturalWidth()
			- checkSt.margin.left()
			- checkSt.margin.right();
		notify->resizeToWidth(normal);
		const auto checkLeft = (width - normal) / 2;
		notify->moveToLeft(checkLeft, checkTop);
	}, notify->lifetime());

	notify->heightValue() | rpl::start_with_next([=](int height) {
		bottom->resize(bottom->width(), st::boxRadius + height);
	}, notify->lifetime());

	const auto processHeight = [=] {
		setScrollBottomSkip(wrap->height());
		wrap->moveToLeft(wrap->x(), height() - wrap->height());
	};

	_inner->sizeValue(
	) | rpl::start_with_next([=](const QSize &s) {
		wrap->resizeToWidth(s.width());
		crl::on_main(wrap, processHeight);
	}, wrap->lifetime());

	rpl::combine(
		wrap->heightValue(),
		heightValue()
	) | rpl::start_with_next(processHeight, wrap->lifetime());

	if (_shown) {
		wrap->toggle(true, anim::type::normal);
	}
	_hasPinnedToBottom = true;
}

void Widget::fillTopBarMenu(const Ui::Menu::MenuCallback &addAction) {
	const auto filter = _filter.current();
	const auto change = [=](Fn<void(Filter&)> update) {
		auto now = _filter.current();
		update(now);
		_filter = now;
	};

	if (filter.sortByValue) {
		addAction(tr::lng_peer_gifts_filter_by_date(tr::now), [=] {
			change([](Filter &filter) { filter.sortByValue = false; });
		}, &st::menuIconSchedule);
	} else {
		addAction(tr::lng_peer_gifts_filter_by_value(tr::now), [=] {
			change([](Filter &filter) { filter.sortByValue = true; });
		}, &st::menuIconEarn);
	}

	addAction({ .isSeparator = true });

	addAction(tr::lng_peer_gifts_filter_unlimited(tr::now), [=] {
		change([](Filter &filter) {
			filter.skipUnlimited = !filter.skipUnlimited;
			if (filter.skipUnlimited
				&& filter.skipLimited
				&& filter.skipUnique) {
				filter.skipLimited = false;
			}
		});
	}, filter.skipUnlimited ? nullptr : &st::mediaPlayerMenuCheck);
	addAction(tr::lng_peer_gifts_filter_limited(tr::now), [=] {
		change([](Filter &filter) {
			filter.skipLimited = !filter.skipLimited;
			if (filter.skipUnlimited
				&& filter.skipLimited
				&& filter.skipUnique) {
				filter.skipUnlimited = false;
			}
		});
	}, filter.skipLimited ? nullptr : &st::mediaPlayerMenuCheck);
	addAction(tr::lng_peer_gifts_filter_unique(tr::now), [=] {
		change([](Filter &filter) {
			filter.skipUnique = !filter.skipUnique;
			if (filter.skipUnlimited
				&& filter.skipLimited
				&& filter.skipUnique) {
				filter.skipUnlimited = false;
			}
		});
	}, filter.skipUnique ? nullptr : &st::mediaPlayerMenuCheck);

	if (_inner->peer()->canManageGifts() && _inner->peer()->isChannel()) {
		addAction({ .isSeparator = true });

		addAction(tr::lng_peer_gifts_filter_saved(tr::now), [=] {
			change([](Filter &filter) {
				filter.skipSaved = !filter.skipSaved;
				if (filter.skipSaved && filter.skipUnsaved) {
					filter.skipUnsaved = false;
				}
			});
		}, filter.skipSaved ? nullptr : &st::mediaPlayerMenuCheck);
		addAction(tr::lng_peer_gifts_filter_unsaved(tr::now), [=] {
			change([](Filter &filter) {
				filter.skipUnsaved = !filter.skipUnsaved;
				if (filter.skipSaved && filter.skipUnsaved) {
					filter.skipSaved = false;
				}
			});
		}, filter.skipUnsaved ? nullptr : &st::mediaPlayerMenuCheck);
	}
}

rpl::producer<QString> Widget::title() {
	return tr::lng_peer_gifts_title();
}

rpl::producer<bool> Widget::desiredBottomShadowVisibility() {
	return _hasPinnedToBottom.value();
}

not_null<PeerData*> Widget::peer() const {
	return _inner->peer();
}

bool Widget::showInternal(not_null<ContentMemento*> memento) {
	if (!controller()->validateMementoPeer(memento)) {
		return false;
	}
	if (auto similarMemento = dynamic_cast<Memento*>(memento.get())) {
		if (similarMemento->peer() == peer()) {
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
	auto result = std::make_shared<Memento>(peer());
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
