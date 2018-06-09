/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_session.h"

#include "observer_peer.h"
#include "auth_session.h"
#include "apiwrap.h"
#include "history/history.h"
#include "history/history_item_components.h"
#include "history/history_media.h"
#include "history/view/history_view_element.h"
#include "inline_bots/inline_bot_layout_item.h"
#include "storage/localstorage.h"
#include "data/data_media_types.h"
#include "data/data_feed.h"
#include "data/data_photo.h"
#include "data/data_document.h"
#include "data/data_web_page.h"
#include "data/data_game.h"

namespace Data {
namespace {

using ViewElement = HistoryView::Element;

// s: box 100x100
// m: box 320x320
// x: box 800x800
// y: box 1280x1280
// w: box 2560x2560 // if loading this fix HistoryPhoto::updateFrom
// a: crop 160x160
// b: crop 320x320
// c: crop 640x640
// d: crop 1280x1280
const auto ThumbLevels = QByteArray::fromRawData("sambcxydw", 9);
const auto MediumLevels = QByteArray::fromRawData("mbcxasydw", 9);
const auto FullLevels = QByteArray::fromRawData("yxwmsdcba", 9);

void UpdateImage(ImagePtr &old, ImagePtr now) {
	if (now->isNull()) {
		return;
	}
	if (old->isNull()) {
		old = now;
	} else if (const auto delayed = old->toDelayedStorageImage()) {
		const auto location = now->location();
		if (!location.isNull()) {
			delayed->setStorageLocation(location);
		}
	}
}

} // namespace

Session::Session(not_null<AuthSession*> session)
: _session(session)
, _groups(this) {
	setupContactViewsViewer();
	setupChannelLeavingViewer();
}

void Session::setupContactViewsViewer() {
	Notify::PeerUpdateViewer(
		Notify::PeerUpdate::Flag::UserIsContact
	) | rpl::map([](const Notify::PeerUpdate &update) {
		return update.peer->asUser();
	}) | rpl::filter([](UserData *user) {
		return user != nullptr;
	}) | rpl::start_with_next([=](not_null<UserData*> user) {
		userIsContactUpdated(user);
	}, _lifetime);
}

void Session::setupChannelLeavingViewer() {
	Notify::PeerUpdateViewer(
		Notify::PeerUpdate::Flag::ChannelAmIn
	) | rpl::map([](const Notify::PeerUpdate &update) {
		return update.peer->asChannel();
	}) | rpl::filter([](ChannelData *channel) {
		return (channel != nullptr)
			&& !(channel->amIn())
			&& (channel->feed() != nullptr);
	}) | rpl::start_with_next([=](not_null<ChannelData*> channel) {
		channel->clearFeed();
	}, _lifetime);
}

Session::~Session() = default;

template <typename Method>
void Session::enumerateItemViews(
		not_null<const HistoryItem*> item,
		Method method) {
	if (const auto i = _views.find(item); i != _views.end()) {
		for (const auto view : i->second) {
			method(view);
		}
	}
}

void Session::photoLoadSettingsChanged() {
	for (const auto &[id, photo] : _photos) {
		photo->automaticLoadSettingsChanged();
	}
}

void Session::voiceLoadSettingsChanged() {
	for (const auto &[id, document] : _documents) {
		if (document->isVoiceMessage()) {
			document->automaticLoadSettingsChanged();
		}
	}
}

void Session::animationLoadSettingsChanged() {
	for (const auto &[id, document] : _documents) {
		if (document->isAnimation()) {
			document->automaticLoadSettingsChanged();
		}
	}
}

void Session::notifyPhotoLayoutChanged(not_null<const PhotoData*> photo) {
	if (const auto i = _photoItems.find(photo); i != end(_photoItems)) {
		for (const auto item : i->second) {
			notifyItemLayoutChange(item);
		}
	}
}

void Session::notifyDocumentLayoutChanged(
		not_null<const DocumentData*> document) {
	const auto i = _documentItems.find(document);
	if (i != end(_documentItems)) {
		for (const auto item : i->second) {
			notifyItemLayoutChange(item);
		}
	}
	if (const auto items = InlineBots::Layout::documentItems()) {
		if (const auto i = items->find(document); i != items->end()) {
			for (const auto item : i->second) {
				item->layoutChanged();
			}
		}
	}
}

void Session::requestDocumentViewRepaint(
		not_null<const DocumentData*> document) {
	const auto i = _documentItems.find(document);
	if (i != end(_documentItems)) {
		for (const auto item : i->second) {
			requestItemRepaint(item);
		}
	}
}

void Session::markMediaRead(not_null<const DocumentData*> document) {
	const auto i = _documentItems.find(document);
	if (i != end(_documentItems)) {
		_session->api().markMediaRead({ begin(i->second), end(i->second) });
	}
}

void Session::notifyItemLayoutChange(not_null<const HistoryItem*> item) {
	_itemLayoutChanges.fire_copy(item);
	enumerateItemViews(item, [&](not_null<ViewElement*> view) {
		notifyViewLayoutChange(view);
	});
}

rpl::producer<not_null<const HistoryItem*>> Session::itemLayoutChanged() const {
	return _itemLayoutChanges.events();
}

void Session::notifyViewLayoutChange(not_null<const ViewElement*> view) {
	_viewLayoutChanges.fire_copy(view);
}

rpl::producer<not_null<const ViewElement*>> Session::viewLayoutChanged() const {
	return _viewLayoutChanges.events();
}

void Session::notifyItemIdChange(IdChange event) {
	_itemIdChanges.fire_copy(event);

	const auto refreshViewDataId = [](not_null<ViewElement*> view) {
		view->refreshDataId();
	};
	enumerateItemViews(event.item, refreshViewDataId);
	if (const auto group = Auth().data().groups().find(event.item)) {
		const auto leader = group->items.back();
		if (leader != event.item) {
			enumerateItemViews(leader, refreshViewDataId);
		}
	}
}

rpl::producer<Session::IdChange> Session::itemIdChanged() const {
	return _itemIdChanges.events();
}

void Session::requestItemRepaint(not_null<const HistoryItem*> item) {
	_itemRepaintRequest.fire_copy(item);
	enumerateItemViews(item, [&](not_null<const ViewElement*> view) {
		requestViewRepaint(view);
	});
}

rpl::producer<not_null<const HistoryItem*>> Session::itemRepaintRequest() const {
	return _itemRepaintRequest.events();
}

void Session::requestViewRepaint(not_null<const ViewElement*> view) {
	_viewRepaintRequest.fire_copy(view);
}

rpl::producer<not_null<const ViewElement*>> Session::viewRepaintRequest() const {
	return _viewRepaintRequest.events();
}

void Session::requestItemResize(not_null<const HistoryItem*> item) {
	_itemResizeRequest.fire_copy(item);
	enumerateItemViews(item, [&](not_null<ViewElement*> view) {
		requestViewResize(view);
	});
}

rpl::producer<not_null<const HistoryItem*>> Session::itemResizeRequest() const {
	return _itemResizeRequest.events();
}

void Session::requestViewResize(not_null<ViewElement*> view) {
	view->setPendingResize();
	_viewResizeRequest.fire_copy(view);
	notifyViewLayoutChange(view);
}

rpl::producer<not_null<ViewElement*>> Session::viewResizeRequest() const {
	return _viewResizeRequest.events();
}

void Session::requestItemViewRefresh(not_null<HistoryItem*> item) {
	if (const auto view = item->mainView()) {
		view->setPendingResize();
	}
	_itemViewRefreshRequest.fire_copy(item);
}

rpl::producer<not_null<HistoryItem*>> Session::itemViewRefreshRequest() const {
	return _itemViewRefreshRequest.events();
}

void Session::requestItemTextRefresh(not_null<HistoryItem*> item) {
	if (const auto i = _views.find(item); i != _views.end()) {
		for (const auto view : i->second) {
			if (const auto media = view->media()) {
				media->parentTextUpdated();
			}
		}
	}
}

void Session::requestAnimationPlayInline(not_null<HistoryItem*> item) {
	_animationPlayInlineRequest.fire_copy(item);
}

rpl::producer<not_null<HistoryItem*>> Session::animationPlayInlineRequest() const {
	return _animationPlayInlineRequest.events();
}

void Session::notifyItemRemoved(not_null<const HistoryItem*> item) {
	_itemRemoved.fire_copy(item);
	groups().unregisterMessage(item);
}

rpl::producer<not_null<const HistoryItem*>> Session::itemRemoved() const {
	return _itemRemoved.events();
}

void Session::notifyViewRemoved(not_null<const ViewElement*> view) {
	_viewRemoved.fire_copy(view);
}

rpl::producer<not_null<const ViewElement*>> Session::viewRemoved() const {
	return _viewRemoved.events();
}

void Session::notifyHistoryUnloaded(not_null<const History*> history) {
	_historyUnloaded.fire_copy(history);
}

rpl::producer<not_null<const History*>> Session::historyUnloaded() const {
	return _historyUnloaded.events();
}

void Session::notifyHistoryCleared(not_null<const History*> history) {
	_historyCleared.fire_copy(history);
}

rpl::producer<not_null<const History*>> Session::historyCleared() const {
	return _historyCleared.events();
}

void Session::notifyHistoryChangeDelayed(not_null<History*> history) {
	history->setHasPendingResizedItems();
	_historiesChanged.insert(history);
}

rpl::producer<not_null<History*>> Session::historyChanged() const {
	return _historyChanged.events();
}

void Session::sendHistoryChangeNotifications() {
	for (const auto history : base::take(_historiesChanged)) {
		_historyChanged.fire_copy(history);
	}
}

void Session::removeMegagroupParticipant(
		not_null<ChannelData*> channel,
		not_null<UserData*> user) {
	_megagroupParticipantRemoved.fire({ channel, user });
}

auto Session::megagroupParticipantRemoved() const
-> rpl::producer<MegagroupParticipant> {
	return _megagroupParticipantRemoved.events();
}

rpl::producer<not_null<UserData*>> Session::megagroupParticipantRemoved(
		not_null<ChannelData*> channel) const {
	return megagroupParticipantRemoved(
	) | rpl::filter([channel](auto updateChannel, auto user) {
		return (updateChannel == channel);
	}) | rpl::map([](auto updateChannel, auto user) {
		return user;
	});
}

void Session::addNewMegagroupParticipant(
		not_null<ChannelData*> channel,
		not_null<UserData*> user) {
	_megagroupParticipantAdded.fire({ channel, user });
}

auto Session::megagroupParticipantAdded() const
-> rpl::producer<MegagroupParticipant> {
	return _megagroupParticipantAdded.events();
}

rpl::producer<not_null<UserData*>> Session::megagroupParticipantAdded(
		not_null<ChannelData*> channel) const {
	return megagroupParticipantAdded(
	) | rpl::filter([channel](auto updateChannel, auto user) {
		return (updateChannel == channel);
	}) | rpl::map([](auto updateChannel, auto user) {
		return user;
	});
}

void Session::notifyFeedUpdated(
		not_null<Feed*> feed,
		FeedUpdateFlag update) {
	_feedUpdates.fire({ feed, update });
}

rpl::producer<FeedUpdate> Session::feedUpdated() const {
	return _feedUpdates.events();
}

void Session::notifyStickersUpdated() {
	_stickersUpdated.fire({});
}

rpl::producer<> Session::stickersUpdated() const {
	return _stickersUpdated.events();
}

void Session::notifySavedGifsUpdated() {
	_savedGifsUpdated.fire({});
}

rpl::producer<> Session::savedGifsUpdated() const {
	return _savedGifsUpdated.events();
}

void Session::userIsContactUpdated(not_null<UserData*> user) {
	const auto i = _contactViews.find(peerToUser(user->id));
	if (i != _contactViews.end()) {
		for (const auto view : i->second) {
			requestViewResize(view);
		}
	}
}

HistoryItemsList Session::idsToItems(
		const MessageIdsList &ids) const {
	return ranges::view::all(
		ids
	) | ranges::view::transform([](const FullMsgId &fullId) {
		return App::histItemById(fullId);
	}) | ranges::view::filter([](HistoryItem *item) {
		return item != nullptr;
	}) | ranges::view::transform([](HistoryItem *item) {
		return not_null<HistoryItem*>(item);
	}) | ranges::to_vector;
}

MessageIdsList Session::itemsToIds(
		const HistoryItemsList &items) const {
	return ranges::view::all(
		items
	) | ranges::view::transform([](not_null<HistoryItem*> item) {
		return item->fullId();
	}) | ranges::to_vector;
}

MessageIdsList Session::itemOrItsGroup(not_null<HistoryItem*> item) const {
	if (const auto group = groups().find(item)) {
		return itemsToIds(group->items);
	}
	return { 1, item->fullId() };
}

void Session::setPinnedDialog(const Dialogs::Key &key, bool pinned) {
	setIsPinned(key, pinned);
}

void Session::applyPinnedDialogs(const QVector<MTPDialog> &list) {
	clearPinnedDialogs();
	for (auto i = list.size(); i != 0;) {
		const auto &dialog = list[--i];
		switch (dialog.type()) {
		case mtpc_dialog: {
			const auto &dialogData = dialog.c_dialog();
			if (const auto peer = peerFromMTP(dialogData.vpeer)) {
				setPinnedDialog(App::history(peer), true);
			}
		} break;

		//case mtpc_dialogFeed: { // #feed
		//	const auto &feedData = dialog.c_dialogFeed();
		//	const auto feedId = feedData.vfeed_id.v;
		//	setPinnedDialog(feed(feedId), true);
		//} break;

		default: Unexpected("Type in ApiWrap::applyDialogsPinned.");
		}
	}
}

void Session::applyPinnedDialogs(const QVector<MTPDialogPeer> &list) {
	clearPinnedDialogs();
	for (auto i = list.size(); i != 0;) {
		const auto &dialogPeer = list[--i];
		switch (dialogPeer.type()) {
		case mtpc_dialogPeer: {
			const auto &peerData = dialogPeer.c_dialogPeer();
			if (const auto peerId = peerFromMTP(peerData.vpeer)) {
				setPinnedDialog(App::history(peerId), true);
			}
		} break;
		//case mtpc_dialogPeerFeed: { // #feed
		//	const auto &feedData = dialogPeer.c_dialogPeerFeed();
		//	const auto feedId = feedData.vfeed_id.v;
		//	setPinnedDialog(feed(feedId), true);
		//} break;
		}
	}
}

int Session::pinnedDialogsCount() const {
	return _pinnedDialogs.size();
}

const std::deque<Dialogs::Key> &Session::pinnedDialogsOrder() const {
	return _pinnedDialogs;
}

void Session::clearPinnedDialogs() {
	while (!_pinnedDialogs.empty()) {
		setPinnedDialog(_pinnedDialogs.back(), false);
	}
}

void Session::reorderTwoPinnedDialogs(
		const Dialogs::Key &key1,
		const Dialogs::Key &key2) {
	const auto &order = pinnedDialogsOrder();
	const auto index1 = ranges::find(order, key1) - begin(order);
	const auto index2 = ranges::find(order, key2) - begin(order);
	Assert(index1 >= 0 && index1 < order.size());
	Assert(index2 >= 0 && index2 < order.size());
	Assert(index1 != index2);
	std::swap(_pinnedDialogs[index1], _pinnedDialogs[index2]);
	key1.entry()->cachePinnedIndex(index2 + 1);
	key2.entry()->cachePinnedIndex(index1 + 1);
}

void Session::setIsPinned(const Dialogs::Key &key, bool pinned) {
	const auto already = ranges::find(_pinnedDialogs, key);
	if (pinned) {
		if (already != end(_pinnedDialogs)) {
			auto saved = std::move(*already);
			const auto alreadyIndex = already - end(_pinnedDialogs);
			const auto count = int(size(_pinnedDialogs));
			Assert(alreadyIndex < count);
			for (auto index = alreadyIndex + 1; index != count; ++index) {
				_pinnedDialogs[index - 1] = std::move(_pinnedDialogs[index]);
				_pinnedDialogs[index - 1].entry()->cachePinnedIndex(index);
			}
			_pinnedDialogs.back() = std::move(saved);
			_pinnedDialogs.back().entry()->cachePinnedIndex(count);
		} else {
			_pinnedDialogs.push_back(key);
			if (_pinnedDialogs.size() > Global::PinnedDialogsCountMax()) {
				_pinnedDialogs.front().entry()->cachePinnedIndex(0);
				_pinnedDialogs.pop_front();

				auto index = 0;
				for (const auto &pinned : _pinnedDialogs) {
					pinned.entry()->cachePinnedIndex(++index);
				}
			} else {
				key.entry()->cachePinnedIndex(_pinnedDialogs.size());
			}
		}
	} else if (!pinned && already != end(_pinnedDialogs)) {
		key.entry()->cachePinnedIndex(0);
		_pinnedDialogs.erase(already);
		auto index = 0;
		for (const auto &pinned : _pinnedDialogs) {
			pinned.entry()->cachePinnedIndex(++index);
		}
	}
}

not_null<PhotoData*> Session::photo(PhotoId id) {
	auto i = _photos.find(id);
	if (i == _photos.end()) {
		i = _photos.emplace(id, std::make_unique<PhotoData>(id)).first;
	}
	return i->second.get();
}

not_null<PhotoData*> Session::photo(const MTPPhoto &data) {
	switch (data.type()) {
	case mtpc_photo:
		return photo(data.c_photo());

	case mtpc_photoEmpty:
		return photo(data.c_photoEmpty().vid.v);
	}
	Unexpected("Type in Session::photo().");
}

not_null<PhotoData*> Session::photo(const MTPDphoto &data) {
	const auto result = photo(data.vid.v);
	photoApplyFields(result, data);
	return result;
}

not_null<PhotoData*> Session::photo(
		const MTPPhoto &data,
		const PreparedPhotoThumbs &thumbs) {
	auto thumb = (const QPixmap*)nullptr;
	auto medium = (const QPixmap*)nullptr;
	auto full = (const QPixmap*)nullptr;
	auto thumbLevel = -1;
	auto mediumLevel = -1;
	auto fullLevel = -1;
	for (auto i = thumbs.cbegin(), e = thumbs.cend(); i != e; ++i) {
		const auto newThumbLevel = ThumbLevels.indexOf(i.key());
		const auto newMediumLevel = MediumLevels.indexOf(i.key());
		const auto newFullLevel = FullLevels.indexOf(i.key());
		if (newThumbLevel < 0 || newMediumLevel < 0 || newFullLevel < 0) {
			continue;
		}
		if (thumbLevel < 0 || newThumbLevel < thumbLevel) {
			thumbLevel = newThumbLevel;
			thumb = &i.value();
		}
		if (mediumLevel < 0 || newMediumLevel < mediumLevel) {
			mediumLevel = newMediumLevel;
			medium = &i.value();
		}
		if (fullLevel < 0 || newFullLevel < fullLevel) {
			fullLevel = newFullLevel;
			full = &i.value();
		}
	}
	if (!thumb || !medium || !full) {
		return photo(0);
	}
	switch (data.type()) {
	case mtpc_photo:
		return photo(
			data.c_photo().vid.v,
			data.c_photo().vaccess_hash.v,
			data.c_photo().vdate.v,
			ImagePtr(*thumb, "JPG"),
			ImagePtr(*medium, "JPG"),
			ImagePtr(*full, "JPG"));

	case mtpc_photoEmpty:
		return photo(data.c_photoEmpty().vid.v);
	}
	Unexpected("Type in Session::photo() with prepared thumbs.");
}

not_null<PhotoData*> Session::photo(
		PhotoId id,
		const uint64 &access,
		TimeId date,
		const ImagePtr &thumb,
		const ImagePtr &medium,
		const ImagePtr &full) {
	const auto result = photo(id);
	photoApplyFields(
		result,
		access,
		date,
		thumb,
		medium,
		full);
	return result;
}

void Session::photoConvert(
		not_null<PhotoData*> original,
		const MTPPhoto &data) {
	const auto id = [&] {
		switch (data.type()) {
		case mtpc_photo: return data.c_photo().vid.v;
		case mtpc_photoEmpty: return data.c_photoEmpty().vid.v;
		}
		Unexpected("Type in Session::photoConvert().");
	}();
	if (original->id != id) {
		auto i = _photos.find(id);
		if (i == _photos.end()) {
			const auto j = _photos.find(original->id);
			Assert(j != _photos.end());
			auto owned = std::move(j->second);
			_photos.erase(j);
			i = _photos.emplace(id, std::move(owned)).first;
		}

		original->id = id;
		original->uploadingData = nullptr;

		if (i->second.get() != original) {
			photoApplyFields(i->second.get(), data);
		}
	}
	photoApplyFields(original, data);
}

PhotoData *Session::photoFromWeb(
		const MTPWebDocument &data,
		ImagePtr thumb) {
	const auto full = ImagePtr(data);
	if (full->isNull()) {
		return nullptr;
	}
	const auto width = full->width();
	const auto height = full->height();
	if (thumb->isNull()) {
		auto thumbsize = shrinkToKeepAspect(width, height, 100, 100);
		thumb = ImagePtr(thumbsize.width(), thumbsize.height());
	}

	auto mediumsize = shrinkToKeepAspect(width, height, 320, 320);
	auto medium = ImagePtr(mediumsize.width(), mediumsize.height());

	return photo(
		rand_value<PhotoId>(),
		uint64(0),
		unixtime(),
		thumb,
		medium,
		full);
}

void Session::photoApplyFields(
		not_null<PhotoData*> photo,
		const MTPPhoto &data) {
	if (data.type() == mtpc_photo) {
		photoApplyFields(photo, data.c_photo());
	}
}

void Session::photoApplyFields(
		not_null<PhotoData*> photo,
		const MTPDphoto &data) {
		auto thumb = (const MTPPhotoSize*)nullptr;
	auto medium = (const MTPPhotoSize*)nullptr;
	auto full = (const MTPPhotoSize*)nullptr;
	auto thumbLevel = -1;
	auto mediumLevel = -1;
	auto fullLevel = -1;
	for (const auto &sizeData : data.vsizes.v) {
		const auto sizeLetter = [&] {
			switch (sizeData.type()) {
			case mtpc_photoSizeEmpty: return char(0);
			case mtpc_photoSize: {
				const auto &data = sizeData.c_photoSize();
				return data.vtype.v.isEmpty() ? char(0) : data.vtype.v[0];
			} break;
			case mtpc_photoCachedSize: {
				const auto &data = sizeData.c_photoCachedSize();
				return data.vtype.v.isEmpty() ? char(0) : data.vtype.v[0];
			} break;
			}
			Unexpected("Type in photo size.");
		}();
		if (!sizeLetter) continue;

		const auto newThumbLevel = ThumbLevels.indexOf(sizeLetter);
		const auto newMediumLevel = MediumLevels.indexOf(sizeLetter);
		const auto newFullLevel = FullLevels.indexOf(sizeLetter);
		if (newThumbLevel < 0 || newMediumLevel < 0 || newFullLevel < 0) {
			continue;
		}
		if (thumbLevel < 0 || newThumbLevel < thumbLevel) {
			thumbLevel = newThumbLevel;
			thumb = &sizeData;
		}
		if (mediumLevel < 0 || newMediumLevel < mediumLevel) {
			mediumLevel = newMediumLevel;
			medium = &sizeData;
		}
		if (fullLevel < 0 || newFullLevel < fullLevel) {
			fullLevel = newFullLevel;
			full = &sizeData;
		}
	}
	if (thumb && medium && full) {
		photoApplyFields(
			photo,
			data.vaccess_hash.v,
			data.vdate.v,
			App::image(*thumb),
			App::image(*medium),
			App::image(*full));
	}
}

void Session::photoApplyFields(
		not_null<PhotoData*> photo,
		const uint64 &access,
		TimeId date,
		const ImagePtr &thumb,
		const ImagePtr &medium,
		const ImagePtr &full) {
	if (!date) {
		return;
	}
	photo->access = access;
	photo->date = date;
	UpdateImage(photo->thumb, thumb);
	UpdateImage(photo->medium, medium);
	UpdateImage(photo->full, full);
}

not_null<DocumentData*> Session::document(DocumentId id) {
	auto i = _documents.find(id);
	if (i == _documents.cend()) {
		i = _documents.emplace(
			id,
			std::make_unique<DocumentData>(id, _session)).first;
	}
	return i->second.get();
}

not_null<DocumentData*> Session::document(const MTPDocument &data) {
	switch (data.type()) {
	case mtpc_document:
		return document(data.c_document());

	case mtpc_documentEmpty:
		return document(data.c_documentEmpty().vid.v);
	}
	Unexpected("Type in Session::document().");
}

not_null<DocumentData*> Session::document(const MTPDdocument &data) {
	const auto result = document(data.vid.v);
	documentApplyFields(result, data);
	return result;
}

not_null<DocumentData*> Session::document(
		const MTPdocument &data,
		const QPixmap &thumb) {
	switch (data.type()) {
	case mtpc_documentEmpty:
		return document(data.c_documentEmpty().vid.v);

	case mtpc_document: {
		const auto &fields = data.c_document();
		return document(
			fields.vid.v,
			fields.vaccess_hash.v,
			fields.vversion.v,
			fields.vdate.v,
			fields.vattributes.v,
			qs(fields.vmime_type),
			ImagePtr(thumb, "JPG"),
			fields.vdc_id.v,
			fields.vsize.v,
			StorageImageLocation());
	} break;
	}
	Unexpected("Type in Session::document() with thumb.");
}

not_null<DocumentData*> Session::document(
		DocumentId id,
		const uint64 &access,
		int32 version,
		TimeId date,
		const QVector<MTPDocumentAttribute> &attributes,
		const QString &mime,
		const ImagePtr &thumb,
		int32 dc,
		int32 size,
		const StorageImageLocation &thumbLocation) {
	const auto result = document(id);
	documentApplyFields(
		result,
		access,
		version,
		date,
		attributes,
		mime,
		thumb,
		dc,
		size,
		thumbLocation);
	return result;
}

void Session::documentConvert(
		not_null<DocumentData*> original,
		const MTPDocument &data) {
	const auto id = [&] {
		switch (data.type()) {
		case mtpc_document: return data.c_document().vid.v;
		case mtpc_documentEmpty: return data.c_documentEmpty().vid.v;
		}
		Unexpected("Type in Session::documentConvert().");
	}();
	const auto oldKey = original->mediaKey();
	const auto idChanged = (original->id != id);
	const auto sentSticker = idChanged && (original->sticker() != nullptr);
	if (idChanged) {
		auto i = _documents.find(id);
		if (i == _documents.end()) {
			const auto j = _documents.find(original->id);
			Assert(j != _documents.end());
			auto owned = std::move(j->second);
			_documents.erase(j);
			i = _documents.emplace(id, std::move(owned)).first;
		}

		original->id = id;
		original->status = FileReady;
		original->uploadingData = nullptr;

		if (i->second.get() != original) {
			documentApplyFields(i->second.get(), data);
		}
	}
	documentApplyFields(original, data);
	if (idChanged) {
		const auto newKey = original->mediaKey();
		if (oldKey != newKey) {
			if (original->isVoiceMessage()) {
				Local::copyAudio(oldKey, newKey);
			} else if (original->sticker() || original->isAnimation()) {
				Local::copyStickerImage(oldKey, newKey);
			}
		}
		if (savedGifs().indexOf(original) >= 0) {
			Local::writeSavedGifs();
		}
	}
}

DocumentData *Session::documentFromWeb(
		const MTPWebDocument &data,
		ImagePtr thumb) {
	switch (data.type()) {
	case mtpc_webDocument:
		return documentFromWeb(data.c_webDocument(), thumb);

	case mtpc_webDocumentNoProxy:
		return documentFromWeb(data.c_webDocumentNoProxy(), thumb);

	}
	Unexpected("Type in Session::documentFromWeb.");
}

DocumentData *Session::documentFromWeb(
		const MTPDwebDocument &data,
		ImagePtr thumb) {
	const auto result = document(
		rand_value<DocumentId>(),
		uint64(0),
		int32(0),
		unixtime(),
		data.vattributes.v,
		data.vmime_type.v,
		thumb,
		MTP::maindc(),
		int32(0), // data.vsize.v
		StorageImageLocation());
	result->setWebLocation(WebFileLocation(
		data.vdc_id.v,
		data.vurl.v,
		data.vaccess_hash.v));
	return result;
}

DocumentData *Session::documentFromWeb(
		const MTPDwebDocumentNoProxy &data,
		ImagePtr thumb) {
	const auto result = document(
		rand_value<DocumentId>(),
		uint64(0),
		int32(0),
		unixtime(),
		data.vattributes.v,
		data.vmime_type.v,
		thumb,
		MTP::maindc(),
		int32(0), // data.vsize.v
		StorageImageLocation());
	result->setContentUrl(qs(data.vurl));
	return result;
}

void Session::documentApplyFields(
		not_null<DocumentData*> document,
		const MTPDocument &data) {
	if (data.type() == mtpc_document) {
		documentApplyFields(document, data.c_document());
	}
}

void Session::documentApplyFields(
		not_null<DocumentData*> document,
		const MTPDdocument &data) {
	documentApplyFields(
		document,
		data.vaccess_hash.v,
		data.vversion.v,
		data.vdate.v,
		data.vattributes.v,
		qs(data.vmime_type),
		App::image(data.vthumb),
		data.vdc_id.v,
		data.vsize.v,
		StorageImageLocation::FromMTP(data.vthumb));
}

void Session::documentApplyFields(
		not_null<DocumentData*> document,
		const uint64 &access,
		int32 version,
		TimeId date,
		const QVector<MTPDocumentAttribute> &attributes,
		const QString &mime,
		const ImagePtr &thumb,
		int32 dc,
		int32 size,
		const StorageImageLocation &thumbLocation) {
	if (!date) {
		return;
	}
	document->setattributes(attributes);
	document->setRemoteVersion(version);
	if (dc != 0 && access != 0) {
		document->setRemoteLocation(dc, access);
	}
	document->date = date;
	document->setMimeString(mime);
	if (!thumb->isNull()
		&& (document->thumb->isNull()
			|| document->thumb->width() < thumb->width()
			|| document->thumb->height() < thumb->height())) {
		document->thumb = thumb;
	}
	document->size = size;
	document->recountIsImage();
	if (document->sticker()
		&& document->sticker()->loc.isNull()
		&& !thumbLocation.isNull()) {
		document->sticker()->loc = thumbLocation;
	}
}

not_null<WebPageData*> Session::webpage(WebPageId id) {
	auto i = _webpages.find(id);
	if (i == _webpages.cend()) {
		i = _webpages.emplace(id, std::make_unique<WebPageData>(id)).first;
	}
	return i->second.get();
}

not_null<WebPageData*> Session::webpage(const MTPWebPage &data) {
	switch (data.type()) {
	case mtpc_webPage:
		return webpage(data.c_webPage());
	case mtpc_webPageEmpty: {
		const auto result = webpage(data.c_webPageEmpty().vid.v);
		if (result->pendingTill > 0) {
			result->pendingTill = -1; // failed
		}
		return result;
	} break;
	case mtpc_webPagePending:
		return webpage(data.c_webPagePending());
	case mtpc_webPageNotModified:
		LOG(("API Error: "
			"webPageNotModified is unexpected in Session::webpage()."));
		return webpage(0);
	}
	Unexpected("Type in Session::webpage().");
}

not_null<WebPageData*> Session::webpage(const MTPDwebPage &data) {
	const auto result = webpage(data.vid.v);
	webpageApplyFields(result, data);
	return result;
}

not_null<WebPageData*> Session::webpage(const MTPDwebPagePending &data) {
	constexpr auto kDefaultPendingTimeout = 60;
	const auto result = webpage(data.vid.v);
	webpageApplyFields(
		result,
		QString(),
		QString(),
		QString(),
		QString(),
		QString(),
		TextWithEntities(),
		nullptr,
		nullptr,
		0,
		QString(),
		data.vdate.v
			? data.vdate.v
			: (unixtime() + kDefaultPendingTimeout));
	return result;
}

not_null<WebPageData*> Session::webpage(
		WebPageId id,
		const QString &siteName,
		const TextWithEntities &content) {
	return webpage(
		id,
		qsl("article"),
		QString(),
		QString(),
		siteName,
		QString(),
		content,
		nullptr,
		nullptr,
		0,
		QString(),
		TimeId(0));
}

not_null<WebPageData*> Session::webpage(
		WebPageId id,
		const QString &type,
		const QString &url,
		const QString &displayUrl,
		const QString &siteName,
		const QString &title,
		const TextWithEntities &description,
		PhotoData *photo,
		DocumentData *document,
		int duration,
		const QString &author,
		TimeId pendingTill) {
	const auto result = webpage(id);
	webpageApplyFields(
		result,
		type,
		url,
		displayUrl,
		siteName,
		title,
		description,
		photo,
		document,
		duration,
		author,
		pendingTill);
	return result;
}

void Session::webpageApplyFields(
		not_null<WebPageData*> page,
		const MTPDwebPage &data) {
	auto description = TextWithEntities {
		data.has_description()
			? TextUtilities::Clean(qs(data.vdescription))
			: QString()
	};
	const auto siteName = data.has_site_name()
		? qs(data.vsite_name)
		: QString();
	auto parseFlags = TextParseLinks | TextParseMultiline | TextParseRichText;
	if (siteName == qstr("Twitter") || siteName == qstr("Instagram")) {
		parseFlags |= TextParseHashtags | TextParseMentions;
	}
	TextUtilities::ParseEntities(description, parseFlags);
	const auto pendingTill = TimeId(0);
	webpageApplyFields(
		page,
		data.has_type() ? qs(data.vtype) : qsl("article"),
		qs(data.vurl),
		qs(data.vdisplay_url),
		siteName,
		data.has_title() ? qs(data.vtitle) : QString(),
		description,
		data.has_photo() ? photo(data.vphoto).get() : nullptr,
		data.has_document() ? document(data.vdocument).get() : nullptr,
		data.has_duration() ? data.vduration.v : 0,
		data.has_author() ? qs(data.vauthor) : QString(),
		pendingTill);
}

void Session::webpageApplyFields(
		not_null<WebPageData*> page,
		const QString &type,
		const QString &url,
		const QString &displayUrl,
		const QString &siteName,
		const QString &title,
		const TextWithEntities &description,
		PhotoData *photo,
		DocumentData *document,
		int duration,
		const QString &author,
		TimeId pendingTill) {
	if (!page->pendingTill && pendingTill > 0) {
		_session->api().requestWebPageDelayed(page);
	}
	const auto changed = page->applyChanges(
		type,
		url,
		displayUrl,
		siteName,
		title,
		description,
		photo,
		document,
		duration,
		author,
		pendingTill);
	if (changed) {
		notifyWebPageUpdateDelayed(page);
	}
}

not_null<GameData*> Session::game(GameId id) {
	auto i = _games.find(id);
	if (i == _games.cend()) {
		i = _games.emplace(id, std::make_unique<GameData>(id)).first;
	}
	return i->second.get();
}

not_null<GameData*> Session::game(const MTPDgame &data) {
	const auto result = game(data.vid.v);
	gameApplyFields(result, data);
	return result;
}

not_null<GameData*> Session::game(
		GameId id,
		const uint64 &accessHash,
		const QString &shortName,
		const QString &title,
		const QString &description,
		PhotoData *photo,
		DocumentData *document) {
	const auto result = game(id);
	gameApplyFields(
		result,
		accessHash,
		shortName,
		title,
		description,
		photo,
		document);
	return result;
}

void Session::gameConvert(
		not_null<GameData*> original,
		const MTPGame &data) {
	Expects(data.type() == mtpc_game);

	const auto id = data.c_game().vid.v;
	if (original->id != id) {
		auto i = _games.find(id);
		if (i == _games.end()) {
			const auto j = _games.find(original->id);
			Assert(j != _games.end());
			auto owned = std::move(j->second);
			_games.erase(j);
			i = _games.emplace(id, std::move(owned)).first;
		}

		original->id = id;
		original->accessHash = 0;

		if (i->second.get() != original) {
			gameApplyFields(i->second.get(), data.c_game());
		}
	}
	gameApplyFields(original, data.c_game());
}

void Session::gameApplyFields(
		not_null<GameData*> game,
		const MTPDgame &data) {
	gameApplyFields(
		game,
		data.vaccess_hash.v,
		qs(data.vshort_name),
		qs(data.vtitle),
		qs(data.vdescription),
		photo(data.vphoto),
		data.has_document() ? document(data.vdocument).get() : nullptr);
}

void Session::gameApplyFields(
		not_null<GameData*> game,
		const uint64 &accessHash,
		const QString &shortName,
		const QString &title,
		const QString &description,
		PhotoData *photo,
		DocumentData *document) {
	if (game->accessHash) {
		return;
	}
	game->accessHash = accessHash;
	game->shortName = TextUtilities::Clean(shortName);
	game->title = TextUtilities::SingleLine(title);
	game->description = TextUtilities::Clean(description);
	game->photo = photo;
	game->document = document;
	notifyGameUpdateDelayed(game);
}

void Session::registerPhotoItem(
		not_null<const PhotoData*> photo,
		not_null<HistoryItem*> item) {
	_photoItems[photo].insert(item);
}

void Session::unregisterPhotoItem(
		not_null<const PhotoData*> photo,
		not_null<HistoryItem*> item) {
	const auto i = _photoItems.find(photo);
	if (i != _photoItems.end()) {
		auto &items = i->second;
		if (items.remove(item) && items.empty()) {
			_photoItems.erase(i);
		}
	}
}

void Session::registerDocumentItem(
		not_null<const DocumentData*> document,
		not_null<HistoryItem*> item) {
	_documentItems[document].insert(item);
}

void Session::unregisterDocumentItem(
		not_null<const DocumentData*> document,
		not_null<HistoryItem*> item) {
	const auto i = _documentItems.find(document);
	if (i != _documentItems.end()) {
		auto &items = i->second;
		if (items.remove(item) && items.empty()) {
			_documentItems.erase(i);
		}
	}
}

void Session::registerWebPageView(
		not_null<const WebPageData*> page,
		not_null<ViewElement*> view) {
	_webpageViews[page].insert(view);
}

void Session::unregisterWebPageView(
		not_null<const WebPageData*> page,
		not_null<ViewElement*> view) {
	const auto i = _webpageViews.find(page);
	if (i != _webpageViews.end()) {
		auto &items = i->second;
		if (items.remove(view) && items.empty()) {
			_webpageViews.erase(i);
		}
	}
}

void Session::registerWebPageItem(
		not_null<const WebPageData*> page,
		not_null<HistoryItem*> item) {
	_webpageItems[page].insert(item);
}

void Session::unregisterWebPageItem(
		not_null<const WebPageData*> page,
		not_null<HistoryItem*> item) {
	const auto i = _webpageItems.find(page);
	if (i != _webpageItems.end()) {
		auto &items = i->second;
		if (items.remove(item) && items.empty()) {
			_webpageItems.erase(i);
		}
	}
}

void Session::registerGameView(
		not_null<const GameData*> game,
		not_null<ViewElement*> view) {
	_gameViews[game].insert(view);
}

void Session::unregisterGameView(
		not_null<const GameData*> game,
		not_null<ViewElement*> view) {
	const auto i = _gameViews.find(game);
	if (i != _gameViews.end()) {
		auto &items = i->second;
		if (items.remove(view) && items.empty()) {
			_gameViews.erase(i);
		}
	}
}

void Session::registerContactView(
		UserId contactId,
		not_null<ViewElement*> view) {
	if (!contactId) {
		return;
	}
	_contactViews[contactId].insert(view);
}

void Session::unregisterContactView(
		UserId contactId,
		not_null<ViewElement*> view) {
	if (!contactId) {
		return;
	}
	const auto i = _contactViews.find(contactId);
	if (i != _contactViews.end()) {
		auto &items = i->second;
		if (items.remove(view) && items.empty()) {
			_contactViews.erase(i);
		}
	}
}

void Session::registerContactItem(
		UserId contactId,
		not_null<HistoryItem*> item) {
	if (!contactId) {
		return;
	}
	const auto contact = App::userLoaded(contactId);
	const auto canShare = contact ? contact->canShareThisContact() : false;

	_contactItems[contactId].insert(item);

	if (contact && canShare != contact->canShareThisContact()) {
		Notify::peerUpdatedDelayed(
			contact,
			Notify::PeerUpdate::Flag::UserCanShareContact);
	}

	if (const auto i = _views.find(item); i != _views.end()) {
		for (const auto view : i->second) {
			if (const auto media = view->media()) {
				media->updateSharedContactUserId(contactId);
			}
		}
	}
}

void Session::unregisterContactItem(
		UserId contactId,
		not_null<HistoryItem*> item) {
	if (!contactId) {
		return;
	}
	const auto contact = App::userLoaded(contactId);
	const auto canShare = contact ? contact->canShareThisContact() : false;

	const auto i = _contactItems.find(contactId);
	if (i != _contactItems.end()) {
		auto &items = i->second;
		if (items.remove(item) && items.empty()) {
			_contactItems.erase(i);
		}
	}

	if (contact && canShare != contact->canShareThisContact()) {
		Notify::peerUpdatedDelayed(
			contact,
			Notify::PeerUpdate::Flag::UserCanShareContact);
	}
}

void Session::registerAutoplayAnimation(
		not_null<::Media::Clip::Reader*> reader,
		not_null<ViewElement*> view) {
	_autoplayAnimations.emplace(reader, view);
}

void Session::unregisterAutoplayAnimation(
		not_null<::Media::Clip::Reader*> reader) {
	_autoplayAnimations.remove(reader);
}

void Session::stopAutoplayAnimations() {
	for (const auto [reader, view] : base::take(_autoplayAnimations)) {
		if (const auto media = view->media()) {
			media->stopAnimation();
		}
	}
}

HistoryItem *Session::findWebPageItem(not_null<WebPageData*> page) const {
	const auto i = _webpageItems.find(page);
	if (i != _webpageItems.end()) {
		for (const auto item : i->second) {
			if (IsServerMsgId(item->id)) {
				return item;
			}
		}
	}
	return nullptr;
}

QString Session::findContactPhone(not_null<UserData*> contact) const {
	const auto result = contact->phone();
	return result.isEmpty()
		? findContactPhone(contact->bareId())
		: App::formatPhone(result);
}

QString Session::findContactPhone(UserId contactId) const {
	const auto i = _contactItems.find(contactId);
	if (i != _contactItems.end()) {
		if (const auto media = (*begin(i->second))->media()) {
			if (const auto contact = media->sharedContact()) {
				return contact->phoneNumber;
			}
		}
	}
	return QString();
}

void Session::notifyWebPageUpdateDelayed(not_null<WebPageData*> page) {
	const auto invoke = _webpagesUpdated.empty() && _gamesUpdated.empty();
	_webpagesUpdated.insert(page);
	if (invoke) {
		crl::on_main(_session, [=] { sendWebPageGameNotifications(); });
	}
}

void Session::notifyGameUpdateDelayed(not_null<GameData*> game) {
	const auto invoke = _webpagesUpdated.empty() && _gamesUpdated.empty();
	_gamesUpdated.insert(game);
	if (invoke) {
		crl::on_main(_session, [=] { sendWebPageGameNotifications(); });
	}
}

void Session::sendWebPageGameNotifications() {
	for (const auto page : base::take(_webpagesUpdated)) {
		const auto i = _webpageViews.find(page);
		if (i != _webpageViews.end()) {
			for (const auto view : i->second) {
				requestViewResize(view);
			}
		}
	}
	for (const auto game : base::take(_gamesUpdated)) {
		if (const auto i = _gameViews.find(game); i != _gameViews.end()) {
			for (const auto view : i->second) {
				requestViewResize(view);
			}
		}
	}
}

void Session::registerItemView(not_null<ViewElement*> view) {
	_views[view->data()].push_back(view);
}

void Session::unregisterItemView(not_null<ViewElement*> view) {
	const auto i = _views.find(view->data());
	if (i != end(_views)) {
		auto &list = i->second;
		list.erase(ranges::remove(list, view), end(list));
		if (list.empty()) {
			_views.erase(i);
		}
	}
	if (App::hoveredItem() == view) {
		App::hoveredItem(nullptr);
	}
	if (App::pressedItem() == view) {
		App::pressedItem(nullptr);
	}
	if (App::hoveredLinkItem() == view) {
		App::hoveredLinkItem(nullptr);
	}
	if (App::pressedLinkItem() == view) {
		App::pressedLinkItem(nullptr);
	}
	if (App::mousedItem() == view) {
		App::mousedItem(nullptr);
	}
}

not_null<Feed*> Session::feed(FeedId id) {
	if (const auto result = feedLoaded(id)) {
		return result;
	}
	const auto [it, ok] = _feeds.emplace(
		id,
		std::make_unique<Feed>(id, this));
	return it->second.get();
}

Feed *Session::feedLoaded(FeedId id) {
	const auto it = _feeds.find(id);
	return (it == end(_feeds)) ? nullptr : it->second.get();
}

void Session::setDefaultFeedId(FeedId id) {
	_defaultFeedId = id;
}

FeedId Session::defaultFeedId() const {
	return _defaultFeedId.current();
}

rpl::producer<FeedId> Session::defaultFeedIdValue() const {
	return _defaultFeedId.value();
}

void Session::forgetMedia() {
	for (const auto &[id, photo] : _photos) {
		photo->forget();
	}
	for (const auto &[id, document] : _documents) {
		document->forget();
	}
}

void Session::setMimeForwardIds(MessageIdsList &&list) {
	_mimeForwardIds = std::move(list);
}

MessageIdsList Session::takeMimeForwardIds() {
	return std::move(_mimeForwardIds);
}

} // namespace Data
