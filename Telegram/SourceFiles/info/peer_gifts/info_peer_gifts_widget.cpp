/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/peer_gifts/info_peer_gifts_widget.h"

#include "api/api_credits.h"
#include "api/api_hash.h"
#include "api/api_premium.h"
#include "apiwrap.h"
#include "boxes/star_gift_box.h"
#include "core/ui_integration.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_channel.h"
#include "data/data_credits.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "info/peer_gifts/info_peer_gifts_collections.h"
#include "info/peer_gifts/info_peer_gifts_common.h"
#include "info/info_controller.h"
#include "ui/controls/sub_tabs.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/box_content_divider.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/ui_utility.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
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
		.date = gift.date,
		.userpic = !gift.info.unique,
		.pinned = gift.pinned,
		.hidden = gift.hidden,
		.mine = to->isSelf(),
	};
}

[[nodiscard]] Data::GiftCollection FromTL(
		not_null<Main::Session*> session,
		const MTPStarGiftCollection &collection) {
	const auto &data = collection.data();
	return {
		.id = data.vcollection_id().v,
		.count = data.vgifts_count().v,
		.title = qs(data.vtitle()),
		.icon = (data.vicon()
			? session->data().processDocument(*data.vicon()).get()
			: nullptr),
		.hash = data.vhash().v,
	};
}

[[nodiscard]] std::vector<Data::GiftCollection> FromTL(
		not_null<Main::Session*> session,
		const MTPDpayments_starGiftCollections &data) {
	auto result = std::vector<Data::GiftCollection>();

	const auto &list = data.vcollections().v;
	result.reserve(list.size());
	for (const auto &collection : list) {
		result.push_back(FromTL(session, collection));
	}
	return result;
}

} // namespace

class InnerWidget final : public Ui::BoxContentDivider {
public:
	InnerWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		not_null<PeerData*> peer,
		rpl::producer<Filter> filter,
		rpl::producer<int> addingToCollectionId);

	[[nodiscard]] not_null<PeerData*> peer() const {
		return _peer;
	}
	[[nodiscard]] rpl::producer<bool> notifyEnabled() const {
		return _notifyEnabled.events();
	}
	[[nodiscard]] rpl::producer<> resetFilterRequests() const {
		return _resetFilterRequests.events();
	}
	[[nodiscard]] rpl::producer<int> addToCollectionRequests() const {
		return _addToCollectionRequests.events();
	}
	[[nodiscard]] rpl::producer<> scrollToTop() const {
		return _scrollToTop.events();
	}

	void saveAdding();
	void collectionAdded(MTPStarGiftCollection result);

	void saveState(not_null<Memento*> memento);
	void restoreState(not_null<Memento*> memento);

private:
	struct Entry {
		Data::SavedStarGift gift;
		GiftDescriptor descriptor;
	};
	struct Entries {
		std::vector<Entry> list;
		Filter filter;
		int total = 0;
		bool allLoaded = false;
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
	void loadCollections();
	void loadMore();
	void loaded(const MTPpayments_SavedStarGifts &result);
	void refreshButtons();
	void validateButtons();
	void showGift(int index);
	void showMenuFor(not_null<GiftButton*> button, QPoint point);
	void refreshAbout();
	void refreshCollectionsTabs();

	[[nodiscard]] int shownCollectionId() const;

	void markPinned(std::vector<Entry>::iterator i);
	void markUnpinned(std::vector<Entry>::iterator i);

	int resizeGetHeight(int width) override;

	[[nodiscard]] auto pinnedSavedGifts()
		-> Fn<std::vector<Data::CreditsHistoryEntry>()>;

	const not_null<Window::SessionController*> _window;
	rpl::variable<Filter> _filter;
	rpl::variable<int> _collectionId;
	rpl::variable<int> _addingToCollectionId;
	Delegate _delegate;
	const not_null<Controller*> _controller;
	std::unique_ptr<Ui::SubTabs> _collectionsTabs;
	std::unique_ptr<Ui::RpWidget> _about;
	const not_null<PeerData*> _peer;
	rpl::event_stream<> _scrollToTop;

	std::vector<Data::GiftCollection> _collections;

	Entries _all;
	base::flat_map<int, Entries> _perCollection;
	not_null<Entries*> _entries;
	not_null<std::vector<Entry>*> _list;

	MTP::Sender _api;
	mtpRequestId _loadMoreRequestId = 0;
	Fn<void()> _collectionsLoadedCallback;
	QString _offset;
	bool _reloading = false;
	bool _aboutFiltered = false;
	bool _aboutCollection = false;
	bool _collectionsLoaded = false;

	rpl::event_stream<int> _addToCollectionRequests;
	rpl::event_stream<> _resetFilterRequests;
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

	base::unique_qptr<Ui::PopupMenu> _menu;

};

InnerWidget::InnerWidget(
	QWidget *parent,
	not_null<Controller*> controller,
	not_null<PeerData*> peer,
	rpl::producer<Filter> filter,
	rpl::producer<int> addingToCollectionId)
: BoxContentDivider(parent)
, _window(controller->parentController())
, _filter(std::move(filter))
, _addingToCollectionId(std::move(addingToCollectionId))
, _delegate(&_window->session(), GiftButtonMode::Minimal)
, _controller(controller)
, _peer(peer)
, _all({ .total = _peer->peerGiftsCount() })
, _entries(&_all)
, _list(&_entries->list)
, _api(&_peer->session().mtp()) {
	_singleMin = _delegate.buttonSize();

	if (peer->canManageGifts()) {
		subscribeToUpdates();
	}

	loadCollections();

	_filter.value() | rpl::start_with_next([=] {
		_entries->allLoaded = false;
		_reloading = true;
		_api.request(base::take(_loadMoreRequestId)).cancel();
		_collectionsLoadedCallback = nullptr;
		refreshButtons();
		refreshAbout();
		loadMore();
	}, lifetime());

	_collectionId.changes() | rpl::start_with_next([=](int id) {
		_reloading = true;
		_api.request(base::take(_loadMoreRequestId)).cancel();
		_collectionsLoadedCallback = nullptr;
		_entries = id ? &_perCollection[id] : &_all;
		_list = &_entries->list;
		refreshButtons();
		refreshAbout();
		loadMore();
	}, lifetime());

	_addingToCollectionId.value(
	) | rpl::combine_previous(
	) | rpl::start_with_next([=](int previousId, int id) {
		_reloading = true;
		_api.request(base::take(_loadMoreRequestId)).cancel();
		_collectionsLoadedCallback = nullptr;
		if (!id) {
			_collectionId = previousId;
		}
		_entries = id ? &_all : &_perCollection[previousId];
		_list = &_entries->list;

		if (_collectionsTabs) {
			_collectionsTabs->setVisible(!id);
		}

		refreshAbout();
		refreshButtons();
		crl::on_main(this, [=] {
			loadMore();
		});
	}, lifetime());
}

void InnerWidget::loadCollections() {
	_api.request(MTPpayments_GetStarGiftCollections(
		_peer->input,
		MTP_long(Api::CountHash(_collections
			| ranges::views::transform(&Data::GiftCollection::hash)))
	)).done([=](const MTPpayments_StarGiftCollections &result) {
		result.match([&](const MTPDpayments_starGiftCollections &data) {
			_collections = FromTL(&_window->session(), data);

			refreshCollectionsTabs();
		}, [&](const MTPDpayments_starGiftCollectionsNotModified &) {
		});
		_collectionsLoaded = true;
		if (const auto onstack = base::take(_collectionsLoadedCallback)) {
			onstack();
		}
	}).fail([=] {
		_collectionsLoaded = true;
		if (const auto onstack = base::take(_collectionsLoadedCallback)) {
			onstack();
		}
	}).send();
}

void InnerWidget::subscribeToUpdates() {
	_peer->owner().giftUpdates(
	) | rpl::start_with_next([=](const Data::GiftUpdate &update) {
		const auto savedId = [](const Entry &entry) {
			return entry.gift.manageId;
		};
		const auto bySlug = [](const Entry &entry) {
			return entry.gift.info.unique
				? entry.gift.info.unique->slug
				: QString();
		};
		const auto i = update.id
			? ranges::find(*_list, update.id, savedId)
			: ranges::find(*_list, update.slug, bySlug);
		if (i == end(*_list)) {
			return;
		}
		const auto index = int(i - begin(*_list));
		using Action = Data::GiftUpdate::Action;
		if (update.action == Action::Convert
			|| update.action == Action::Transfer
			|| update.action == Action::Delete) {
			_list->erase(i);
			if (_entries->total > 0) {
				--_entries->total;
			}
			for (auto &view : _views) {
				if (view.index >= index) {
					--view.index;
				}
			}
		} else if (update.action == Action::Save
			|| update.action == Action::Unsave) {
			i->gift.hidden = (update.action == Action::Unsave);

			const auto unpin = i->gift.hidden && i->gift.pinned;
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
			if (unpin) {
				markUnpinned(i);
			}
		} else if (update.action == Action::Pin
			|| update.action == Action::Unpin) {
			if (update.action == Action::Pin) {
				markPinned(i);
			} else {
				markUnpinned(i);
			}
		} else if (update.action == Action::ResaleChange) {
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
		if (update.action == Action::Pin) {
			_scrollToTop.fire({});
		}
	}, lifetime());
}

void InnerWidget::markPinned(std::vector<Entry>::iterator i) {
	const auto index = int(i - begin(*_list));

	i->gift.pinned = true;
	v::match(i->descriptor, [](const GiftTypePremium &) {
	}, [&](GiftTypeStars &data) {
		data.pinned = true;
	});
	if (index) {
		std::rotate(begin(*_list), i, i + 1);
	}
	auto unpin = end(*_list);
	const auto session = &_window->session();
	const auto limit = session->appConfig().pinnedGiftsLimit();
	if (limit < _list->size()) {
		const auto j = begin(*_list) + limit;
		if (j->gift.pinned) {
			unpin = j;
		}
	}
	for (auto &view : _views) {
		if (view.index <= index) {
			view.index = -1;
			view.manageId = {};
		}
	}
	if (unpin != end(_entries->list)) {
		markUnpinned(unpin);
	}
}

void InnerWidget::markUnpinned(std::vector<Entry>::iterator i) {
	const auto index = int(i - begin(*_list));

	i->gift.pinned = false;
	v::match(i->descriptor, [](const GiftTypePremium &) {
	}, [&](GiftTypeStars &data) {
		data.pinned = false;
	});
	auto after = index + 1;
	for (auto j = i + 1; j != end(*_list); ++j) {
		if (!j->gift.pinned && j->gift.date <= i->gift.date) {
			break;
		}
		++after;
	}
	if (after == _list->size() && !_entries->allLoaded) {
		// We don't know if the correct position is exactly in the end
		// of the loaded part or later, so we hide it for now, let it
		// be loaded later while scrolling.
		_list->erase(i);
	} else if (after > index + 1) {
		std::rotate(i, i + 1, begin(*_list) + after);
	}
	for (auto &view : _views) {
		if (view.index >= index) {
			view.index = -1;
			view.manageId = {};
		}
	}
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

int InnerWidget::shownCollectionId() const {
	return _addingToCollectionId.current()
		? 0
		: _collectionId.current();
}

void InnerWidget::saveAdding() {
	using Flag = MTPpayments_UpdateStarGiftCollection::Flag;
	auto add = QVector<MTPInputSavedStarGift>();
	auto remove = QVector<MTPInputSavedStarGift>();
	const auto id = _addingToCollectionId.current();
	auto document = (DocumentData*)nullptr;
	for (const auto &entry : (*_list)) {
		if (ranges::contains(entry.gift.collectionIds, id)) {
			if (!document) {
				document = entry.gift.info.document;
			}
			add.push_back(Api::InputSavedStarGiftId(
				entry.gift.manageId,
				entry.gift.info.unique));
		} else {
			remove.push_back(Api::InputSavedStarGiftId(
				entry.gift.manageId,
				entry.gift.info.unique));
		}
	}
	_window->session().api().request(MTPpayments_UpdateStarGiftCollection(
		MTP_flags(Flag()
			| (add.isEmpty() ? Flag() : Flag::f_add_stargift)
			| (remove.isEmpty() ? Flag() : Flag::f_delete_stargift)),
		_peer->input,
		MTP_int(id),
		MTPstring(),
		MTP_vector<MTPInputSavedStarGift>(remove),
		MTP_vector<MTPInputSavedStarGift>(add),
		MTPVector<MTPInputSavedStarGift>()
	)).done([=] {
		_entries->allLoaded = false;
		_reloading = true;
		_api.request(base::take(_loadMoreRequestId)).cancel();
		_collectionsLoadedCallback = nullptr;
		refreshButtons();
		refreshAbout();
		loadMore();
	}).fail([=](const MTP::Error &error) {
		_window->showToast(error.type());
	}).send();
	_addToCollectionRequests.fire(0);

	const auto i = ranges::find(_collections, id, &Data::GiftCollection::id);
	if (i != end(_collections) && i->icon != document) {
		i->icon = document;
		refreshCollectionsTabs();
	}
}

void InnerWidget::collectionAdded(MTPStarGiftCollection result) {
	_collections.push_back(FromTL(&_window->session(), result));
	refreshCollectionsTabs();
}

void InnerWidget::loadMore() {
	if (_entries->allLoaded || _loadMoreRequestId) {
		return;
	}
	using Flag = MTPpayments_GetSavedStarGifts::Flag;
	const auto filter = _filter.current();
	const auto collectionId = shownCollectionId();
	_loadMoreRequestId = _api.request(MTPpayments_GetSavedStarGifts(
		MTP_flags((filter.sortByValue ? Flag::f_sort_by_value : Flag())
			| (filter.skipLimited ? Flag::f_exclude_limited : Flag())
			| (filter.skipUnlimited ? Flag::f_exclude_unlimited : Flag())
			| (filter.skipUnique ? Flag::f_exclude_unique : Flag())
			| (filter.skipSaved ? Flag::f_exclude_saved : Flag())
			| (filter.skipUnsaved ? Flag::f_exclude_unsaved : Flag())
			| (collectionId ? Flag::f_collection_id : Flag())),
		_peer->input,
		MTP_int(collectionId),
		MTP_string(_reloading ? QString() : _offset),
		MTP_int(kPerPage)
	)).done([=](const MTPpayments_SavedStarGifts &result) {
		const auto &data = result.data();

		const auto owner = &_peer->owner();
		owner->processUsers(data.vusers());
		owner->processChats(data.vchats());

		if (_collectionsLoaded) {
			loaded(result);
		} else {
			_collectionsLoadedCallback = [=] {
				loaded(result);
			};
		}
	}).fail([=] {
		_loadMoreRequestId = 0;
		_collectionsLoadedCallback = nullptr;
		_entries->allLoaded = true;
	}).send();
}

void InnerWidget::loaded(const MTPpayments_SavedStarGifts &result) {
	const auto &data = result.data();

	_loadMoreRequestId = 0;
	_collectionsLoadedCallback = nullptr;
	if (const auto enabled = data.vchat_notifications_enabled()) {
		_notifyEnabled.fire(mtpIsTrue(*enabled));
	}
	if (const auto next = data.vnext_offset()) {
		_offset = qs(*next);
	} else {
		_entries->allLoaded = true;
	}
	if (!_filter.current().skipsSomething()) {
		_entries->total = data.vcount().v;
	}

	if (base::take(_reloading)) {
		_list->clear();
	}
	_list->reserve(_list->size() + data.vgifts().v.size());
	auto hasUnique = false;
	for (const auto &gift : data.vgifts().v) {
		if (auto parsed = Api::FromTL(_peer, gift)) {
			auto descriptor = DescriptorForGift(_peer, *parsed);
			_list->push_back({
				.gift = std::move(*parsed),
				.descriptor = std::move(descriptor),
			});
			hasUnique = (parsed->info.unique != nullptr);
		}
	}
	refreshButtons();
	refreshAbout();

	if (hasUnique) {
		Ui::PreloadUniqueGiftResellPrices(&_peer->session());
	}
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
	const auto vskip = (_collectionsTabs && !_collectionsTabs->isHidden())
		? (padding.top() + _collectionsTabs->height() + padding.top())
		: padding.bottom();
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
	const auto addingToId = _addingToCollectionId.current();
	auto x = left;
	auto y = vskip + fromRow * oneh;
	auto views = std::vector<View>();
	views.reserve((tillRow - fromRow) * _perRow);
	const auto idUsed = [&](uint64 giftId, int column, int row) {
		for (auto j = row; j != tillRow; ++j) {
			for (auto i = column; i != _perRow; ++i) {
				const auto index = j * _perRow + i;
				if (index >= _list->size()) {
					return false;
				} else if ((*_list)[index].gift.info.id == giftId) {
					return true;
				}
			}
			column = 0;
		}
		return false;
	};
	const auto add = [&](int column, int row) {
		const auto index = row * _perRow + column;
		if (index >= _list->size()) {
			return false;
		}
		const auto &entry = (*_list)[index];
		const auto &gift = entry.gift;
		const auto giftId = gift.info.id;
		const auto manageId = gift.manageId;
		const auto &descriptor = entry.descriptor;
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
				const auto raw = button.get();
				raw->contextMenuRequests(
				) | rpl::start_with_next([=](QPoint point) {
					showMenuFor(raw, point);
				}, raw->lifetime());
				raw->show();
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
		view.button->toggleSelected(addingToId
			&& ranges::contains(gift.collectionIds, addingToId));
		view.button->setDescriptor(descriptor, GiftButton::Mode::Minimal);
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

auto InnerWidget::pinnedSavedGifts()
-> Fn<std::vector<Data::CreditsHistoryEntry>()> {
	struct Entry {
		Data::SavedStarGiftId id;
		std::shared_ptr<Data::UniqueGift> unique;
	};
	auto entries = std::vector<Entry>();
	for (const auto &entry : *_list) {
		if (entry.gift.pinned) {
			Assert(entry.gift.info.unique != nullptr);
			entries.push_back({
				entry.gift.manageId,
				entry.gift.info.unique,
			});
		} else {
			break;
		}
	}
	return [entries] {
		auto result = std::vector<Data::CreditsHistoryEntry>();
		result.reserve(entries.size());
		for (const auto &entry : entries) {
			const auto &id = entry.id;
			result.push_back({
				.bareMsgId = uint64(id.userMessageId().bare),
				.bareEntryOwnerId = id.chat() ? id.chat()->id.value : 0,
				.giftChannelSavedId = id.chatSavedId(),
				.uniqueGift = entry.unique,
				.stargift = true,
			});
		}
		return result;
	};
}

void InnerWidget::showMenuFor(not_null<GiftButton*> button, QPoint point) {
	if (_menu) {
		return;
	}
	const auto index = [&] {
		for (const auto &view : _views) {
			if (view.button.get() == button) {
				return view.index;
			}
		}
		return -1;
	}();
	if (index < 0) {
		return;
	}

	auto entry = ::Settings::SavedStarGiftEntry(
		_peer,
		(*_list)[index].gift);
	entry.pinnedSavedGifts = pinnedSavedGifts();
	_menu = base::make_unique_q<Ui::PopupMenu>(this, st::popupMenuWithIcons);
	::Settings::FillSavedStarGiftMenu(
		_controller->uiShow(),
		_menu.get(),
		entry,
		::Settings::SavedStarGiftMenuType::List);
	if (_menu->empty()) {
		return;
	}
	_menu->popup(point);
}

void InnerWidget::showGift(int index) {
	Expects(index >= 0 && index < _list->size());

	if (const auto id = _addingToCollectionId.current()) {
		auto &gift = (*_list)[index].gift;
		const auto selected = ranges::contains(gift.collectionIds, id);
		if (selected) {
			gift.collectionIds.erase(
				ranges::remove(gift.collectionIds, id),
				end(gift.collectionIds));
		} else {
			gift.collectionIds.push_back(id);
		}
		const auto view = ranges::find(_views, index, &View::index);
		if (view != end(_views)) {
			view->button->toggleSelected(!selected);
		}
		return;
	}

	_window->show(Box(
		::Settings::SavedStarGiftBox,
		_window,
		_peer,
		(*_list)[index].gift,
		pinnedSavedGifts()));
}

void InnerWidget::refreshAbout() {
	const auto filter = _filter.current();
	const auto collectionEmpty = _collectionId.current()
		&& _entries->allLoaded
		&& !_entries->total
		&& _list->empty();
	const auto filteredEmpty = !collectionEmpty
		&& _entries->allLoaded
		&& _list->empty()
		&& filter.skipsSomething();

	if (collectionEmpty) {
		const auto id = _collectionId.current();
		auto about = std::make_unique<Ui::VerticalLayout>(this);
		about->add(
			object_ptr<Ui::CenterWrap<>>(
				about.get(),
				object_ptr<Ui::FlatLabel>(
					about.get(),
					tr::lng_gift_collection_empty_title(),
					st::collectionEmptyTitle)),
			st::collectionEmptyTitleMargin);
		about->add(
			object_ptr<Ui::CenterWrap<>>(
				about.get(),
				object_ptr<Ui::FlatLabel>(
					about.get(),
					tr::lng_gift_collection_empty_text(),
					st::collectionEmptyText)),
			st::collectionEmptyTextMargin);

		const auto button = about->add(
			object_ptr<Ui::CenterWrap<Ui::RoundButton>>(
				about.get(),
				object_ptr<Ui::RoundButton>(
					about.get(),
					tr::lng_gift_collection_empty_button(),
					st::defaultActiveButton)),
			st::collectionEmptyAddMargin)->entity();
		button->setTextTransform(
			Ui::RoundButton::TextTransform::NoTransform);
		button->setClickedCallback([=] {
			_addToCollectionRequests.fire_copy(id);
		});

		about->show();
		_about = std::move(about);
		_aboutCollection = true;
		resizeToWidth(width());
	} else if (filteredEmpty) {
		auto text = tr::lng_peer_gifts_empty_search(
			tr::now,
			Ui::Text::RichLangValue);
		if (_entries->total > 0) {
			text.append("\n\n").append(Ui::Text::Link(
				tr::lng_peer_gifts_view_all(tr::now)));
		}
		auto about = std::make_unique<Ui::FlatLabel>(
			this,
			rpl::single(text),
			st::giftListAbout);
		about->setClickHandlerFilter([=](const auto &...) {
			_resetFilterRequests.fire({});
			return false;
		});
		about->show();
		_about = std::move(about);
		_aboutFiltered = true;
		resizeToWidth(width());
	} else if (_aboutFiltered || _aboutCollection) {
		_about = nullptr;
		_aboutFiltered = false;
		_aboutCollection = false;
	}

	if (!_peer->isSelf() && _peer->canManageGifts() && !_list->empty()) {
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

void InnerWidget::refreshCollectionsTabs() {
	if (_collections.empty()) {
		if (base::take(_collectionsTabs)) {
			resizeToWidth(width());
		}
		return;
	}
	auto tabs = std::vector<Ui::SubTabs::Tab>();
	tabs.push_back({
		.id = u"all"_q,
		.text = tr::lng_gift_stars_tabs_all(tr::now, Ui::Text::WithEntities),
	});
	for (const auto &collection : _collections) {
		auto title = TextWithEntities();
		if (collection.icon) {
			title.append(
				Data::SingleCustomEmoji(collection.icon)
			).append(' ');
		}
		title.append(collection.title);
		tabs.push_back({
			.id = QString::number(collection.id),
			.text = std::move(title),
		});
	}
	tabs.push_back({
		.id = u"add"_q,
		.text = { '+' + tr::lng_gift_collection_add(tr::now) },
	});
	const auto context = Core::TextContext({
		.session = &_controller->session(),
	});
	if (!_collectionsTabs) {
		_collectionsTabs = std::make_unique<Ui::SubTabs>(
			this,
			Ui::SubTabs::Options{ .selected = u"all"_q, .centered = true},
			std::move(tabs),
			context);
		_collectionsTabs->show();

		_collectionsTabs->activated(
		) | rpl::start_with_next([=](const QString &id) {
			if (id == u"add"_q) {
				const auto added = [=](MTPStarGiftCollection result) {
					collectionAdded(result);
				};
				_controller->uiShow()->show(Box(
					NewCollectionBox,
					_controller,
					peer(),
					Data::SavedStarGiftId(),
					added));
			} else {
				_collectionsTabs->setActiveTab(id);
				_collectionId = (id == u"all"_q) ? 0 : id.toInt();
			}
		}, _collectionsTabs->lifetime());
	} else {
		_collectionsTabs->setTabs(std::move(tabs), context);
	}
	resizeToWidth(width());
}

int InnerWidget::resizeGetHeight(int width) {
	const auto padding = st::giftBoxPadding;
	const auto count = int(_list->size());
	const auto available = width - padding.left() - padding.right();
	const auto skipw = st::giftBoxGiftSkip.x();
	_perRow = std::min(
		(available + skipw) / (_singleMin.width() + skipw),
		std::max(count, 1));
	if (!_perRow) {
		return 0;
	}
	auto result = 0;
	if (_collectionsTabs && !_collectionsTabs->isHidden()) {
		result += padding.top();
		_collectionsTabs->resizeToWidth(width);
		_collectionsTabs->move(0, result);
		 result += _collectionsTabs->height();
	}

	const auto singlew = std::min(
		((available + skipw) / _perRow) - skipw,
		2 * _singleMin.width());
	Assert(singlew >= _singleMin.width());
	const auto singleh = _singleMin.height();

	_single = QSize(singlew, singleh);
	const auto rows = (count + _perRow - 1) / _perRow;
	const auto skiph = st::giftBoxGiftSkip.y();

	result += rows
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
: ContentMemento(peer, nullptr, nullptr, PeerId()) {
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
		object_ptr<InnerWidget>(
			this,
			controller,
			peer,
			_filter.value(),
			_addingToCollectionId.value()));
	_inner->notifyEnabled(
	) | rpl::take(1) | rpl::start_with_next([=](bool enabled) {
		setupNotifyCheckbox(enabled);
	}, _inner->lifetime());
	_inner->resetFilterRequests(
	) | rpl::start_with_next([=] {
		_filter = Filter();
	}, _inner->lifetime());
	_inner->addToCollectionRequests(
	) | rpl::start_with_next([=](int id) {
		_addingToCollectionId = id;
		_filter = Filter();
	}, _inner->lifetime());
	_inner->scrollToTop() | rpl::start_with_next([=] {
		scrollTo({ 0, 0 });
	}, _inner->lifetime());

	_addingToCollectionId.changes() | rpl::start_with_next([=](int id) {
		toggleAddButton(id != 0);
	}, _inner->lifetime());
}

void Widget::toggleAddButton(bool shown) {
	if (!shown) {
		setScrollBottomSkip(0);
		_hasPinnedToBottom = false;
		delete _pinnedToBottom.data();
	} else if (!_pinnedToBottom) {
		_pinnedToBottom = Ui::CreateChild<Ui::SlideWrap<Ui::RpWidget>>(
			this,
			object_ptr<Ui::RpWidget>(this));
		const auto wrap = _pinnedToBottom.data();
		wrap->toggle(false, anim::type::instant);

		const auto bottom = wrap->entity();
		bottom->show();

		const auto save = Ui::CreateChild<Ui::RoundButton>(
			bottom,
			tr::lng_settings_save(),
			st::defaultActiveButton);
		save->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
		save->show();

		save->setClickedCallback([=] {
			_inner->saveAdding();
		});

		const auto &checkSt = st::defaultCheckbox;
		const auto checkTop = st::boxRadius;
		bottom->widthValue() | rpl::start_with_next([=](int width) {
			const auto normal = width
				- checkSt.margin.left()
				- checkSt.margin.right();
			save->resizeToWidth(normal);
			const auto checkLeft = (width - normal) / 2;
			save->moveToLeft(checkLeft, checkTop);
		}, save->lifetime());

		save->heightValue() | rpl::start_with_next([=](int height) {
			bottom->resize(bottom->width(), st::boxRadius * 2 + height);
		}, save->lifetime());

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
	const auto peer = _inner->peer();
	const auto canManage = peer->canManageGifts();
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

	if (canManage) {
		const auto weak = base::make_weak(
			(Window::SessionNavigation*)controller());
		addAction(tr::lng_gift_collection_add(tr::now), [=] {
			if (const auto strong = weak.get()) {
				const auto added = [=](MTPStarGiftCollection result) {
					_inner->collectionAdded(result);
				};
				strong->uiShow()->show(Box(
					NewCollectionBox,
					strong,
					peer,
					Data::SavedStarGiftId(),
					added));
			}
		}, &st::menuIconAddToFolder);
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

	if (canManage) {
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
	return _addingToCollectionId.value() | rpl::map([](int id) {
		return id
			? tr::lng_gift_collection_add_title()
			: tr::lng_peer_gifts_title();
	}) | rpl::flatten_latest();
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
