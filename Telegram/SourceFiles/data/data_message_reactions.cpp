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
#include "data/data_session.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_changes.h"
#include "base/timer_rpl.h"
#include "apiwrap.h"
#include "styles/style_chat.h"

namespace Data {
namespace {

constexpr auto kRefreshFullListEach = 60 * 60 * crl::time(1000);
constexpr auto kPollEach = 20 * crl::time(1000);

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
}

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

std::vector<Reaction> Reactions::list(not_null<PeerData*> peer) const {
	if (const auto chat = peer->asChat()) {
		return filtered(chat->allowedReactions());
	} else if (const auto channel = peer->asChannel()) {
		return filtered(channel->allowedReactions());
	} else {
		return list(Type::Active);
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
	const auto document = (i != end(_available))
		? i->staticIcon.get()
		: nullptr;
	if (document) {
		loadImage(set, document);
	} else if (!_waitingForList) {
		refresh();
	}
}

QImage Reactions::resolveImageFor(
		const QString &emoji,
		ImageSize size) {
	const auto i = _images.find(emoji);
	if (i == end(_images)) {
		preloadImageFor(emoji);
	}
	auto &set = (i != end(_images)) ? i->second : _images[emoji];
	switch (size) {
	case ImageSize::BottomInfo: return set.bottomInfo;
	case ImageSize::InlineList: return set.inlineList;
	}
	Unexpected("ImageSize in Reactions::resolveImageFor.");
}

void Reactions::resolveImages() {
	for (auto &[emoji, set] : _images) {
		if (!set.bottomInfo.isNull() || set.media) {
			continue;
		}
		const auto i = ranges::find(_available, emoji, &Reaction::emoji);
		const auto document = (i != end(_available))
			? i->staticIcon.get()
			: nullptr;
		if (document) {
			loadImage(set, document);
		} else {
			LOG(("API Error: Reaction for emoji '%1' not found!"
				).arg(emoji));
		}
	}
}

void Reactions::loadImage(
		ImageSet &set,
		not_null<DocumentData*> document) {
	if (!set.bottomInfo.isNull()) {
		return;
	} else if (!set.media) {
		set.media = document->createMediaView();
	}
	if (const auto image = set.media->getStickerLarge()) {
		setImage(set, image->original());
	} else if (!_imagesLoadLifetime) {
		document->session().downloaderTaskFinished(
		) | rpl::start_with_next([=] {
			downloadTaskFinished();
		}, _imagesLoadLifetime);
	}
}

void Reactions::setImage(ImageSet &set, QImage large) {
	set.media = nullptr;
	const auto scale = [&](int size) {
		const auto factor = style::DevicePixelRatio();
		return Images::prepare(
			large,
			size * factor,
			size * factor,
			Images::Option::Smooth,
			size,
			size);
	};
	set.bottomInfo = scale(st::reactionInfoSize);
	set.inlineList = scale(st::reactionBottomSize);
}

void Reactions::downloadTaskFinished() {
	auto hasOne = false;
	for (auto &[emoji, set] : _images) {
		if (!set.media) {
			continue;
		} else if (const auto image = set.media->getStickerLarge()) {
			setImage(set, image->original());
		} else {
			hasOne = true;
		}
	}
	if (!hasOne) {
		_imagesLoadLifetime.destroy();
	}
}

std::vector<Reaction> Reactions::Filtered(
		const std::vector<Reaction> &reactions,
		const std::vector<QString> &emoji) {
	auto result = std::vector<Reaction>();
	result.reserve(emoji.size());
	for (const auto &single : emoji) {
		const auto i = ranges::find(reactions, single, &Reaction::emoji);
		if (i != end(reactions)) {
			result.push_back(*i);
		}
	}
	return result;
}

std::vector<Reaction> Reactions::filtered(
		const std::vector<QString> &emoji) const {
	return Filtered(list(Type::Active), emoji);
}

std::vector<QString> Reactions::ParseAllowed(
		const MTPVector<MTPstring> *list) {
	if (!list) {
		return {};
	}
	return list->v | ranges::view::transform([](const MTPstring &string) {
		return qs(string);
	}) | ranges::to_vector;
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
			_hash = data.vhash().v;

			const auto &list = data.vreactions().v;
			_active.clear();
			_available.clear();
			_active.reserve(list.size());
			_available.reserve(list.size());
			for (const auto &reaction : list) {
				if (const auto parsed = parse(reaction)) {
					_available.push_back(*parsed);
					if (parsed->active) {
						_active.push_back(*parsed);
					}
				}
			}
			if (_waitingForList) {
				_waitingForList = false;
				resolveImages();
			}
			_updated.fire({});
		}, [&](const MTPDmessages_availableReactionsNotModified &) {
		});
	}).fail([=] {
		_requestId = 0;
		_hash = 0;
	}).send();
}

std::optional<Reaction> Reactions::parse(const MTPAvailableReaction &entry) {
	return entry.match([&](const MTPDavailableReaction &data) {
		const auto emoji = qs(data.vreaction());
		const auto known = (Ui::Emoji::Find(emoji) != nullptr);
		if (!known) {
			LOG(("API Error: Unknown emoji in reactions: %1").arg(emoji));
		}
		return known
			? std::make_optional(Reaction{
				.emoji = emoji,
				.title = qs(data.vtitle()),
				.staticIcon = _owner->processDocument(data.vstatic_icon()),
				.appearAnimation = _owner->processDocument(
					data.vappear_animation()),
				.selectAnimation = _owner->processDocument(
					data.vselect_animation()),
				.activateAnimation = _owner->processDocument(
					data.vactivate_animation()),
				.activateEffects = _owner->processDocument(
					data.veffect_animation()),
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
	auto closest = 0;
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

MessageReactions::MessageReactions(not_null<HistoryItem*> item)
: _item(item) {
}

void MessageReactions::add(const QString &reaction) {
	if (_chosen == reaction) {
		return;
	}
	if (!_chosen.isEmpty()) {
		const auto i = _list.find(_chosen);
		Assert(i != end(_list));
		--i->second;
		if (!i->second) {
			_list.erase(i);
		}
	}
	_chosen = reaction;
	if (!reaction.isEmpty()) {
		++_list[reaction];
	}
	auto &owner = _item->history()->owner();
	owner.reactions().send(_item, _chosen);
	owner.notifyItemDataChange(_item);
}

void MessageReactions::remove() {
	add(QString());
}

void MessageReactions::set(
		const QVector<MTPReactionCount> &list,
		bool ignoreChosen) {
	if (_item->history()->owner().reactions().sending(_item)) {
		// We'll apply non-stale data from the request response.
		return;
	}
	auto changed = false;
	auto existing = base::flat_set<QString>();
	for (const auto &count : list) {
		count.match([&](const MTPDreactionCount &data) {
			const auto reaction = qs(data.vreaction());
			if (data.is_chosen() && !ignoreChosen) {
				if (_chosen != reaction) {
					_chosen = reaction;
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
	if (changed) {
		_item->history()->owner().notifyItemDataChange(_item);
	}
}

const base::flat_map<QString, int> &MessageReactions::list() const {
	return _list;
}

bool MessageReactions::empty() const {
	return _list.empty();
}

QString MessageReactions::chosen() const {
	return _chosen;
}

} // namespace Data
