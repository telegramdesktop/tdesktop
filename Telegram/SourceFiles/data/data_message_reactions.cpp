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
#include "base/timer_rpl.h"
#include "apiwrap.h"
#include "styles/style_chat.h"

namespace Data {
namespace {

constexpr auto kRefreshEach = 60 * 60 * crl::time(1000);

} // namespace

Reactions::Reactions(not_null<Session*> owner) : _owner(owner) {
	refresh();

	base::timer_each(
		kRefreshEach
	) | rpl::start_with_next([=] {
		refresh();
	}, _lifetime);
}

void Reactions::refresh() {
	request();
}

const std::vector<Reaction> &Reactions::list() const {
	return _available;
}

std::vector<Reaction> Reactions::list(not_null<PeerData*> peer) const {
	if (const auto chat = peer->asChat()) {
		return filtered(chat->allowedReactions());
	} else if (const auto channel = peer->asChannel()) {
		return filtered(channel->allowedReactions());
	} else {
		return list();
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
		const auto document = (i != end(list()))
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
	return Filtered(list(), emoji);
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
	_requestId = api.request(MTPmessages_GetAvailableReactions(
		MTP_int(_hash)
	)).done([=](const MTPmessages_AvailableReactions &result) {
		_requestId = 0;
		result.match([&](const MTPDmessages_availableReactions &data) {
			_hash = data.vhash().v;

			const auto &list = data.vreactions().v;
			_available.clear();
			_available.reserve(data.vreactions().v.size());
			for (const auto &reaction : list) {
				if (const auto parsed = parse(reaction)) {
					_available.push_back(*parsed);
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
				.selectAnimation = _owner->processDocument(
					data.vselect_animation()),
				.activateAnimation = _owner->processDocument(
					data.vactivate_animation()),
				.activateEffects = _owner->processDocument(
					data.veffect_animation()),
			})
			: std::nullopt;
	});
}

std::vector<QString> MessageReactions::SuggestList() {
	constexpr auto utf = [](const char *utf) {
		return QString::fromUtf8(utf);
	};
	return {
		utf("\xE2\x9D\xA4\xEF\xB8\x8F"), // :heart:
		utf("\xF0\x9F\x91\x8D"), // :like:
		utf("\xF0\x9F\x98\x82"), // :joy:
		utf("\xF0\x9F\x98\xB3"), // :flushed:
		utf("\xF0\x9F\x98\x94"), // :pensive:
	};
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
	sendRequest();
	_item->history()->owner().requestItemResize(_item);
}

void MessageReactions::set(
		const QVector<MTPReactionCount> &list,
		bool ignoreChosen) {
	if (_requestId) {
		// We'll apply non-stale data from the request response.
		return;
	}
	auto existing = base::flat_set<QString>();
	for (const auto &count : list) {
		count.match([&](const MTPDreactionCount &data) {
			const auto reaction = qs(data.vreaction());
			if (data.is_chosen() && !ignoreChosen) {
				_chosen = reaction;
			}
			_list[reaction] = data.vcount().v;
			existing.emplace(reaction);
		});
	}
	if (_list.size() != existing.size()) {
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
	_item->history()->owner().requestItemResize(_item);
}

const base::flat_map<QString, int> &MessageReactions::list() const {
	return _list;
}

QString MessageReactions::chosen() const {
	return _chosen;
}

void MessageReactions::sendRequest() {
	const auto api = &_item->history()->session().api();
	if (_requestId) {
		api->request(_requestId).cancel();
	}
	const auto flags = _chosen.isEmpty()
		? MTPmessages_SendReaction::Flag(0)
		: MTPmessages_SendReaction::Flag::f_reaction;
	_requestId = api->request(MTPmessages_SendReaction(
		MTP_flags(flags),
		_item->history()->peer->input,
		MTP_int(_item->id),
		MTP_string(_chosen)
	)).done([=](const MTPUpdates &result) {
		_requestId = 0;
		api->applyUpdates(result);
	}).fail([=](const MTP::Error &error) {
		_requestId = 0;
	}).send();
}

} // namespace Data
