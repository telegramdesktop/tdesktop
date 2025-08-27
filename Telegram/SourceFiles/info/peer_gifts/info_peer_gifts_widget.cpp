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
#include "boxes/share_box.h"
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
#include "info/info_memento.h"
#include "ui/boxes/confirm_box.h"
#include "ui/controls/sub_tabs.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
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
		not_null<Window::SessionController*> window,
		not_null<PeerData*> peer,
		rpl::producer<Descriptor> descriptor);

	[[nodiscard]] not_null<PeerData*> peer() const {
		return _peer;
	}
	[[nodiscard]] rpl::producer<bool> notifyEnabled() const {
		return _notifyEnabled.events();
	}
	[[nodiscard]] rpl::producer<Descriptor> descriptorChanges() const {
		return _descriptorChanges.events();
	}
	[[nodiscard]] rpl::producer<> scrollToTop() const {
		return _scrollToTop.events();
	}

	[[nodiscard]] rpl::producer<Data::GiftsUpdate> changes() const {
		return _collectionChanges.value();
	}

	[[nodiscard]] rpl::producer<bool> collectionEmptyValue() const {
		return _collectionEmpty.value();
	}

	void reloadCollection(int id);
	void editCollectionGifts(int id);
	void shareCollectionLink(const QString &username, int id);
	void editCollectionName(int id);
	void confirmDeleteCollection(int id);
	void collectionAdded(MTPStarGiftCollection result);
	void fillMenu(const Ui::Menu::MenuCallback &addAction);

	void saveState(not_null<Memento*> memento);
	void restoreState(not_null<Memento*> memento);

private:
	struct Entry {
		Data::SavedStarGift gift;
		GiftDescriptor descriptor;
	};
	struct Entries {
		std::vector<Entry> list;
		std::optional<Filter> filter;
		int total = 0;
		bool allLoaded = false;
	};
	struct View {
		std::unique_ptr<GiftButton> button;
		Data::SavedStarGiftId manageId;
		uint64 giftId = 0;
		int index = 0;
	};

public:
	InnerWidget(
		QWidget *parent,
		not_null<Window::SessionController*> window,
		not_null<PeerData*> peer,
		rpl::producer<Descriptor> descriptor,
		int addingToCollectionId,
		Entries all);

private:
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;
	void paintEvent(QPaintEvent *e) override;

	void subscribeToUpdates();
	void applyUpdateTo(Entries &entries, const Data::GiftUpdate &update);
	void loadCollections();
	void loadMore();
	void loaded(const MTPpayments_SavedStarGifts &result);
	void markInCollection(const Data::SavedStarGift &gift);
	void refreshButtons();
	void validateButtons();
	void showGift(int index);
	void showMenuFor(not_null<GiftButton*> button, QPoint point);
	void showMenuForCollection(int id);
	void refreshAbout();
	void refreshCollectionsTabs();

	void collectionRenamed(int id, QString name);
	void collectionRemoved(int id);

	void markPinned(std::vector<Entry>::iterator i);
	void markUnpinned(std::vector<Entry>::iterator i);

	int resizeGetHeight(int width) override;

	[[nodiscard]] auto pinnedSavedGifts()
		-> Fn<std::vector<Data::CreditsHistoryEntry>()>;

	const not_null<Window::SessionController*> _window;
	const not_null<PeerData*> _peer;
	const int _addingToCollectionId = 0;
	const GiftButtonMode _mode;

	rpl::variable<Descriptor> _descriptor;
	Delegate _delegate;
	std::unique_ptr<Ui::SubTabs> _collectionsTabs;
	std::unique_ptr<Ui::RpWidget> _about;
	rpl::event_stream<> _scrollToTop;
	rpl::variable<bool> _collectionEmpty;

	std::vector<Data::GiftCollection> _collections;

	Entries _all;
	std::map<int, Entries> _perCollection;
	not_null<Entries*> _entries;
	not_null<std::vector<Entry>*> _list;
	rpl::variable<Data::GiftsUpdate> _collectionChanges;
	base::flat_set<Data::SavedStarGiftId> _inCollection;

	MTP::Sender _api;
	mtpRequestId _loadMoreRequestId = 0;
	Fn<void()> _collectionsLoadedCallback;
	QString _offset;
	bool _reloading = false;
	bool _collectionsLoaded = false;

	rpl::event_stream<Descriptor> _descriptorChanges;
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
	not_null<Window::SessionController*> window,
	not_null<PeerData*> peer,
	rpl::producer<Descriptor> descriptor)
: InnerWidget(
	parent,
	window,
	peer,
	std::move(descriptor),
	0,
	{ .total = peer->peerGiftsCount() }) {
}

InnerWidget::InnerWidget(
	QWidget *parent,
	not_null<Window::SessionController*> window,
	not_null<PeerData*> peer,
	rpl::producer<Descriptor> descriptor,
	int addingToCollectionId,
	Entries all)
: BoxContentDivider(parent)
, _window(window)
, _peer(peer)
, _addingToCollectionId(addingToCollectionId)
, _mode(_addingToCollectionId
	? GiftButtonMode::Selection
	: GiftButtonMode::Minimal)
, _descriptor(std::move(descriptor))
, _delegate(&_window->session(), _mode)
, _all(std::move(all))
, _entries(&_all)
, _list(&_entries->list)
, _collectionChanges(Data::GiftsUpdate{
	.peer = _peer,
	.collectionId = addingToCollectionId,
})
, _api(&_peer->session().mtp()) {
	_singleMin = _delegate.buttonSize();

	if (peer->canManageGifts()) {
		subscribeToUpdates();
	}

	for (const auto &entry : _all.list) {
		markInCollection(entry.gift);
	}

	loadCollections();

	_window->session().data().giftsUpdates(
	) | rpl::start_with_next([=](const Data::GiftsUpdate &update) {
		if (update.peer != _peer) {
			return;
		}
		const auto added = base::flat_set<Data::SavedStarGiftId>{
			begin(update.added),
			end(update.added)
		};
		const auto removed = base::flat_set<Data::SavedStarGiftId>{
			begin(update.removed),
			end(update.removed)
		};
		const auto id = update.collectionId;
		const auto process = [&](Entries &entries) {
			for (auto &entry : entries.list) {
				if (added.contains(entry.gift.manageId)) {
					entry.gift.collectionIds.push_back(id);
				} else if (removed.contains(entry.gift.manageId)) {
					entry.gift.collectionIds.erase(
						ranges::remove(entry.gift.collectionIds, id),
						end(entry.gift.collectionIds));
				}
			}
		};
		for (auto &[_, entries] : _perCollection) {
			process(entries);
		}
		process(_all);
	}, lifetime());

	_descriptor.value(
	) | rpl::start_with_next([=](Descriptor now) {
		const auto id = now.collectionId;
		_collectionsLoadedCallback = nullptr;
		_api.request(base::take(_loadMoreRequestId)).cancel();
		_entries = id ? &_perCollection[id] : &_all;
		_list = &_entries->list;
		refreshButtons();
		refreshAbout();
		loadMore();
	}, lifetime());
}

void InnerWidget::loadCollections() {
	if (_addingToCollectionId) {
		return;
	}
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
		applyUpdateTo(_all, update);
		using Action = Data::GiftUpdate::Action;
		if (update.action == Action::Pin || update.action == Action::Unpin) {
			for (auto &[_, entries] : _perCollection) {
				applyUpdateTo(entries, update);
			}
		};
	}, lifetime());
}

void InnerWidget::applyUpdateTo(
		Entries &entries,
		const Data::GiftUpdate &update) {
	using Action = Data::GiftUpdate::Action;
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
	if (update.action == Action::Convert
		|| update.action == Action::Transfer
		|| update.action == Action::Delete) {
		_list->erase(i);
		if (entries.total > 0) {
			--entries.total;
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

void InnerWidget::collectionAdded(MTPStarGiftCollection result) {
	_collections.push_back(FromTL(&_window->session(), result));
	const auto id = _collections.back().id;
	refreshCollectionsTabs();

	_collectionsTabs->setActiveTab(QString::number(id));

	auto now = _descriptor.current();
	now.collectionId = id;
	_descriptorChanges.fire(std::move(now));
}

void InnerWidget::loadMore() {
	const auto descriptor = _descriptor.current();
	const auto filter = descriptor.filter;
	const auto filterChanged = (_entries->filter != filter);
	const auto allLoaded = !filterChanged && _entries->allLoaded;
	if (allLoaded || _loadMoreRequestId) {
		return;
	}
	using Flag = MTPpayments_GetSavedStarGifts::Flag;
	const auto collectionId = descriptor.collectionId;
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
		MTP_string(filterChanged ? QString() : _offset),
		MTP_int(kPerPage)
	)).done([=](const MTPpayments_SavedStarGifts &result) {
		const auto &data = result.data();

		const auto owner = &_peer->owner();
		owner->processUsers(data.vusers());
		owner->processChats(data.vchats());

		if (_addingToCollectionId || _collectionsLoaded) {
			loaded(result);
		} else {
			_collectionsLoadedCallback = [=] {
				loaded(result);
			};
		}
	}).fail([=] {
		_loadMoreRequestId = 0;
		_collectionsLoadedCallback = nullptr;
		_entries->filter = _descriptor.current().filter;
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
	const auto descriptor = _descriptor.current();
	const auto filter = descriptor.filter;
	if (!filter.skipsSomething()) {
		_entries->total = data.vcount().v;
	}
	if (_entries->filter != filter) {
		_entries->filter = filter;
		_list->clear();
	}
	_list->reserve(_list->size() + data.vgifts().v.size());

	const auto i = ranges::find(
		_collections,
		descriptor.collectionId,
		&Data::GiftCollection::id);
	const auto collection = (i != end(_collections)) ? &*i : nullptr;

	auto hasUnique = false;
	for (const auto &gift : data.vgifts().v) {
		if (auto parsed = Api::FromTL(_peer, gift)) {
			if (collection && !collection->icon) {
				collection->icon = parsed->info.document;
				refreshCollectionsTabs();
			}
			markInCollection(*parsed);
			auto descriptor = DescriptorForGift(_peer, *parsed);
			_list->push_back({
				.gift = std::move(*parsed),
				.descriptor = std::move(descriptor),
			});
			hasUnique = (parsed->info.unique != nullptr);
		}
	}
	if (_entries->allLoaded) {
		_entries->total = _entries->list.size();
	}
	refreshButtons();
	refreshAbout();

	if (hasUnique) {
		Ui::PreloadUniqueGiftResellPrices(&_peer->session());
	}
}

void InnerWidget::markInCollection(const Data::SavedStarGift &gift) {
	if (const auto collectionId = _addingToCollectionId) {
		const auto id = gift.manageId;
		if (ranges::contains(gift.collectionIds, collectionId)) {
			const auto &changes = _collectionChanges.current();
			if (!ranges::contains(changes.removed, id)) {
				_inCollection.emplace(id);
			}
		}
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
		view.button->toggleSelected(
			_addingToCollectionId && _inCollection.contains(manageId),
			anim::type::instant);
		view.button->setDescriptor(descriptor, _mode);
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

void InnerWidget::showMenuForCollection(int id) {
	if (_menu || _addingToCollectionId) {
		return;
	}
	_menu = base::make_unique_q<Ui::PopupMenu>(this, st::popupMenuWithIcons);
	const auto addAction = Ui::Menu::CreateAddActionCallback(_menu);
	addAction(tr::lng_gift_collection_add_button(tr::now), [=] {
		editCollectionGifts(id);
	}, &st::menuIconGiftPremium);
	if (const auto username = _peer->username(); !username.isEmpty()) {
		addAction(tr::lng_stories_album_share(tr::now), [=] {
			shareCollectionLink(username, id);
		}, &st::menuIconShare);
	}
	addAction(tr::lng_gift_collection_edit(tr::now), [=] {
		editCollectionName(id);
	}, &st::menuIconEdit);
	addAction({
		.text = tr::lng_gift_collection_delete(tr::now),
		.handler = [=] { confirmDeleteCollection(id); },
		.icon = &st::menuIconDeleteAttention,
		.isAttention = true,
	});
	_menu->popup(QCursor::pos());
}

void InnerWidget::shareCollectionLink(const QString &username, int id) {
	const auto url = _window->session().createInternalLinkFull(
		username + u"/c/"_q + QString::number(id));
	FastShareLink(_window, url);
}

void InnerWidget::editCollectionName(int id) {
	const auto done = [=](QString name) {
		collectionRenamed(id, name);
	};
	const auto i = ranges::find(_collections, id, &Data::GiftCollection::id);
	if (i == end(_collections)) {
		return;
	}
	_window->uiShow()->show(Box(
		EditCollectionNameBox,
		_window,
		peer(),
		id,
		i->title,
		done));
}

void InnerWidget::confirmDeleteCollection(int id) {
	const auto done = [=](Fn<void()> close) {
		_window->session().api().request(
			MTPpayments_DeleteStarGiftCollection(_peer->input, MTP_int(id))
		).send();
		collectionRemoved(id);
		close();
	};
	_window->uiShow()->show(Ui::MakeConfirmBox({
		.text = tr::lng_gift_collection_delete_sure(),
		.confirmed = crl::guard(this, done),
		.confirmText = tr::lng_gift_collection_delete_button(),
		.confirmStyle = &st::attentionBoxButton,
	}));
}

void InnerWidget::showMenuFor(not_null<GiftButton*> button, QPoint point) {
	if (_menu || _addingToCollectionId) {
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
		_window->uiShow(),
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

	if (const auto id = _addingToCollectionId) {
		auto &gift = (*_list)[index].gift;
		auto changes = _collectionChanges.current();
		const auto selected = _inCollection.contains(gift.manageId);
		if (selected) {
			_inCollection.remove(gift.manageId);
			if (ranges::contains(gift.collectionIds, id)) {
				changes.removed.push_back(gift.manageId);
			} else {
				changes.added.erase(
					ranges::remove(changes.added, gift.manageId),
					end(changes.added));
			}
		} else {
			_inCollection.emplace(gift.manageId);
			if (ranges::contains(gift.collectionIds, id)) {
				changes.removed.erase(
					ranges::remove(changes.removed, gift.manageId),
					end(changes.removed));
			} else {
				changes.added.push_back(gift.manageId);
			}
		}
		_collectionChanges = std::move(changes);

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
	const auto descriptor = _descriptor.current();
	const auto filter = descriptor.filter;
	const auto collectionId = descriptor.collectionId;
	const auto maybeEmpty = _list->empty();
	const auto knownEmpty = maybeEmpty
		&& (_entries->allLoaded || !_entries->total);
	const auto filteredEmpty = knownEmpty && filter.skipsSomething();
	const auto collectionCanAdd = knownEmpty
		&& descriptor.collectionId != 0
		&& _peer->canManageGifts();
	_collectionEmpty = !filteredEmpty && collectionCanAdd;
	if (filteredEmpty) {
		auto text = tr::lng_peer_gifts_empty_search(
			tr::now,
			Ui::Text::RichLangValue);
		text.append("\n\n").append(Ui::Text::Link(
			tr::lng_peer_gifts_view_all(tr::now)));
		auto about = std::make_unique<Ui::FlatLabel>(
			this,
			rpl::single(text),
			st::giftListAbout);
		about->setClickHandlerFilter([=](const auto &...) {
			auto now = _descriptor.current();
			now.filter = Filter();
			_descriptorChanges.fire(std::move(now));
			return false;
		});
		about->show();
		_about = std::move(about);
		resizeToWidth(width());
	} else if (collectionCanAdd) {
		auto about = std::make_unique<Ui::VerticalLayout>(this);
		about->add(
			object_ptr<Ui::FlatLabel>(
				about.get(),
				tr::lng_gift_collection_empty_title(),
				st::collectionEmptyTitle),
			st::collectionEmptyTitleMargin,
			style::al_top);
		about->add(
			object_ptr<Ui::FlatLabel>(
				about.get(),
				tr::lng_gift_collection_empty_text(),
				st::collectionEmptyText),
			st::collectionEmptyTextMargin,
			style::al_top);

		const auto button = about->add(
			object_ptr<Ui::RoundButton>(
				about.get(),
				rpl::single(QString()),
				st::collectionEmptyButton),
			st::collectionEmptyAddMargin,
			style::al_top);
		button->setText(tr::lng_gift_collection_add_button(
		) | rpl::map([](const QString &text) {
			return Ui::Text::IconEmoji(&st::collectionAddIcon).append(text);
		}));
		button->setTextTransform(
			Ui::RoundButton::TextTransform::NoTransform);
		button->setClickedCallback([=] {
			editCollectionGifts(collectionId);
		});

		about->show();
		_about = std::move(about);
		resizeToWidth(width());
	} else if ((!collectionId && _peer->isSelf())
		|| (!collectionId && !_peer->canManageGifts())
		|| maybeEmpty) {
		_about = std::make_unique<Ui::FlatLabel>(
			this,
			((maybeEmpty && !knownEmpty)
				? tr::lng_contacts_loading(Ui::Text::WithEntities)
				: _peer->isSelf()
				? tr::lng_peer_gifts_about_mine(Ui::Text::RichLangValue)
				: tr::lng_peer_gifts_about(
					lt_user,
					rpl::single(Ui::Text::Bold(_peer->shortName())),
					Ui::Text::RichLangValue)),
			st::giftListAbout);
		_about->show();
		resizeToWidth(width());
	} else if (_about) {
		_about = nullptr;
		resizeToWidth(width());
	}
}

void InnerWidget::reloadCollection(int id) {
	_perCollection[id].filter = std::optional<Filter>();
	_perCollection[id].allLoaded = false;

	auto now = _descriptor.current();
	now.filter = Filter();
	now.collectionId = id;
	_descriptorChanges.fire(std::move(now));

	_api.request(base::take(_loadMoreRequestId)).cancel();
	_collectionsLoadedCallback = nullptr;
	refreshButtons();
	refreshAbout();
	loadMore();
}

void InnerWidget::editCollectionGifts(int id) {
	const auto weak = base::make_weak(this);
	_window->uiShow()->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_gift_collection_add_title());
		box->setWidth(st::boxWideWidth);
		box->setStyle(st::collectionEditBox);

		struct State {
			rpl::variable<Descriptor> descriptor;
			rpl::variable<Data::GiftsUpdate> changes;
			base::unique_qptr<Ui::PopupMenu> menu;
			bool saving = false;
		};
		const auto state = box->lifetime().make_state<State>(State{
			.changes = Data::GiftsUpdate{
				.peer = _peer,
				.collectionId = id,
			},
		});
		const auto content = box->addRow(
			object_ptr<InnerWidget>(
				box,
				_window,
				_peer,
				state->descriptor.value(),
				id,
				(_all.filter == Filter()) ? _all : Entries()),
			style::margins());
		state->changes = content->changes();

		content->descriptorChanges(
		) | rpl::start_with_next([=](Descriptor now) {
			state->descriptor = now;
		}, content->lifetime());

		content->scrollToTop() | rpl::start_with_next([=] {
			box->scrollToY(0);
		}, content->lifetime());

		box->addTopButton(st::boxTitleClose, [=] {
			box->closeBox();
		});
		box->addTopButton(st::collectionEditMenuToggle, [=] {
			state->menu = base::make_unique_q<Ui::PopupMenu>(
				box,
				st::popupMenuWithIcons);
			content->fillMenu(
				Ui::Menu::CreateAddActionCallback(state->menu));
			state->menu->popup(QCursor::pos());
		});
		const auto weakBox = base::make_weak(box);
		auto text = state->changes.value(
		) | rpl::map([=](const Data::GiftsUpdate &update) {
			return (!update.added.empty() && update.removed.empty())
				? tr::lng_gift_collection_add_button()
				: tr::lng_settings_save();
		}) | rpl::flatten_latest();
		box->addButton(std::move(text), [=] {
			if (state->saving) {
				return;
			}
			auto add = QVector<MTPInputSavedStarGift>();
			auto remove = QVector<MTPInputSavedStarGift>();
			const auto &changes = state->changes.current();
			for (const auto &id : changes.added) {
				add.push_back(Api::InputSavedStarGiftId(id));
			}
			for (const auto &id : changes.removed) {
				remove.push_back(Api::InputSavedStarGiftId(id));
			}
			if (add.empty() && remove.empty()) {
				box->closeBox();
				return;
			}
			state->saving = true;
			const auto session = &_window->session();
			using Flag = MTPpayments_UpdateStarGiftCollection::Flag;
			session->api().request(
				MTPpayments_UpdateStarGiftCollection(
					MTP_flags(Flag()
						| (add.isEmpty() ? Flag() : Flag::f_add_stargift)
						| (remove.isEmpty()
							? Flag()
							: Flag::f_delete_stargift)),
					_peer->input,
					MTP_int(id),
					MTPstring(),
					MTP_vector<MTPInputSavedStarGift>(remove),
					MTP_vector<MTPInputSavedStarGift>(add),
					MTPVector<MTPInputSavedStarGift>())
			).done([=] {
				if (const auto strong = weakBox.get()) {
					state->saving = false;
					strong->closeBox();
				}
				session->data().notifyGiftsUpdate(base::duplicate(changes));
				if (const auto strong = weak.get()) {
					strong->reloadCollection(id);
				}
			}).fail([=](const MTP::Error &error) {
				if (const auto strong = weakBox.get()) {
					state->saving = false;
					strong->uiShow()->showToast(error.type());
				}
			}).send();
		});
	}));
}

void InnerWidget::refreshCollectionsTabs() {
	if (_collections.empty() || _addingToCollectionId) {
		if (base::take(_collectionsTabs)) {
			resizeToWidth(width());
		}
		return;
	}

	auto tabs = std::vector<Ui::SubTabs::Tab>();
	tabs.push_back({
		.id = u"all"_q,
		.text = tr::lng_gift_collection_all(tr::now, Ui::Text::WithEntities),
	});
	for (const auto &collection : _collections) {
		auto &per = _perCollection[collection.id];
		if (!per.allLoaded) {
			per.total = collection.count;
		}

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
	if (_peer->canManageGifts()) {
		tabs.push_back({
			.id = u"add"_q,
			.text = { '+' + tr::lng_gift_collection_add(tr::now) },
		});
	}
	const auto context = Core::TextContext({
		.session = &_window->session(),
	});
	if (!_collectionsTabs) {
		const auto selectedId = _descriptor.current().collectionId;
		const auto selected = (selectedId > 0
			&& ranges::contains(
				_collections,
				selectedId,
				&Data::GiftCollection::id))
			? QString::number(selectedId)
			: u"all"_q;
		_collectionsTabs = std::make_unique<Ui::SubTabs>(
			this,
			Ui::SubTabs::Options{ .selected = selected, .centered = true },
			std::move(tabs),
			context);
		_collectionsTabs->show();

		_collectionsTabs->activated(
		) | rpl::start_with_next([=](const QString &id) {
			if (id == u"add"_q) {
				const auto added = [=](MTPStarGiftCollection result) {
					collectionAdded(result);
				};
				_window->uiShow()->show(Box(
					NewCollectionBox,
					_window,
					peer(),
					Data::SavedStarGiftId(),
					added));
			} else {
				_collectionsTabs->setActiveTab(id);

				auto now = _descriptor.current();
				now.collectionId = (id == u"all"_q) ? 0 : id.toInt();
				_descriptorChanges.fire(std::move(now));
			}
		}, _collectionsTabs->lifetime());

		_collectionsTabs->contextMenuRequests(
		) | rpl::start_with_next([=](const QString &id) {
			if (id == u"add"_q
				|| id == u"all"_q
				|| !_peer->canManageGifts()) {
				return;
			}
			showMenuForCollection(id.toInt());
		}, _collectionsTabs->lifetime());
	} else {
		_collectionsTabs->setTabs(std::move(tabs), context);
	}
	resizeToWidth(width());
}

void InnerWidget::collectionRenamed(int id, QString name) {
	const auto i = ranges::find(_collections, id, &Data::GiftCollection::id);
	if (i != end(_collections)) {
		i->title = name;
		refreshCollectionsTabs();
	}
}

void InnerWidget::collectionRemoved(int id) {
	auto now = _descriptor.current();
	if (now.collectionId == id) {
		now.collectionId = 0;
		_descriptorChanges.fire(std::move(now));
	}
	Assert(_entries != &_perCollection[id]);
	_perCollection.erase(id);
	const auto removeFrom = [&](Entries &entries) {
		for (auto &entry : entries.list) {
			entry.gift.collectionIds.erase(
				ranges::remove(entry.gift.collectionIds, id),
				end(entry.gift.collectionIds));
		}
	};
	removeFrom(_all);
	for (auto &[_, entries] : _perCollection) {
		removeFrom(entries);
	}

	const auto i = ranges::find(_collections, id, &Data::GiftCollection::id);
	if (i != end(_collections)) {
		_collections.erase(i);
		refreshCollectionsTabs();
	}
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
	} else {
		result += padding.bottom();
	}

	const auto singlew = std::min(
		((available + skipw) / _perRow) - skipw,
		2 * _singleMin.width());
	Assert(singlew >= _singleMin.width());
	const auto singleh = _singleMin.height();

	_single = QSize(singlew, singleh);
	const auto rows = (count + _perRow - 1) / _perRow;
	const auto rowsPerCount = rows
		? rows
		: ((std::min(_entries->total, kPerPage) + _perRow - 1) / _perRow);
	const auto skiph = st::giftBoxGiftSkip.y();

	const auto resultPerCount = result
		+ (rowsPerCount
			? (padding.bottom() + rowsPerCount * (singleh + skiph) - skiph)
			: 0);
	result += rows
		? (padding.bottom() + rows * (singleh + skiph) - skiph)
		: 0;

	if (const auto about = _about.get()) {
		const auto margin = st::giftListAboutMargin;
		about->resizeToWidth(width - margin.left() - margin.right());
		about->moveToLeft(margin.left(), result + margin.top());
		result += margin.top() + about->height() + margin.bottom();
	}

	return std::max(result, resultPerCount);
}

void InnerWidget::saveState(not_null<Memento*> memento) {
	auto state = std::make_unique<ListState>();
	memento->setListState(std::move(state));
}

void InnerWidget::restoreState(not_null<Memento*> memento) {
	if (const auto state = memento->listState()) {

	}
}

void InnerWidget::fillMenu(const Ui::Menu::MenuCallback &addAction) {
	const auto canManage = _peer->canManageGifts();
	const auto descriptor = _descriptor.current();
	const auto filter = descriptor.filter;
	const auto change = [=](Fn<void(Filter&)> update) {
		auto now = _descriptor.current();
		update(now.filter);
		_descriptorChanges.fire(std::move(now));
	};

	const auto collectionId = descriptor.collectionId;
	if (!collectionId) {
		if (filter.sortByValue) {
			addAction(tr::lng_peer_gifts_filter_by_date(tr::now), [=] {
				change([](Filter &filter) { filter.sortByValue = false; });
			}, &st::menuIconSchedule);
		} else {
			addAction(tr::lng_peer_gifts_filter_by_value(tr::now), [=] {
				change([](Filter &filter) { filter.sortByValue = true; });
			}, &st::menuIconEarn);
		}
		if (canManage && !_addingToCollectionId) {
			const auto peer = _peer;
			const auto weak = base::make_weak(_window);
			addAction(tr::lng_gift_collection_add(tr::now), [=] {
				if (const auto strong = weak.get()) {
					const auto added = [=](MTPStarGiftCollection result) {
						collectionAdded(result);
					};
					strong->uiShow()->show(Box(
						NewCollectionBox,
						strong,
						peer,
						Data::SavedStarGiftId(),
						crl::guard(this, added)));
				}
			}, &st::menuIconAddToFolder);
		}
	} else if (canManage) {
		addAction(tr::lng_gift_collection_add_button(tr::now), [=] {
			editCollectionGifts(collectionId);
		}, &st::menuIconGiftPremium);

		addAction({
			.text = tr::lng_gift_collection_delete(tr::now),
			.handler = [=] { confirmDeleteCollection(collectionId); },
			.icon = &st::menuIconDeleteAttention,
			.isAttention = true,
		});
	}

	if (canManage || !collectionId) {
		addAction({ .isSeparator = true });
	}

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

Memento::Memento(not_null<Controller*> controller)
: ContentMemento(Tag{
	controller->giftsPeer(),
	controller->giftsCollectionId() }) {
}

Memento::Memento(not_null<PeerData*> peer, int collectionId)
: ContentMemento(Tag{ peer, collectionId }) {
}

Section Memento::section() const {
	return Section(Section::Type::PeerGifts);
}

object_ptr<ContentWidget> Memento::createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) {
	auto result = object_ptr<Widget>(parent, controller);
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

Widget::Widget(QWidget *parent, not_null<Controller*> controller)
: ContentWidget(parent, controller)
, _descriptor(Descriptor{
	.collectionId = controller->giftsCollectionId(),
}) {
	_inner = setInnerWidget(
		object_ptr<InnerWidget>(
			this,
			controller->parentController(),
			controller->giftsPeer(),
			_descriptor.value()));
	_emptyCollectionShown = _inner->collectionEmptyValue();
	_inner->notifyEnabled(
	) | rpl::take(1) | rpl::start_with_next([=](bool enabled) {
		_notifyEnabled = enabled;
		refreshBottom();
	}, _inner->lifetime());
	_inner->descriptorChanges(
	) | rpl::start_with_next([=](Descriptor descriptor) {
		_descriptor = descriptor;
	}, _inner->lifetime());
	_inner->scrollToTop() | rpl::start_with_next([=] {
		scrollTo({ 0, 0 });
	}, _inner->lifetime());

	rpl::combine(
		_descriptor.value(),
		_emptyCollectionShown.value()
	) | rpl::start_with_next([=] {
		refreshBottom();
	}, _inner->lifetime());
}

void Widget::refreshBottom() {
	const auto notify = _notifyEnabled.has_value();
	const auto descriptor = _descriptor.current();
	const auto shownId = descriptor.collectionId;
	const auto withButton = shownId
		&& peer()->canManageGifts()
		&& !_emptyCollectionShown.current();
	const auto wasBottom = _pinnedToBottom ? _pinnedToBottom->height() : 0;
	delete _pinnedToBottom.data();
	if (!notify && !withButton) {
		setScrollBottomSkip(0);
		_hasPinnedToBottom = false;
	} else if (withButton) {
		setupBottomButton(wasBottom);
	} else {
		setupNotifyCheckbox(wasBottom, *_notifyEnabled);
	}
}

void Widget::setupBottomButton(int wasBottomHeight) {
	_pinnedToBottom = Ui::CreateChild<Ui::SlideWrap<Ui::RpWidget>>(
		this,
		object_ptr<Ui::RpWidget>(this));
	const auto wrap = _pinnedToBottom.data();
	wrap->toggle(false, anim::type::instant);

	const auto bottom = wrap->entity();
	bottom->show();

	const auto button = Ui::CreateChild<Ui::RoundButton>(
		bottom,
		rpl::single(QString()),
		st::collectionEditBox.button);
	button->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
	button->setText(tr::lng_gift_collection_add_button(
	) | rpl::map([](const QString &text) {
		return Ui::Text::IconEmoji(&st::collectionAddIcon).append(text);
	}));
	button->show();
	_hasPinnedToBottom = true;

	button->setClickedCallback([=] {
		if (const auto id = _descriptor.current().collectionId) {
			_inner->editCollectionGifts(id);
		} else {
			refreshBottom();
		}
	});

	const auto buttonTop = st::boxRadius;
	bottom->widthValue() | rpl::start_with_next([=](int width) {
		const auto normal = width - 2 * buttonTop;
		button->resizeToWidth(normal);
		const auto buttonLeft = (width - normal) / 2;
		button->moveToLeft(buttonLeft, buttonTop);
	}, button->lifetime());

	button->heightValue() | rpl::start_with_next([=](int height) {
		bottom->resize(bottom->width(), st::boxRadius + height);
	}, button->lifetime());

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
		wrap->toggle(
			true,
			wasBottomHeight ? anim::type::instant : anim::type::normal);
	}
}

void Widget::showFinished() {
	_shown = true;
	if (const auto bottom = _pinnedToBottom.data()) {
		bottom->toggle(true, anim::type::normal);
	}
}

void Widget::setupNotifyCheckbox(int wasBottomHeight, bool enabled) {
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
		wrap->toggle(
			true,
			wasBottomHeight ? anim::type::instant : anim::type::normal);
	}
	_hasPinnedToBottom = true;
}

void Widget::fillTopBarMenu(const Ui::Menu::MenuCallback &addAction) {
	_inner->fillMenu(addAction);
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
	auto result = std::make_shared<Memento>(controller());
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

std::shared_ptr<Info::Memento> Make(not_null<PeerData*> peer, int albumId) {
	return std::make_shared<Info::Memento>(
		std::vector<std::shared_ptr<ContentMemento>>(
			1,
			std::make_shared<Memento>(peer, albumId)));
}

} // namespace Info::PeerGifts
