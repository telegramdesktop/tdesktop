/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_message_reactions.h"

#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "main/main_account.h"
#include "main/main_app_config.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "data/data_histories.h"
#include "data/data_changes.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "lottie/lottie_icon.h"
#include "base/timer_rpl.h"
#include "apiwrap.h"
#include "styles/style_chat.h"

namespace Data {
namespace {

constexpr auto kRefreshFullListEach = 60 * 60 * crl::time(1000);
constexpr auto kPollEach = 20 * crl::time(1000);
constexpr auto kSizeForDownscale = 64;

} // namespace

Reactions::Reactions(not_null<Session*> owner)
: _owner(owner)
, _repaintTimer([=] { repaintCollected(); }) {
	refresh();

	base::timer_each(
		kRefreshFullListEach
	) | rpl::start_with_next([=] {
		refresh();
	}, _lifetime);

	_owner->session().changes().messageUpdates(
		MessageUpdate::Flag::Destroyed
	) | rpl::start_with_next([=](const MessageUpdate &update) {
		const auto item = update.item;
		_pollingItems.remove(item);
		_pollItems.remove(item);
		_repaintItems.remove(item);
	}, _lifetime);

	const auto appConfig = &_owner->session().account().appConfig();
	appConfig->value(
	) | rpl::start_with_next([=] {
		const auto favorite = appConfig->get<QString>(
			u"reactions_default"_q,
			QString::fromUtf8("\xf0\x9f\x91\x8d"));
		if (_favorite != favorite && !_saveFaveRequestId) {
			_favorite = favorite;
			_updated.fire({});
		}
	}, _lifetime);
}

Reactions::~Reactions() = default;

void Reactions::refresh() {
	request();
}

const std::vector<Reaction> &Reactions::list(Type type) const {
	switch (type) {
	case Type::Active: return _active;
	case Type::All: return _available;
	}
	Unexpected("Type in Reactions::list.");
}

QString Reactions::favorite() const {
	return _favorite;
}

void Reactions::setFavorite(const QString &emoji) {
	const auto api = &_owner->session().api();
	if (_saveFaveRequestId) {
		api->request(_saveFaveRequestId).cancel();
	}
	_saveFaveRequestId = api->request(MTPmessages_SetDefaultReaction(
		MTP_string(emoji)
	)).done([=] {
		_saveFaveRequestId = 0;
	}).fail([=] {
		_saveFaveRequestId = 0;
	}).send();

	if (_favorite != emoji) {
		_favorite = emoji;
		_updated.fire({});
	}
}

rpl::producer<> Reactions::updates() const {
	return _updated.events();
}

void Reactions::preloadImageFor(const QString &emoji) {
	if (_images.contains(emoji)) {
		return;
	}
	auto &set = _images.emplace(emoji).first->second;
	const auto i = ranges::find(_available, emoji, &Reaction::emoji);
	const auto document = (i == end(_available))
		? nullptr
		: i->centerIcon
		? i->centerIcon
		: i->appearAnimation.get();
	if (document) {
		loadImage(set, document, !i->centerIcon);
	} else if (!_waitingForList) {
		_waitingForList = true;
		refresh();
	}
}

void Reactions::preloadAnimationsFor(const QString &emoji) {
	const auto i = ranges::find(_available, emoji, &Reaction::emoji);
	if (i == end(_available)) {
		return;
	}

	const auto preload = [&](DocumentData *document) {
		const auto view = document
			? document->activeMediaView()
			: nullptr;
		if (view) {
			view->checkStickerLarge();
		}
	};
	preload(i->centerIcon);
	preload(i->aroundAnimation);
}

QImage Reactions::resolveImageFor(
		const QString &emoji,
		ImageSize size) {
	const auto i = _images.find(emoji);
	if (i == end(_images)) {
		preloadImageFor(emoji);
	}
	auto &set = (i != end(_images)) ? i->second : _images[emoji];
	const auto resolve = [&](QImage &image, int size) {
		const auto factor = style::DevicePixelRatio();
		const auto frameSize = set.fromAppearAnimation
			? (size / 2)
			: size;
		image = set.icon->frame().scaled(
			frameSize * factor,
			frameSize * factor,
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation);
		if (set.fromAppearAnimation) {
			auto result = QImage(
				size * factor,
				size * factor,
				QImage::Format_ARGB32_Premultiplied);
			result.fill(Qt::transparent);

			auto p = QPainter(&result);
			p.drawImage(
				(size - frameSize) * factor / 2,
				(size - frameSize) * factor / 2,
				image);
			p.end();

			std::swap(result, image);
		}
		image.setDevicePixelRatio(factor);
	};
	if (set.bottomInfo.isNull() && set.icon) {
		resolve(set.bottomInfo, st::reactionInfoImage);
		resolve(set.inlineList, st::reactionInlineImage);
		crl::async([icon = std::move(set.icon)]{});
	}
	switch (size) {
	case ImageSize::BottomInfo: return set.bottomInfo;
	case ImageSize::InlineList: return set.inlineList;
	}
	Unexpected("ImageSize in Reactions::resolveImageFor.");
}

void Reactions::resolveImages() {
	for (auto &[emoji, set] : _images) {
		if (!set.bottomInfo.isNull() || set.icon || set.media) {
			continue;
		}
		const auto i = ranges::find(_available, emoji, &Reaction::emoji);
		const auto document = (i == end(_available))
			? nullptr
			: i->centerIcon
			? i->centerIcon
			: i->appearAnimation.get();
		if (document) {
			loadImage(set, document, !i->centerIcon);
		} else {
			LOG(("API Error: Reaction for emoji '%1' not found!"
				).arg(emoji));
		}
	}
}

void Reactions::loadImage(
		ImageSet &set,
		not_null<DocumentData*> document,
		bool fromAppearAnimation) {
	if (!set.bottomInfo.isNull() || set.icon) {
		return;
	} else if (!set.media) {
		set.fromAppearAnimation = fromAppearAnimation;
		set.media = document->createMediaView();
		set.media->checkStickerLarge();
	}
	if (set.media->loaded()) {
		setLottie(set);
	} else if (!_imagesLoadLifetime) {
		document->session().downloaderTaskFinished(
		) | rpl::start_with_next([=] {
			downloadTaskFinished();
		}, _imagesLoadLifetime);
	}
}

void Reactions::setLottie(ImageSet &set) {
	const auto size = style::ConvertScale(kSizeForDownscale);
	set.icon = Lottie::MakeIcon({
		.path = set.media->owner()->filepath(true),
		.json = set.media->bytes(),
		.sizeOverride = QSize(size, size),
		.frame = -1,
	});
	set.media = nullptr;
}

void Reactions::downloadTaskFinished() {
	auto hasOne = false;
	for (auto &[emoji, set] : _images) {
		if (!set.media) {
			continue;
		} else if (set.media->loaded()) {
			setLottie(set);
		} else {
			hasOne = true;
		}
	}
	if (!hasOne) {
		_imagesLoadLifetime.destroy();
	}
}

base::flat_set<QString> Reactions::ParseAllowed(
		const MTPVector<MTPstring> *list) {
	if (!list) {
		return {};
	}
	const auto parsed = ranges::views::all(
		list->v
	) | ranges::views::transform([](const MTPstring &string) {
		return qs(string);
	}) | ranges::to_vector;
	return { begin(parsed), end(parsed) };
}

void Reactions::request() {
	auto &api = _owner->session().api();
	if (_requestId) {
		return;
	}
	_requestId = api.request(MTPmessages_GetAvailableReactions(
		MTP_int(_hash)
	)).done([=](const MTPmessages_AvailableReactions &result) {
		_requestId = 0;
		result.match([&](const MTPDmessages_availableReactions &data) {
			updateFromData(data);
		}, [&](const MTPDmessages_availableReactionsNotModified &) {
		});
	}).fail([=] {
		_requestId = 0;
		_hash = 0;
	}).send();
}

void Reactions::updateFromData(const MTPDmessages_availableReactions &data) {
	_hash = data.vhash().v;

	const auto &list = data.vreactions().v;
	const auto oldCache = base::take(_iconsCache);
	const auto toCache = [&](DocumentData *document) {
		if (document) {
			_iconsCache.emplace(document, document->createMediaView());
		}
	};
	_active.clear();
	_available.clear();
	_active.reserve(list.size());
	_available.reserve(list.size());
	_iconsCache.reserve(list.size() * 4);
	for (const auto &reaction : list) {
		if (const auto parsed = parse(reaction)) {
			_available.push_back(*parsed);
			if (parsed->active) {
				_active.push_back(*parsed);
				toCache(parsed->appearAnimation);
				toCache(parsed->selectAnimation);
				toCache(parsed->centerIcon);
				toCache(parsed->aroundAnimation);
			}
		}
	}
	if (_waitingForList) {
		_waitingForList = false;
		resolveImages();
	}
	_updated.fire({});
}

std::optional<Reaction> Reactions::parse(const MTPAvailableReaction &entry) {
	return entry.match([&](const MTPDavailableReaction &data) {
		const auto emoji = qs(data.vreaction());
		const auto known = (Ui::Emoji::Find(emoji) != nullptr);
		if (!known) {
			LOG(("API Error: Unknown emoji in reactions: %1").arg(emoji));
		}
		const auto selectAnimation = _owner->processDocument(
			data.vselect_animation());
		return known
			? std::make_optional(Reaction{
				.emoji = emoji,
				.title = qs(data.vtitle()),
				.staticIcon = _owner->processDocument(data.vstatic_icon()),
				.appearAnimation = _owner->processDocument(
					data.vappear_animation()),
				.selectAnimation = selectAnimation,
				//.activateAnimation = _owner->processDocument(
				//	data.vactivate_animation()),
				//.activateEffects = _owner->processDocument(
				//	data.veffect_animation()),
				.centerIcon = (data.vcenter_icon()
					? _owner->processDocument(*data.vcenter_icon()).get()
					: nullptr),
				.aroundAnimation = (data.varound_animation()
					? _owner->processDocument(
						*data.varound_animation()).get()
					: nullptr),
				.active = !data.is_inactive(),
			})
			: std::nullopt;
	});
}

void Reactions::send(not_null<HistoryItem*> item, const QString &chosen) {
	const auto id = item->fullId();
	auto &api = _owner->session().api();
	auto i = _sentRequests.find(id);
	if (i != end(_sentRequests)) {
		api.request(i->second).cancel();
	} else {
		i = _sentRequests.emplace(id).first;
	}
	const auto flags = chosen.isEmpty()
		? MTPmessages_SendReaction::Flag(0)
		: MTPmessages_SendReaction::Flag::f_reaction;
	i->second = api.request(MTPmessages_SendReaction(
		MTP_flags(flags),
		item->history()->peer->input,
		MTP_int(id.msg),
		MTP_string(chosen)
	)).done([=](const MTPUpdates &result) {
		_sentRequests.remove(id);
		_owner->session().api().applyUpdates(result);
	}).fail([=](const MTP::Error &error) {
		_sentRequests.remove(id);
	}).send();
}

void Reactions::poll(not_null<HistoryItem*> item, crl::time now) {
	// Group them by one second.
	const auto last = item->lastReactionsRefreshTime();
	const auto grouped = ((last + 999) / 1000) * 1000;
	if (!grouped || item->history()->peer->isUser()) {
		// First reaction always edits message.
		return;
	} else if (const auto left = grouped + kPollEach - now; left > 0) {
		if (!_repaintItems.contains(item)) {
			_repaintItems.emplace(item, grouped + kPollEach);
			if (!_repaintTimer.isActive()
				|| _repaintTimer.remainingTime() > left) {
				_repaintTimer.callOnce(left);
			}
		}
	} else if (!_pollingItems.contains(item)) {
		if (_pollItems.empty() && !_pollRequestId) {
			crl::on_main(&_owner->session(), [=] {
				pollCollected();
			});
		}
		_pollItems.emplace(item);
	}
}

void Reactions::updateAllInHistory(not_null<PeerData*> peer, bool enabled) {
	if (const auto history = _owner->historyLoaded(peer)) {
		history->reactionsEnabledChanged(enabled);
	}
}

void Reactions::repaintCollected() {
	const auto now = crl::now();
	auto closest = crl::time();
	for (auto i = begin(_repaintItems); i != end(_repaintItems);) {
		if (i->second <= now) {
			_owner->requestItemRepaint(i->first);
			i = _repaintItems.erase(i);
		} else {
			if (!closest || i->second < closest) {
				closest = i->second;
			}
			++i;
		}
	}
	if (closest) {
		_repaintTimer.callOnce(closest - now);
	}
}

void Reactions::pollCollected() {
	auto toRequest = base::flat_map<not_null<PeerData*>, QVector<MTPint>>();
	_pollingItems = std::move(_pollItems);
	for (const auto &item : _pollingItems) {
		toRequest[item->history()->peer].push_back(MTP_int(item->id));
	}
	auto &api = _owner->session().api();
	for (const auto &[peer, ids] : toRequest) {
		const auto finalize = [=] {
			const auto now = crl::now();
			for (const auto &item : base::take(_pollingItems)) {
				const auto last = item->lastReactionsRefreshTime();
				if (last && last + kPollEach <= now) {
					item->updateReactions(nullptr);
				}
			}
			_pollRequestId = 0;
			if (!_pollItems.empty()) {
				crl::on_main(&_owner->session(), [=] {
					pollCollected();
				});
			}
		};
		_pollRequestId = api.request(MTPmessages_GetMessagesReactions(
			peer->input,
			MTP_vector<MTPint>(ids)
		)).done([=](const MTPUpdates &result) {
			_owner->session().api().applyUpdates(result);
			finalize();
		}).fail([=] {
			finalize();
		}).send();
	}
}

bool Reactions::sending(not_null<HistoryItem*> item) const {
	return _sentRequests.contains(item->fullId());
}

bool Reactions::HasUnread(const MTPMessageReactions &data) {
	return data.match([&](const MTPDmessageReactions &data) {
		if (const auto &recent = data.vrecent_reactions()) {
			for (const auto &one : recent->v) {
				if (one.match([&](const MTPDmessagePeerReaction &data) {
					return data.is_unread();
				})) {
					return true;
				}
			}
		}
		return false;
	});
}

void Reactions::CheckUnknownForUnread(
		not_null<Session*> owner,
		const MTPMessage &message) {
	message.match([&](const MTPDmessage &data) {
		if (data.vreactions() && HasUnread(*data.vreactions())) {
			const auto peerId = peerFromMTP(data.vpeer_id());
			if (const auto history = owner->historyLoaded(peerId)) {
				owner->histories().requestDialogEntry(history);
			}
		}
	}, [](const auto &) {
	});
}

MessageReactions::MessageReactions(not_null<HistoryItem*> item)
: _item(item) {
}

void MessageReactions::add(const QString &reaction) {
	if (_chosen == reaction) {
		return;
	}
	const auto history = _item->history();
	const auto self = history->session().user();
	if (!_chosen.isEmpty()) {
		const auto i = _list.find(_chosen);
		Assert(i != end(_list));
		--i->second;
		const auto removed = !i->second;
		if (removed) {
			_list.erase(i);
		}
		const auto j = _recent.find(_chosen);
		if (j != end(_recent)) {
			j->second.erase(
				ranges::remove(j->second, self, &RecentReaction::peer),
				end(j->second));
			if (j->second.empty() || removed) {
				_recent.erase(j);
			}
		}
	}
	_chosen = reaction;
	if (!reaction.isEmpty()) {
		if (_item->canViewReactions()) {
			auto &list = _recent[reaction];
			list.insert(begin(list), RecentReaction{ self });
		}
		++_list[reaction];
	}
	auto &owner = history->owner();
	owner.reactions().send(_item, _chosen);
	owner.notifyItemDataChange(_item);
}

void MessageReactions::remove() {
	add(QString());
}

bool MessageReactions::checkIfChanged(
		const QVector<MTPReactionCount> &list,
		const QVector<MTPMessagePeerReaction> &recent) const {
	auto &owner = _item->history()->owner();
	if (owner.reactions().sending(_item)) {
		// We'll apply non-stale data from the request response.
		return false;
	}
	auto existing = base::flat_set<QString>();
	for (const auto &count : list) {
		const auto changed = count.match([&](const MTPDreactionCount &data) {
			const auto reaction = qs(data.vreaction());
			const auto nowCount = data.vcount().v;
			const auto i = _list.find(reaction);
			const auto wasCount = (i != end(_list)) ? i->second : 0;
			if (wasCount != nowCount) {
				return true;
			}
			existing.emplace(reaction);
			return false;
		});
		if (changed) {
			return true;
		}
	}
	for (const auto &[reaction, count] : _list) {
		if (!existing.contains(reaction)) {
			return true;
		}
	}
	auto parsed = base::flat_map<QString, std::vector<RecentReaction>>();
	for (const auto &reaction : recent) {
		reaction.match([&](const MTPDmessagePeerReaction &data) {
			const auto emoji = qs(data.vreaction());
			if (_list.contains(emoji)) {
				parsed[emoji].push_back(RecentReaction{
					.peer = owner.peer(peerFromMTP(data.vpeer_id())),
					.unread = data.is_unread(),
					.big = data.is_big(),
				});
			}
		});
	}
	return !ranges::equal(_recent, parsed, [](
			const auto &a,
			const auto &b) {
		return ranges::equal(a.second, b.second, [](
				const RecentReaction &a,
				const RecentReaction &b) {
			return (a.peer == b.peer) && (a.big == b.big);
		});
	});
}

bool MessageReactions::change(
		const QVector<MTPReactionCount> &list,
		const QVector<MTPMessagePeerReaction> &recent,
		bool ignoreChosen) {
	auto &owner = _item->history()->owner();
	if (owner.reactions().sending(_item)) {
		// We'll apply non-stale data from the request response.
		return false;
	}
	auto changed = false;
	auto existing = base::flat_set<QString>();
	for (const auto &count : list) {
		count.match([&](const MTPDreactionCount &data) {
			const auto reaction = qs(data.vreaction());
			if (!ignoreChosen) {
				if (data.is_chosen() && _chosen != reaction) {
					_chosen = reaction;
					changed = true;
				} else if (!data.is_chosen() && _chosen == reaction) {
					_chosen = QString();
					changed = true;
				}
			}
			const auto nowCount = data.vcount().v;
			auto &wasCount = _list[reaction];
			if (wasCount != nowCount) {
				wasCount = nowCount;
				changed = true;
			}
			existing.emplace(reaction);
		});
	}
	if (_list.size() != existing.size()) {
		changed = true;
		for (auto i = begin(_list); i != end(_list);) {
			if (!existing.contains(i->first)) {
				i = _list.erase(i);
			} else {
				++i;
			}
		}
		if (!_chosen.isEmpty() && !_list.contains(_chosen)) {
			_chosen = QString();
		}
	}
	auto parsed = base::flat_map<QString, std::vector<RecentReaction>>();
	for (const auto &reaction : recent) {
		reaction.match([&](const MTPDmessagePeerReaction &data) {
			const auto emoji = qs(data.vreaction());
			if (_list.contains(emoji)) {
				parsed[emoji].push_back(RecentReaction{
					.peer = owner.peer(peerFromMTP(data.vpeer_id())),
					.unread = data.is_unread(),
					.big = data.is_big(),
				});
			}
		});
	}
	if (_recent != parsed) {
		_recent = std::move(parsed);
		changed = true;
	}
	return changed;
}

const base::flat_map<QString, int> &MessageReactions::list() const {
	return _list;
}

auto MessageReactions::recent() const
-> const base::flat_map<QString, std::vector<RecentReaction>> & {
	return _recent;
}

bool MessageReactions::empty() const {
	return _list.empty();
}

bool MessageReactions::hasUnread() const {
	for (auto &[emoji, list] : _recent) {
		if (ranges::contains(list, true, &RecentReaction::unread)) {
			return true;
		}
	}
	return false;
}

void MessageReactions::markRead() {
	for (auto &[emoji, list] : _recent) {
		for (auto &reaction : list) {
			reaction.unread = false;
		}
	}
}

QString MessageReactions::chosen() const {
	return _chosen;
}

} // namespace Data
