/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_message_reactions.h"

#include "api/api_global_privacy.h"
#include "chat_helpers/stickers_lottie.h"
#include "core/application.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "main/main_session.h"
#include "main/main_app_config.h"
#include "main/session/send_as_peers.h"
#include "data/components/credits.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "data/data_histories.h"
#include "data/data_changes.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_peer_values.h"
#include "data/data_saved_sublist.h"
#include "data/stickers/data_custom_emoji.h"
#include "storage/localimageloader.h"
#include "ui/image/image_location_factory.h"
#include "ui/animated_icon.h"
#include "mtproto/mtproto_config.h"
#include "base/timer_rpl.h"
#include "base/call_delayed.h"
#include "base/unixtime.h"
#include "apiwrap.h"
#include "styles/style_chat.h"

#include "base/random.h"

namespace Data {
namespace {

constexpr auto kRefreshFullListEach = 60 * 60 * crl::time(1000);
constexpr auto kPollEach = 20 * crl::time(1000);
constexpr auto kSizeForDownscale = 64;
constexpr auto kRecentRequestTimeout = 10 * crl::time(1000);
constexpr auto kRecentReactionsLimit = 40;
constexpr auto kMyTagsRequestTimeout = crl::time(1000);
constexpr auto kTopRequestDelay = 60 * crl::time(1000);
constexpr auto kTopReactionsLimit = 14;
constexpr auto kPaidAccumulatePeriod = 5 * crl::time(1000) + 500;

[[nodiscard]] QString ReactionIdToLog(const ReactionId &id) {
	if (const auto custom = id.custom()) {
		return "custom:" + QString::number(custom);
	}
	return id.emoji();
}

[[nodiscard]] std::vector<ReactionId> ListFromMTP(
		const MTPDmessages_reactions &data) {
	const auto &list = data.vreactions().v;
	auto result = std::vector<ReactionId>();
	result.reserve(list.size());
	for (const auto &reaction : list) {
		const auto id = ReactionFromMTP(reaction);
		if (id.empty()) {
			LOG(("API Error: reactionEmpty in messages.reactions."));
		} else {
			result.push_back(id);
		}
	}
	return result;
}

[[nodiscard]] std::vector<MyTagInfo> ListFromMTP(
		const MTPDmessages_savedReactionTags &data) {
	const auto &list = data.vtags().v;
	auto result = std::vector<MyTagInfo>();
	result.reserve(list.size());
	for (const auto &reaction : list) {
		const auto &data = reaction.data();
		const auto id = ReactionFromMTP(data.vreaction());
		if (id.empty()) {
			LOG(("API Error: reactionEmpty in messages.reactions."));
		} else {
			result.push_back({
				.id = id,
				.title = qs(data.vtitle().value_or_empty()),
				.count = data.vcount().v,
			});
		}
	}
	return result;
}

[[nodiscard]] Reaction CustomReaction(not_null<DocumentData*> document) {
	return Reaction{
		.id = { { document->id } },
		.title = "Custom reaction",
		.appearAnimation = document,
		.selectAnimation = document,
		.centerIcon = document,
		.active = true,
	};
}

[[nodiscard]] int SentReactionsLimit(not_null<HistoryItem*> item) {
	const auto session = &item->history()->session();
	const auto config = &session->appConfig();
	return session->premium()
		? config->get<int>("reactions_user_max_premium", 3)
		: config->get<int>("reactions_user_max_default", 1);
}

[[nodiscard]] bool IsMyRecent(
		const MTPDmessagePeerReaction &data,
		const ReactionId &id,
		not_null<PeerData*> peer,
		const base::flat_map<
			ReactionId,
			std::vector<RecentReaction>> &recent,
		bool min) {
	if (peer->isSelf()) {
		return true;
	} else if (!min) {
		return data.is_my();
	}
	const auto j = recent.find(id);
	if (j == end(recent)) {
		return false;
	}
	const auto k = ranges::find(
		j->second,
		peer,
		&RecentReaction::peer);
	return (k != end(j->second)) && k->my;
}

[[nodiscard]] bool IsMyTop(
		const MTPDmessageReactor &data,
		PeerData *peer,
		const std::vector<MessageReactionsTopPaid> &top,
		bool min) {
	if (peer && peer->isSelf()) {
		return true;
	} else if (!min) {
		return data.is_my();
	}
	const auto i = ranges::find(top, peer, &MessageReactionsTopPaid::peer);
	return (i != end(top)) && i->my;
}

[[nodiscard]] std::optional<bool> MaybeAnonymous(uint32 privacySet, uint32 anonymous) {
	return privacySet ? (anonymous == 1) : std::optional<bool>();
}

} // namespace

PossibleItemReactionsRef LookupPossibleReactions(
		not_null<HistoryItem*> item,
		bool paidInFront) {
	if (!item->canReact()) {
		return {};
	}
	auto result = PossibleItemReactionsRef();
	auto peer = item->history()->peer;
	if (item->isDiscussionPost()) {
		if (const auto forwarded = item->Get<HistoryMessageForwarded>()) {
			if (forwarded->savedFromPeer) {
				peer = forwarded->savedFromPeer;
			}
		}
	}
	const auto session = &peer->session();
	const auto reactions = &session->data().reactions();
	const auto &full = reactions->list(Reactions::Type::Active);
	const auto &top = reactions->list(Reactions::Type::Top);
	const auto &recent = reactions->list(Reactions::Type::Recent);
	const auto &myTags = reactions->list(Reactions::Type::MyTags);
	const auto &tags = reactions->list(Reactions::Type::Tags);
	const auto &all = item->reactions();
	const auto &allowed = PeerAllowedReactions(peer);
	const auto limit = UniqueReactionsLimit(peer);
	const auto premiumPossible = session->premiumPossible();
	const auto limited = (all.size() >= limit) && [&] {
		const auto my = item->chosenReactions();
		if (my.empty()) {
			return true;
		}
		return true; // #TODO reactions
	}();
	auto added = base::flat_set<ReactionId>();
	const auto add = [&](auto predicate) {
		auto &&all = ranges::views::concat(top, recent, full);
		for (const auto &reaction : all) {
			if (predicate(reaction)) {
				if (added.emplace(reaction.id).second) {
					result.recent.push_back(&reaction);
				}
			}
		}
	};
	reactions->clearTemporary();
	if (item->reactionsAreTags()) {
		auto &&all = ranges::views::concat(myTags, tags);
		result.recent.reserve(myTags.size() + tags.size());
		for (const auto &reaction : all) {
			if (premiumPossible
				|| ranges::contains(tags, reaction.id, &Reaction::id)) {
				if (added.emplace(reaction.id).second) {
					result.recent.push_back(&reaction);
				}
			}
		}
		result.customAllowed = premiumPossible;
		result.tags = true;
	} else if (limited) {
		result.recent.reserve((allowed.paidEnabled ? 1 : 0) + all.size());
		add([&](const Reaction &reaction) {
			return ranges::contains(all, reaction.id, &MessageReaction::id);
		});
		for (const auto &reaction : all) {
			const auto id = reaction.id;
			if (added.emplace(id).second) {
				if (const auto temp = reactions->lookupTemporary(id)) {
					result.recent.push_back(temp);
				}
			}
		}
		if (allowed.paidEnabled
			&& !added.contains(Data::ReactionId::Paid())) {
			result.recent.push_back(reactions->lookupPaid());
		}
	} else {
		result.recent.reserve((allowed.paidEnabled ? 1 : 0)
			+ ((allowed.type == AllowedReactionsType::Some)
				? allowed.some.size()
				: full.size()));
		if (allowed.paidEnabled) {
			result.recent.push_back(reactions->lookupPaid());
		}
		add([&](const Reaction &reaction) {
			const auto id = reaction.id;
			if (id.custom() && !premiumPossible) {
				return false;
			} else if ((allowed.type == AllowedReactionsType::Some)
				&& !ranges::contains(allowed.some, id)) {
				return false;
			} else if (id.custom()
				&& allowed.type == AllowedReactionsType::Default) {
				return false;
			}
			return true;
		});
		if (allowed.type == AllowedReactionsType::Some) {
			for (const auto &id : allowed.some) {
				if (!added.contains(id)) {
					if (const auto temp = reactions->lookupTemporary(id)) {
						result.recent.push_back(temp);
					}
				}
			}
		}
		result.customAllowed = (allowed.type == AllowedReactionsType::All)
			&& premiumPossible;

		const auto favoriteId = reactions->favoriteId();
		if (favoriteId.custom()
			&& result.customAllowed
			&& !ranges::contains(result.recent, favoriteId, &Reaction::id)) {
			if (const auto temp = reactions->lookupTemporary(favoriteId)) {
				result.recent.insert(begin(result.recent), temp);
			}
		}
	}
	if (!item->reactionsAreTags()) {
		const auto toFront = [&](Data::ReactionId id) {
			const auto i = ranges::find(result.recent, id, &Reaction::id);
			if (i != end(result.recent) && i != begin(result.recent)) {
				std::rotate(begin(result.recent), i, i + 1);
			}
		};
		toFront(reactions->favoriteId());
		if (paidInFront) {
			toFront(Data::ReactionId::Paid());
		}
	}
	return result;
}

PossibleItemReactions::PossibleItemReactions(
	const PossibleItemReactionsRef &other)
: recent(other.recent | ranges::views::transform([](const auto &value) {
	return *value;
}) | ranges::to_vector)
, stickers(other.stickers | ranges::views::transform([](const auto &value) {
	return *value;
}) | ranges::to_vector)
, customAllowed(other.customAllowed)
, tags(other.tags){
}

Reactions::Reactions(not_null<Session*> owner)
: _owner(owner)
, _topRefreshTimer([=] { refreshTop(); })
, _repaintTimer([=] { repaintCollected(); })
, _sendPaidTimer([=] { sendPaid(); }) {
	refreshDefault();

	_myTags.emplace(nullptr);

	base::timer_each(
		kRefreshFullListEach
	) | rpl::start_with_next([=] {
		refreshDefault();
		requestEffects();
	}, _lifetime);

	_owner->session().changes().messageUpdates(
		MessageUpdate::Flag::Destroyed
	) | rpl::start_with_next([=](const MessageUpdate &update) {
		const auto item = update.item;
		_pollingItems.remove(item);
		_pollItems.remove(item);
		_repaintItems.remove(item);
		_sendPaidItems.remove(item);
		if (const auto i = _sendingPaid.find(item)
			; i != end(_sendingPaid)) {
			_sendingPaid.erase(i);
			_owner->session().credits().invalidate();
			crl::on_main(&_owner->session(), [=] {
				sendPaid();
			});
		}
	}, _lifetime);

	crl::on_main(&owner->session(), [=] {
		// applyFavorite accesses not yet constructed parts of session.
		rpl::single(rpl::empty) | rpl::then(
			_owner->session().mtp().config().updates()
		) | rpl::map([=] {
			const auto &config = _owner->session().mtp().configValues();
			return config.reactionDefaultCustom
				? ReactionId{ DocumentId(config.reactionDefaultCustom) }
				: ReactionId{ config.reactionDefaultEmoji };
		}) | rpl::filter([=](const ReactionId &id) {
			return !_saveFaveRequestId;
		}) | rpl::start_with_next([=](ReactionId &&id) {
			applyFavorite(id);
		}, _lifetime);
	});
}

Reactions::~Reactions() = default;

Main::Session &Reactions::session() const {
	return _owner->session();
}

void Reactions::refreshTop() {
	requestTop();
}

void Reactions::refreshRecent() {
	requestRecent();
}

void Reactions::refreshRecentDelayed() {
	if (_recentRequestId || _recentRequestScheduled) {
		return;
	}
	_recentRequestScheduled = true;
	base::call_delayed(kRecentRequestTimeout, &_owner->session(), [=] {
		if (_recentRequestScheduled) {
			requestRecent();
		}
	});
}

void Reactions::refreshDefault() {
	requestDefault();
}

void Reactions::refreshMyTags(SavedSublist *sublist) {
	requestMyTags(sublist);
}

void Reactions::refreshMyTagsDelayed() {
	auto &my = _myTags[nullptr];
	if (my.requestId || my.requestScheduled) {
		return;
	}
	my.requestScheduled = true;
	base::call_delayed(kMyTagsRequestTimeout, &_owner->session(), [=] {
		if (_myTags[nullptr].requestScheduled) {
			requestMyTags();
		}
	});
}

void Reactions::refreshTags() {
	requestTags();
}

void Reactions::refreshEffects() {
	if (_effects.empty()) {
		requestEffects();
	}
}

const std::vector<Reaction> &Reactions::list(Type type) const {
	switch (type) {
	case Type::Active: return _active;
	case Type::Recent: return _recent;
	case Type::Top: return _top;
	case Type::All: return _available;
	case Type::MyTags:
		return _myTags.find((SavedSublist*)nullptr)->second.tags;
	case Type::Tags: return _tags;
	case Type::Effects: return _effects;
	}
	Unexpected("Type in Reactions::list.");
}

const std::vector<MyTagInfo> &Reactions::myTagsInfo() const {
	return _myTags.find((SavedSublist*)nullptr)->second.info;
}

const QString &Reactions::myTagTitle(const ReactionId &id) const {
	const auto i = _myTags.find((SavedSublist*)nullptr);
	if (i != end(_myTags)) {
		const auto j = ranges::find(i->second.info, id, &MyTagInfo::id);
		if (j != end(i->second.info)) {
			return j->title;
		}
	}
	static const auto kEmpty = QString();
	return kEmpty;
}

ReactionId Reactions::favoriteId() const {
	return _favoriteId;
}

const Reaction *Reactions::favorite() const {
	return _favorite ? &*_favorite : nullptr;
}

void Reactions::setFavorite(const ReactionId &id) {
	const auto api = &_owner->session().api();
	if (_saveFaveRequestId) {
		api->request(_saveFaveRequestId).cancel();
	}
	_saveFaveRequestId = api->request(MTPmessages_SetDefaultReaction(
		ReactionToMTP(id)
	)).done([=] {
		_saveFaveRequestId = 0;
	}).fail([=] {
		_saveFaveRequestId = 0;
	}).send();

	applyFavorite(id);
}

void Reactions::incrementMyTag(const ReactionId &id, SavedSublist *sublist) {
	if (sublist) {
		incrementMyTag(id, nullptr);
	}
	auto &my = _myTags[sublist];
	auto i = ranges::find(my.info, id, &MyTagInfo::id);
	if (i == end(my.info)) {
		my.info.push_back({ .id = id, .count = 0 });
		i = end(my.info) - 1;
	}
	++i->count;
	while (i != begin(my.info)) {
		auto j = i - 1;
		if (j->count >= i->count) {
			break;
		}
		std::swap(*i, *j);
		i = j;
	}
	scheduleMyTagsUpdate(sublist);
}

void Reactions::decrementMyTag(const ReactionId &id, SavedSublist *sublist) {
	if (sublist) {
		decrementMyTag(id, nullptr);
	}
	auto &my = _myTags[sublist];
	auto i = ranges::find(my.info, id, &MyTagInfo::id);
	if (i != end(my.info) && i->count > 0) {
		--i->count;
		while (i + 1 != end(my.info)) {
			auto j = i + 1;
			if (j->count <= i->count) {
				break;
			}
			std::swap(*i, *j);
			i = j;
		}
	}
	scheduleMyTagsUpdate(sublist);
}

void Reactions::renameTag(const ReactionId &id, const QString &name) {
	auto changed = false;
	for (auto &[sublist, my] : _myTags) {
		auto i = ranges::find(my.info, id, &MyTagInfo::id);
		if (i == end(my.info) || i->title == name) {
			continue;
		}
		i->title = name;
		changed = true;
		scheduleMyTagsUpdate(sublist);
	}
	if (!changed) {
		return;
	}
	_myTagRenamed.fire_copy(id);

	using Flag = MTPmessages_UpdateSavedReactionTag::Flag;
	_owner->session().api().request(MTPmessages_UpdateSavedReactionTag(
		MTP_flags(name.isEmpty() ? Flag(0) : Flag::f_title),
		ReactionToMTP(id),
		MTP_string(name)
	)).send();
}

void Reactions::scheduleMyTagsUpdate(SavedSublist *sublist) {
	auto &my = _myTags[sublist];
	my.updateScheduled = true;
	crl::on_main(&session(), [=] {
		auto &my = _myTags[sublist];
		if (!my.updateScheduled) {
			return;
		}
		my.updateScheduled = false;
		my.tags = resolveByInfos(my.info, _unresolvedMyTags, sublist);
		_myTagsUpdated.fire_copy(sublist);
	});
}

DocumentData *Reactions::chooseGenericAnimation(
		not_null<DocumentData*> custom) const {
	const auto sticker = custom->sticker();
	const auto i = sticker
		? ranges::find(
			_available,
			::Data::ReactionId{ { sticker->alt } },
			&::Data::Reaction::id)
		: end(_available);
	if (i != end(_available) && i->aroundAnimation) {
		const auto view = i->aroundAnimation->createMediaView();
		view->checkStickerLarge();
		if (view->loaded()) {
			return i->aroundAnimation;
		}
	}
	return randomLoadedFrom(_genericAnimations);
}

void Reactions::fillPaidReactionAnimations() const {
	const auto generate = [&](int index) {
		const auto session = &_owner->session();
		const auto name = u"star_reaction_effect%1"_q.arg(index + 1);
		return ChatHelpers::GenerateLocalTgsSticker(session, name);
	};
	const auto kCount = 3;
	for (auto i = 0; i != kCount; ++i) {
		const auto document = generate(i);
		_paidReactionAnimations.push_back(document);
		_paidReactionCache.emplace(
			document,
			document->createMediaView());
	}
	_paidReactionCache.front().second->checkStickerLarge();
}

DocumentData *Reactions::choosePaidReactionAnimation() const {
	if (_paidReactionAnimations.empty()) {
		fillPaidReactionAnimations();
	}
	return randomLoadedFrom(_paidReactionAnimations);
}

DocumentData *Reactions::randomLoadedFrom(
		std::vector<not_null<DocumentData*>> list) const {
	if (list.empty()) {
		return nullptr;
	}
	ranges::shuffle(list);
	const auto first = list.front();
	const auto view = first->createMediaView();
	view->checkStickerLarge();
	if (view->loaded()) {
		return first;
	}
	const auto k = ranges::find_if(list, [&](not_null<DocumentData*> value) {
		return value->createMediaView()->loaded();
	});
	return (k != end(list)) ? (*k) : first;
}

void Reactions::applyFavorite(const ReactionId &id) {
	if (_favoriteId != id) {
		_favoriteId = id;
		_favorite = resolveById(_favoriteId);
		if (!_favorite && _unresolvedFavoriteId != _favoriteId) {
			_unresolvedFavoriteId = _favoriteId;
			resolve(_favoriteId);
		}
		_favoriteUpdated.fire({});
	}
}

rpl::producer<> Reactions::topUpdates() const {
	return _topUpdated.events();
}

rpl::producer<> Reactions::recentUpdates() const {
	return _recentUpdated.events();
}

rpl::producer<> Reactions::defaultUpdates() const {
	return _defaultUpdated.events();
}

rpl::producer<> Reactions::favoriteUpdates() const {
	return _favoriteUpdated.events();
}

rpl::producer<> Reactions::myTagsUpdates() const {
	return _myTagsUpdated.events(
	) | rpl::filter(
		!rpl::mappers::_1
	) | rpl::to_empty;
}

rpl::producer<> Reactions::tagsUpdates() const {
	return _tagsUpdated.events();
}

rpl::producer<ReactionId> Reactions::myTagRenamed() const {
	return _myTagRenamed.events();
}

rpl::producer<> Reactions::effectsUpdates() const {
	return _effectsUpdated.events();
}

void Reactions::preloadReactionImageFor(const ReactionId &emoji) {
	if (emoji.paid() || !emoji.emoji().isEmpty()) {
		preloadImageFor(emoji);
	}
}

void Reactions::preloadEffectImageFor(EffectId id) {
	if (id != kFakeEffectId) {
		preloadImageFor({ DocumentId(id) });
	}
}

void Reactions::preloadImageFor(const ReactionId &id) {
	if (_images.contains(id)) {
		return;
	}
	auto &set = _images.emplace(id).first->second;
	set.effect = (id.custom() != 0);
	if (id.paid()) {
		loadImage(set, lookupPaid()->centerIcon, true);
		return;
	}
	auto &list = set.effect ? _effects : _available;
	const auto i = ranges::find(list, id, &Reaction::id);
	const auto document = (i == end(list))
		? nullptr
		: i->centerIcon
		? i->centerIcon
		: i->selectAnimation.get();
	if (document || (set.effect && i != end(list))) {
		if (!set.effect || i->centerIcon) {
			loadImage(set, document, !i->centerIcon);
		} else {
			generateImage(set, i->title);
		}
		if (set.effect) {
			preloadEffect(*i);
		}
	} else if (set.effect && !_waitingForEffects) {
		_waitingForEffects = true;
		refreshEffects();
	} else if (!set.effect && !_waitingForReactions) {
		_waitingForReactions = true;
		refreshDefault();
	}
}

void Reactions::preloadEffect(const Reaction &effect) {
	if (effect.aroundAnimation) {
		effect.aroundAnimation->createMediaView()->checkStickerLarge();
	} else {
		const auto premium = effect.selectAnimation;
		premium->loadVideoThumbnail(premium->stickerSetOrigin());
	}
}

void Reactions::preloadAnimationsFor(const ReactionId &id) {
	const auto preload = [&](DocumentData *document) {
		const auto view = document
			? document->activeMediaView()
			: nullptr;
		if (view) {
			view->checkStickerLarge();
		}
	};
	if (id.paid()) {
		const auto fake = lookupPaid();
		preload(fake->centerIcon);
		preload(fake->aroundAnimation);
		return;
	}
	const auto custom = id.custom();
	const auto document = custom ? _owner->document(custom).get() : nullptr;
	const auto customSticker = document ? document->sticker() : nullptr;
	const auto findId = custom
		? ReactionId{ { customSticker ? customSticker->alt : QString() } }
		: id;
	const auto i = ranges::find(_available, findId, &Reaction::id);
	if (i == end(_available)) {
		return;
	}
	if (!custom) {
		preload(i->centerIcon);
	}
	preload(i->aroundAnimation);
}

QImage Reactions::resolveReactionImageFor(const ReactionId &emoji) {
	Expects(!emoji.custom());

	return resolveImageFor(emoji);
}

QImage Reactions::resolveEffectImageFor(EffectId id) {
	return (id == kFakeEffectId)
		? QImage()
		: resolveImageFor({ DocumentId(id) });
}

QImage Reactions::resolveImageFor(const ReactionId &id) {
	auto i = _images.find(id);
	if (i == end(_images)) {
		preloadImageFor(id);
		i = _images.find(id);
		Assert(i != end(_images));
	}
	auto &set = i->second;
	set.effect = (id.custom() != 0);

	const auto resolve = [&](QImage &image, int size) {
		const auto factor = style::DevicePixelRatio();
		const auto frameSize = set.fromSelectAnimation
			? (size / 2)
			: size;
		// Must not be colored to text.
		image = set.icon->frame(QColor()).scaled(
			frameSize * factor,
			frameSize * factor,
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation);
		if (set.fromSelectAnimation) {
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
	if (set.image.isNull() && set.icon) {
		resolve(
			set.image,
			set.effect ? st::effectInfoImage : st::reactionInlineImage);
		crl::async([icon = std::move(set.icon)]{});
	}
	return set.image;
}

void Reactions::resolveReactionImages() {
	for (auto &[id, set] : _images) {
		if (set.effect || !set.image.isNull() || set.icon || set.media) {
			continue;
		}
		const auto i = ranges::find(_available, id, &Reaction::id);
		const auto document = (i == end(_available))
			? nullptr
			: i->centerIcon
			? i->centerIcon
			: i->selectAnimation.get();
		if (document) {
			loadImage(set, document, !i->centerIcon);
		} else {
			LOG(("API Error: Reaction '%1' not found!"
				).arg(ReactionIdToLog(id)));
		}
	}
}

void Reactions::resolveEffectImages() {
	for (auto &[id, set] : _images) {
		if (!set.effect || !set.image.isNull() || set.icon || set.media) {
			continue;
		}
		const auto i = ranges::find(_effects, id, &Reaction::id);
		const auto document = (i == end(_effects))
			? nullptr
			: i->centerIcon
			? i->centerIcon
			: nullptr;
		if (document) {
			loadImage(set, document, false);
		} else if (i != end(_effects)) {
			generateImage(set, i->title);
		} else {
			LOG(("API Error: Effect '%1' not found!"
				).arg(ReactionIdToLog(id)));
		}
		if (i != end(_effects)) {
			preloadEffect(*i);
		}
	}
}

void Reactions::loadImage(
		ImageSet &set,
		not_null<DocumentData*> document,
		bool fromSelectAnimation) {
	if (!set.image.isNull() || set.icon) {
		return;
	} else if (!set.media) {
		if (!set.effect) {
			set.fromSelectAnimation = fromSelectAnimation;
		}
		set.media = document->createMediaView();
		set.media->checkStickerLarge();
	}
	if (set.media->loaded()) {
		setAnimatedIcon(set);
	} else if (!_imagesLoadLifetime) {
		document->session().downloaderTaskFinished(
		) | rpl::start_with_next([=] {
			downloadTaskFinished();
		}, _imagesLoadLifetime);
	}
}

void Reactions::generateImage(ImageSet &set, const QString &emoji) {
	Expects(set.effect);

	const auto e = Ui::Emoji::Find(emoji);
	Assert(e != nullptr);

	const auto large = Ui::Emoji::GetSizeLarge();
	const auto factor = style::DevicePixelRatio();
	auto image = QImage(large, large, QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(factor);
	image.fill(Qt::transparent);
	{
		QPainter p(&image);
		Ui::Emoji::Draw(p, e, large, 0, 0);
	}
	const auto size = st::effectInfoImage;
	set.image = image.scaled(size * factor, size * factor);
	set.image.setDevicePixelRatio(factor);
}

void Reactions::setAnimatedIcon(ImageSet &set) {
	const auto size = style::ConvertScale(kSizeForDownscale);
	set.icon = Ui::MakeAnimatedIcon({
		.generator = DocumentIconFrameGenerator(set.media),
		.sizeOverride = QSize(size, size),
		.colorized = set.media->owner()->emojiUsesTextColor(),
	});
	set.media = nullptr;
}

void Reactions::downloadTaskFinished() {
	auto hasOne = false;
	for (auto &[emoji, set] : _images) {
		if (!set.media) {
			continue;
		} else if (set.media->loaded()) {
			setAnimatedIcon(set);
		} else {
			hasOne = true;
		}
	}
	if (!hasOne) {
		_imagesLoadLifetime.destroy();
	}
}

void Reactions::requestTop() {
	if (_topRequestId) {
		return;
	}
	auto &api = _owner->session().api();
	_topRefreshTimer.cancel();
	_topRequestId = api.request(MTPmessages_GetTopReactions(
		MTP_int(kTopReactionsLimit),
		MTP_long(_topHash)
	)).done([=](const MTPmessages_Reactions &result) {
		_topRequestId = 0;
		result.match([&](const MTPDmessages_reactions &data) {
			updateTop(data);
		}, [](const MTPDmessages_reactionsNotModified&) {
		});
	}).fail([=] {
		_topRequestId = 0;
		_topHash = 0;
	}).send();
}

void Reactions::requestRecent() {
	if (_recentRequestId) {
		return;
	}
	auto &api = _owner->session().api();
	_recentRequestScheduled = false;
	_recentRequestId = api.request(MTPmessages_GetRecentReactions(
		MTP_int(kRecentReactionsLimit),
		MTP_long(_recentHash)
	)).done([=](const MTPmessages_Reactions &result) {
		_recentRequestId = 0;
		result.match([&](const MTPDmessages_reactions &data) {
			updateRecent(data);
		}, [](const MTPDmessages_reactionsNotModified&) {
		});
	}).fail([=] {
		_recentRequestId = 0;
		_recentHash = 0;
	}).send();
}

void Reactions::requestDefault() {
	if (_defaultRequestId) {
		return;
	}
	auto &api = _owner->session().api();
	_defaultRequestId = api.request(MTPmessages_GetAvailableReactions(
		MTP_int(_defaultHash)
	)).done([=](const MTPmessages_AvailableReactions &result) {
		_defaultRequestId = 0;
		result.match([&](const MTPDmessages_availableReactions &data) {
			updateDefault(data);
		}, [&](const MTPDmessages_availableReactionsNotModified &) {
		});
	}).fail([=] {
		_defaultRequestId = 0;
		_defaultHash = 0;
	}).send();
}

void Reactions::requestGeneric() {
	if (_genericRequestId) {
		return;
	}
	auto &api = _owner->session().api();
	_genericRequestId = api.request(MTPmessages_GetStickerSet(
		MTP_inputStickerSetEmojiGenericAnimations(),
		MTP_int(0) // hash
	)).done([=](const MTPmessages_StickerSet &result) {
		_genericRequestId = 0;
		result.match([&](const MTPDmessages_stickerSet &data) {
			updateGeneric(data);
		}, [](const MTPDmessages_stickerSetNotModified &) {
			LOG(("API Error: Unexpected messages.stickerSetNotModified."));
		});
	}).fail([=] {
		_genericRequestId = 0;
	}).send();
}

void Reactions::requestMyTags(SavedSublist *sublist) {
	auto &my = _myTags[sublist];
	if (my.requestId) {
		return;
	}
	auto &api = _owner->session().api();
	my.requestScheduled = false;
	using Flag = MTPmessages_GetSavedReactionTags::Flag;
	my.requestId = api.request(MTPmessages_GetSavedReactionTags(
		MTP_flags(sublist ? Flag::f_peer : Flag()),
		(sublist ? sublist->peer()->input : MTP_inputPeerEmpty()),
		MTP_long(my.hash)
	)).done([=](const MTPmessages_SavedReactionTags &result) {
		auto &my = _myTags[sublist];
		my.requestId = 0;
		result.match([&](const MTPDmessages_savedReactionTags &data) {
			updateMyTags(sublist, data);
		}, [](const MTPDmessages_savedReactionTagsNotModified&) {
		});
	}).fail([=] {
		auto &my = _myTags[sublist];
		my.requestId = 0;
		my.hash = 0;
	}).send();
}

void Reactions::requestTags() {
	if (_tagsRequestId) {
		return;
	}
	auto &api = _owner->session().api();
	_tagsRequestId = api.request(MTPmessages_GetDefaultTagReactions(
		MTP_long(_tagsHash)
	)).done([=](const MTPmessages_Reactions &result) {
		_tagsRequestId = 0;
		result.match([&](const MTPDmessages_reactions &data) {
			updateTags(data);
		}, [](const MTPDmessages_reactionsNotModified&) {
		});
	}).fail([=] {
		_tagsRequestId = 0;
		_tagsHash = 0;
	}).send();

}

void Reactions::requestEffects() {
	if (_effectsRequestId) {
		return;
	}
	auto &api = _owner->session().api();
	_effectsRequestId = api.request(MTPmessages_GetAvailableEffects(
		MTP_int(_effectsHash)
	)).done([=](const MTPmessages_AvailableEffects &result) {
		_effectsRequestId = 0;
		result.match([&](const MTPDmessages_availableEffects &data) {
			updateEffects(data);
		}, [&](const MTPDmessages_availableEffectsNotModified &) {
		});
	}).fail([=] {
		_effectsRequestId = 0;
		_effectsHash = 0;
	}).send();
}

void Reactions::updateTop(const MTPDmessages_reactions &data) {
	_topHash = data.vhash().v;
	_topIds = ListFromMTP(data);
	_top = resolveByIds(_topIds, _unresolvedTop);
	_topUpdated.fire({});
}

void Reactions::updateRecent(const MTPDmessages_reactions &data) {
	_recentHash = data.vhash().v;
	_recentIds = ListFromMTP(data);
	_recent = resolveByIds(_recentIds, _unresolvedRecent);
	recentUpdated();
}

void Reactions::updateDefault(const MTPDmessages_availableReactions &data) {
	_defaultHash = data.vhash().v;

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
	if (_waitingForReactions) {
		_waitingForReactions = false;
		resolveReactionImages();
	}
	defaultUpdated();
}

void Reactions::updateGeneric(const MTPDmessages_stickerSet &data) {
	const auto oldCache = base::take(_genericCache);
	const auto toCache = [&](not_null<DocumentData*> document) {
		if (document->sticker()) {
			_genericAnimations.push_back(document);
			_genericCache.emplace(document, document->createMediaView());
		}
	};
	const auto &list = data.vdocuments().v;
	_genericAnimations.clear();
	_genericAnimations.reserve(list.size());
	_genericCache.reserve(list.size());
	for (const auto &sticker : data.vdocuments().v) {
		toCache(_owner->processDocument(sticker));
	}
	if (!_genericCache.empty()) {
		_genericCache.front().second->checkStickerLarge();
	}
}

void Reactions::updateMyTags(
		SavedSublist *sublist,
		const MTPDmessages_savedReactionTags &data) {
	auto &my = _myTags[sublist];
	my.hash = data.vhash().v;
	auto list = ListFromMTP(data);
	auto renamed = base::flat_set<ReactionId>();
	if (!sublist) {
		for (const auto &info : list) {
			const auto j = ranges::find(my.info, info.id, &MyTagInfo::id);
			const auto was = (j != end(my.info)) ? j->title : QString();
			if (info.title != was) {
				renamed.emplace(info.id);
			}
		}
	}
	my.info = std::move(list);
	my.tags = resolveByInfos(my.info, _unresolvedMyTags, sublist);
	_myTagsUpdated.fire_copy(sublist);
	for (const auto &id : renamed) {
		_myTagRenamed.fire_copy(id);
	}
}

void Reactions::updateTags(const MTPDmessages_reactions &data) {
	_tagsHash = data.vhash().v;
	_tagsIds = ListFromMTP(data);
	_tags = resolveByIds(_tagsIds, _unresolvedTags);
	_tagsUpdated.fire({});
}

void Reactions::updateEffects(const MTPDmessages_availableEffects &data) {
	_effectsHash = data.vhash().v;

	const auto &list = data.veffects().v;
	const auto toCache = [&](DocumentData *document) {
		if (document) {
			_iconsCache.emplace(document, document->createMediaView());
		}
	};
	for (const auto &document : data.vdocuments().v) {
		toCache(_owner->processDocument(document));
	}
	_effects.clear();
	_effects.reserve(list.size());
	for (const auto &effect : list) {
		if (const auto parsed = parse(effect)) {
			_effects.push_back(*parsed);
		}
	}
	if (_waitingForEffects) {
		_waitingForEffects = false;
		resolveEffectImages();
	}
	effectsUpdated();
}

void Reactions::recentUpdated() {
	_topRefreshTimer.callOnce(kTopRequestDelay);
	_recentUpdated.fire({});
}

void Reactions::defaultUpdated() {
	refreshTop();
	refreshRecent();
	if (_genericAnimations.empty()) {
		requestGeneric();
	}
	refreshMyTags();
	refreshTags();
	refreshEffects();
	_defaultUpdated.fire({});
}

void Reactions::myTagsUpdated() {
	if (_genericAnimations.empty()) {
		requestGeneric();
	}
	_myTagsUpdated.fire({});
}

void Reactions::tagsUpdated() {
	if (_genericAnimations.empty()) {
		requestGeneric();
	}
	_tagsUpdated.fire({});
}

void Reactions::effectsUpdated() {
	_effectsUpdated.fire({});
}

not_null<CustomEmojiManager::Listener*> Reactions::resolveListener() {
	return static_cast<CustomEmojiManager::Listener*>(this);
}

void Reactions::customEmojiResolveDone(not_null<DocumentData*> document) {
	const auto id = ReactionId{ { document->id } };
	const auto favorite = (_unresolvedFavoriteId == id);
	const auto i = _unresolvedTop.find(id);
	const auto top = (i != end(_unresolvedTop));
	const auto j = _unresolvedRecent.find(id);
	const auto recent = (j != end(_unresolvedRecent));
	const auto k = _unresolvedMyTags.find(id);
	const auto myTagSublists = (k != end(_unresolvedMyTags))
		? base::take(k->second)
		: base::flat_set<SavedSublist*>();
	const auto l = _unresolvedTags.find(id);
	const auto tag = (l != end(_unresolvedTags));
	if (favorite) {
		_unresolvedFavoriteId = ReactionId();
		_favorite = resolveById(_favoriteId);
	}
	if (top) {
		_unresolvedTop.erase(i);
		_top = resolveByIds(_topIds, _unresolvedTop);
	}
	if (recent) {
		_unresolvedRecent.erase(j);
		_recent = resolveByIds(_recentIds, _unresolvedRecent);
	}
	if (!myTagSublists.empty()) {
		_unresolvedMyTags.erase(k);
		for (const auto &sublist : myTagSublists) {
			auto &my = _myTags[sublist];
			my.tags = resolveByInfos(my.info, _unresolvedMyTags, sublist);
		}
	}
	if (tag) {
		_unresolvedTags.erase(l);
		_tags = resolveByIds(_tagsIds, _unresolvedTags);
	}
	if (favorite) {
		_favoriteUpdated.fire({});
	}
	if (top) {
		_topUpdated.fire({});
	}
	if (recent) {
		_recentUpdated.fire({});
	}
	for (const auto &sublist : myTagSublists) {
		_myTagsUpdated.fire_copy(sublist);
	}
	if (tag) {
		_tagsUpdated.fire({});
	}
}

std::optional<Reaction> Reactions::resolveById(const ReactionId &id) {
	if (const auto emoji = id.emoji(); !emoji.isEmpty()) {
		const auto i = ranges::find(_available, id, &Reaction::id);
		if (i != end(_available)) {
			return *i;
		}
	} else if (const auto customId = id.custom()) {
		const auto document = _owner->document(customId);
		if (document->sticker()) {
			return CustomReaction(document);
		}
	}
	return {};
}

std::vector<Reaction> Reactions::resolveByIds(
		const std::vector<ReactionId> &ids,
		base::flat_set<ReactionId> &unresolved) {
	auto result = std::vector<Reaction>();
	result.reserve(ids.size());
	for (const auto &id : ids) {
		if (const auto resolved = resolveById(id)) {
			result.push_back(*resolved);
		} else if (unresolved.emplace(id).second) {
			resolve(id);
		}
	}
	return result;
}

std::optional<Reaction> Reactions::resolveByInfo(
		const MyTagInfo &info,
		SavedSublist *sublist) {
	const auto withInfo = [&](Reaction reaction) {
		reaction.count = info.count;
		reaction.title = sublist ? myTagTitle(reaction.id) : info.title;
		return reaction;
	};
	if (const auto emoji = info.id.emoji(); !emoji.isEmpty()) {
		const auto i = ranges::find(_available, info.id, &Reaction::id);
		if (i != end(_available)) {
			return withInfo(*i);
		}
	} else if (const auto customId = info.id.custom()) {
		const auto document = _owner->document(customId);
		if (document->sticker()) {
			return withInfo(CustomReaction(document));
		}
	}
	return {};
}

std::vector<Reaction> Reactions::resolveByInfos(
		const std::vector<MyTagInfo> &infos,
		base::flat_map<
			ReactionId,
			base::flat_set<SavedSublist*>> &unresolved,
		SavedSublist *sublist) {
	auto result = std::vector<Reaction>();
	result.reserve(infos.size());
	for (const auto &tag : infos) {
		if (auto resolved = resolveByInfo(tag, sublist)) {
			result.push_back(*resolved);
		} else if (const auto i = unresolved.find(tag.id)
			; i != end(unresolved)) {
			i->second.emplace(sublist);
		} else {
			unresolved[tag.id].emplace(sublist);
			resolve(tag.id);
		}
	}
	return result;
}

void Reactions::resolve(const ReactionId &id) {
	if (const auto emoji = id.emoji(); !emoji.isEmpty()) {
		refreshDefault();
	} else if (const auto customId = id.custom()) {
		_owner->customEmojiManager().resolve(
			customId,
			resolveListener());
	}
}

std::optional<Reaction> Reactions::parse(const MTPAvailableReaction &entry) {
	const auto &data = entry.data();
	const auto emoji = qs(data.vreaction());
	const auto known = (Ui::Emoji::Find(emoji) != nullptr);
	if (!known) {
		LOG(("API Error: Unknown emoji in reactions: %1").arg(emoji));
		return std::nullopt;
	}
	return std::make_optional(Reaction{
		.id = ReactionId{ emoji },
		.title = qs(data.vtitle()),
		//.staticIcon = _owner->processDocument(data.vstatic_icon()),
		.appearAnimation = _owner->processDocument(
			data.vappear_animation()),
		.selectAnimation = _owner->processDocument(
			data.vselect_animation()),
		//.activateAnimation = _owner->processDocument(
		//	data.vactivate_animation()),
		//.activateEffects = _owner->processDocument(
		//	data.veffect_animation()),
		.centerIcon = (data.vcenter_icon()
			? _owner->processDocument(*data.vcenter_icon()).get()
			: nullptr),
		.aroundAnimation = (data.varound_animation()
			? _owner->processDocument(*data.varound_animation()).get()
			: nullptr),
		.active = !data.is_inactive(),
	});
}

std::optional<Reaction> Reactions::parse(const MTPAvailableEffect &entry) {
	const auto &data = entry.data();
	const auto emoji = qs(data.vemoticon());
	const auto known = (Ui::Emoji::Find(emoji) != nullptr);
	if (!known) {
		LOG(("API Error: Unknown emoji in effects: %1").arg(emoji));
		return std::nullopt;
	}
	const auto id = DocumentId(data.vid().v);
	const auto stickerId = data.veffect_sticker_id().v;
	const auto document = _owner->document(stickerId);
	if (!document->sticker()) {
		LOG(("API Error: Bad sticker in effects: %1").arg(stickerId));
		return std::nullopt;
	}
	const auto aroundId = data.veffect_animation_id().value_or_empty();
	const auto around = aroundId
		? _owner->document(aroundId).get()
		: nullptr;
	if (around && !around->sticker()) {
		LOG(("API Error: Bad sticker in effects around: %1").arg(aroundId));
		return std::nullopt;
	}
	const auto iconId = data.vstatic_icon_id().value_or_empty();
	const auto icon = iconId ? _owner->document(iconId).get() : nullptr;
	if (icon && !icon->sticker()) {
		LOG(("API Error: Bad sticker in effects icon: %1").arg(iconId));
		return std::nullopt;
	}
	return std::make_optional(Reaction{
		.id = ReactionId{ id },
		.title = emoji,
		.appearAnimation = document,
		.selectAnimation = document,
		.centerIcon = icon,
		.aroundAnimation = around,
		.active = true,
		.effect = true,
		.premium = data.is_premium_required(),
	});
}

void Reactions::send(not_null<HistoryItem*> item, bool addToRecent) {
	const auto id = item->fullId();
	auto &api = _owner->session().api();
	auto i = _sentRequests.find(id);
	if (i != end(_sentRequests)) {
		api.request(i->second).cancel();
	} else {
		i = _sentRequests.emplace(id).first;
	}
	const auto chosen = item->chosenReactions();
	using Flag = MTPmessages_SendReaction::Flag;
	const auto flags = (chosen.empty() ? Flag(0) : Flag::f_reaction)
		| (addToRecent ? Flag::f_add_to_recent : Flag(0));
	i->second = api.request(MTPmessages_SendReaction(
		MTP_flags(flags),
		item->history()->peer->input,
		MTP_int(id.msg),
		MTP_vector<MTPReaction>(chosen | ranges::views::filter([](
				const ReactionId &id) {
			return !id.paid();
		}) | ranges::views::transform(
			ReactionToMTP
		) | ranges::to<QVector<MTPReaction>>())
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

void Reactions::clearTemporary() {
	_temporary.clear();
}

Reaction *Reactions::lookupTemporary(const ReactionId &id) {
	if (id.paid()) {
		return lookupPaid();
	} else if (const auto emoji = id.emoji(); !emoji.isEmpty()) {
		const auto i = ranges::find(_available, id, &Reaction::id);
		return (i != end(_available)) ? &*i : nullptr;
	} else if (const auto customId = id.custom()) {
		if (const auto i = _temporary.find(customId); i != end(_temporary)) {
			return &i->second;
		}
		const auto document = _owner->document(customId);
		if (document->sticker()) {
			return &_temporary.emplace(
				customId,
				CustomReaction(document)).first->second;
		}
		_owner->customEmojiManager().resolve(
			customId,
			resolveListener());
		return nullptr;
	}
	return nullptr;
}

not_null<Reaction*> Reactions::lookupPaid() {
	if (!_paid) {
		const auto generate = [&](const QString &name) {
			const auto session = &_owner->session();
			return ChatHelpers::GenerateLocalTgsSticker(session, name);
		};
		const auto appear = generate(u"star_reaction_appear"_q);
		const auto center = generate(u"star_reaction_center"_q);
		const auto select = generate(u"star_reaction_select"_q);
		_paid.emplace(Reaction{
			.id = ReactionId::Paid(),
			.title = u"Telegram Star"_q,
			.appearAnimation = appear,
			.selectAnimation = select,
			.centerIcon = center,
			.active = true,
		});
		_iconsCache.emplace(appear, appear->createMediaView());
		_iconsCache.emplace(center, center->createMediaView());
		_iconsCache.emplace(select, select->createMediaView());

		fillPaidReactionAnimations();
	}
	return &*_paid;
}

not_null<DocumentData*> Reactions::paidToastAnimation() {
	if (!_paidToastAnimation) {
		_paidToastAnimation = ChatHelpers::GenerateLocalTgsSticker(
			&_owner->session(),
			u"star_reaction_toast"_q);
	}
	return _paidToastAnimation;
}

rpl::producer<std::vector<Reaction>> Reactions::myTagsValue(
		SavedSublist *sublist) {
	refreshMyTags(sublist);
	const auto list = [=] {
		return _myTags[sublist].tags;
	};
	return rpl::single(
		list()
	) | rpl::then(_myTagsUpdated.events(
	) | rpl::filter(
		rpl::mappers::_1 == sublist
	) | rpl::map(list));
}

bool Reactions::isQuitPrevent() {
	for (auto i = begin(_sendPaidItems); i != end(_sendPaidItems);) {
		const auto item = i->first;
		if (_sendingPaid.contains(item)) {
			++i;
		} else {
			i = _sendPaidItems.erase(i);
			sendPaid(item);
		}
	}
	if (_sendingPaid.empty()) {
		return false;
	}
	LOG(("Reactions prevents quit, sending paid..."));
	return true;
}

void Reactions::schedulePaid(not_null<HistoryItem*> item) {
	_sendPaidItems[item] = crl::now() + kPaidAccumulatePeriod;
	if (!_sendPaidTimer.isActive()) {
		_sendPaidTimer.callOnce(kPaidAccumulatePeriod);
	}
}

void Reactions::undoScheduledPaid(not_null<HistoryItem*> item) {
	_sendPaidItems.remove(item);
	item->cancelScheduledPaidReaction();
}

crl::time Reactions::sendingScheduledPaidAt(
		not_null<HistoryItem*> item) const {
	const auto i = _sendPaidItems.find(item);
	return (i != end(_sendPaidItems)) ? i->second : crl::time();
}

crl::time Reactions::ScheduledPaidDelay() {
	return kPaidAccumulatePeriod;
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
	return _sentRequests.contains(item->fullId())
		|| _sendingPaid.contains(item);
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

void Reactions::sendPaid() {
	if (!_sendingPaid.empty()) {
		return;
	}
	auto next = crl::time();
	const auto now = crl::now();
	for (auto i = begin(_sendPaidItems); i != end(_sendPaidItems);) {
		const auto item = i->first;
		const auto when = i->second;
		if (when > now) {
			if (!next || next > when) {
				next = when;
			}
			++i;
		} else {
			i = _sendPaidItems.erase(i);
			if (sendPaid(item)) {
				return;
			}
		}
	}
	if (next) {
		_sendPaidTimer.callOnce(next - now);
	}
}

bool Reactions::sendPaid(not_null<HistoryItem*> item) {
	const auto send = item->startPaidReactionSending();
	if (!send.valid) {
		return false;
	}

	sendPaidRequest(item, send);
	return true;
}

void Reactions::sendPaidPrivacyRequest(
		not_null<HistoryItem*> item,
		PaidReactionSend send) {
	Expects(!_sendingPaid.contains(item));
	Expects(send.anonymous.has_value());
	Expects(!send.count);

	const auto id = item->fullId();
	auto &api = _owner->session().api();
	const auto requestId = api.request(
		MTPmessages_TogglePaidReactionPrivacy(
			item->history()->peer->input,
			MTP_int(id.msg),
			MTP_bool(*send.anonymous))
	).done([=] {
		if (const auto item = _owner->message(id)) {
			if (_sendingPaid.remove(item)) {
				sendPaidFinish(item, send, true);
			}
		}
		checkQuitPreventFinished();
	}).fail([=](const MTP::Error &error) {
		if (const auto item = _owner->message(id)) {
			if (_sendingPaid.remove(item)) {
				sendPaidFinish(item, send, false);
			}
		}
		checkQuitPreventFinished();
	}).send();
	_sendingPaid[item] = requestId;
}

void Reactions::sendPaidRequest(
		not_null<HistoryItem*> item,
		PaidReactionSend send) {
	Expects(!_sendingPaid.contains(item));

	if (!send.count) {
		sendPaidPrivacyRequest(item, send);
		return;
	}

	const auto id = item->fullId();
	const auto randomId = base::unixtime::mtproto_msg_id();
	auto &api = _owner->session().api();
	using Flag = MTPmessages_SendPaidReaction::Flag;
	const auto requestId = api.request(MTPmessages_SendPaidReaction(
		MTP_flags(send.anonymous ? Flag::f_private : Flag()),
		item->history()->peer->input,
		MTP_int(id.msg),
		MTP_int(send.count),
		MTP_long(randomId),
		MTP_bool(send.anonymous.value_or(false))
	)).done([=](const MTPUpdates &result) {
		if (const auto item = _owner->message(id)) {
			if (_sendingPaid.remove(item)) {
				sendPaidFinish(item, send, true);
			}
		}
		_owner->session().api().applyUpdates(result);
		checkQuitPreventFinished();
	}).fail([=](const MTP::Error &error) {
		if (const auto item = _owner->message(id)) {
			_sendingPaid.remove(item);
			if (error.type() == u"RANDOM_ID_EXPIRED"_q) {
				sendPaidRequest(item, send);
			} else {
				sendPaidFinish(item, send, false);
			}
		}
		checkQuitPreventFinished();
	}).send();
	_sendingPaid[item] = requestId;
}

void Reactions::checkQuitPreventFinished() {
	if (_sendingPaid.empty()) {
		if (Core::Quitting()) {
			LOG(("Reactions doesn't prevent quit any more."));
		}
		Core::App().quitPreventFinished();
	}
}

void Reactions::sendPaidFinish(
		not_null<HistoryItem*> item,
		PaidReactionSend send,
		bool success) {
	item->finishPaidReactionSending(send, success);
	sendPaid();
}

MessageReactions::MessageReactions(not_null<HistoryItem*> item)
: _item(item) {
}

MessageReactions::~MessageReactions() {
	cancelScheduledPaid();
	if (const auto paid = _paid.get()) {
		if (paid->sending > 0) {
			finishPaidSending({
				.count = int(paid->sending),
				.valid = true,
				.anonymous = MaybeAnonymous(
					paid->sendingPrivacySet,
					paid->sendingAnonymous),
			}, false);
		}
	}
}

void MessageReactions::add(const ReactionId &id, bool addToRecent) {
	Expects(!id.empty());
	Expects(!id.paid());

	const auto history = _item->history();
	const auto myLimit = SentReactionsLimit(_item);
	if (ranges::contains(chosen(), id)) {
		return;
	}
	auto my = 0;
	const auto tags = _item->reactionsAreTags();
	if (tags) {
		const auto sublist = _item->savedSublist();
		history->owner().reactions().incrementMyTag(id, sublist);
	}
	_list.erase(ranges::remove_if(_list, [&](MessageReaction &one) {
		if (one.id.paid()) {
			return false;
		}
		const auto removing = one.my && (my == myLimit || ++my == myLimit);
		if (!removing) {
			return false;
		}
		one.my = false;
		const auto removed = !--one.count;
		const auto j = _recent.find(one.id);
		if (j != end(_recent)) {
			if (removed) {
				j->second.clear();
				_recent.erase(j);
			} else {
				j->second.erase(
					ranges::remove(j->second, true, &RecentReaction::my),
					end(j->second));
				if (j->second.empty()) {
					_recent.erase(j);
				}
			}
		}
		if (tags) {
			const auto sublist = _item->savedSublist();
			history->owner().reactions().decrementMyTag(one.id, sublist);
		}
		return removed;
	}), end(_list));
	const auto peer = history->peer;
	if (_item->canViewReactions() || peer->isUser()) {
		auto &list = _recent[id];
		const auto from = peer->session().sendAsPeers().resolveChosen(peer);
		list.insert(begin(list), RecentReaction{
			.peer = from,
			.my = true,
		});
	}
	const auto i = ranges::find(_list, id, &MessageReaction::id);
	if (i != end(_list)) {
		i->my = true;
		++i->count;
		std::rotate(i, i + 1, end(_list));
	} else {
		_list.push_back({ .id = id, .count = 1, .my = true });
	}
	auto &owner = history->owner();
	owner.reactions().send(_item, addToRecent);
	owner.notifyItemDataChange(_item);
}

void MessageReactions::remove(const ReactionId &id) {
	Expects(!id.paid());

	const auto history = _item->history();
	const auto self = history->session().user();
	const auto i = ranges::find(_list, id, &MessageReaction::id);
	const auto j = _recent.find(id);
	if (i == end(_list)) {
		Assert(j == end(_recent));
		return;
	} else if (!i->my) {
		Assert(j == end(_recent)
			|| !ranges::contains(j->second, self, &RecentReaction::peer));
		return;
	}
	i->my = false;
	const auto tags = _item->reactionsAreTags();
	const auto removed = !--i->count;
	if (removed) {
		_list.erase(i);
	}
	if (j != end(_recent)) {
		if (removed) {
			j->second.clear();
			_recent.erase(j);
		} else {
			j->second.erase(
				ranges::remove(j->second, true, &RecentReaction::my),
				end(j->second));
			if (j->second.empty()) {
				_recent.erase(j);
			}
		}
	}
	if (tags) {
		const auto sublist = _item->savedSublist();
		history->owner().reactions().decrementMyTag(id, sublist);
	}
	auto &owner = history->owner();
	owner.reactions().send(_item, false);
	owner.notifyItemDataChange(_item);
}

bool MessageReactions::checkIfChanged(
		const QVector<MTPReactionCount> &list,
		const QVector<MTPMessagePeerReaction> &recent,
		bool min) const {
	auto &owner = _item->history()->owner();
	if (owner.reactions().sending(_item)) {
		// We'll apply non-stale data from the request response.
		return false;
	}
	auto existing = base::flat_set<ReactionId>();
	for (const auto &count : list) {
		const auto changed = count.match([&](const MTPDreactionCount &data) {
			const auto id = ReactionFromMTP(data.vreaction());
			const auto nowCount = data.vcount().v;
			const auto i = ranges::find(_list, id, &MessageReaction::id);
			const auto wasCount = (i != end(_list)) ? i->count : 0;
			if (wasCount != nowCount) {
				return true;
			}
			existing.emplace(id);
			return false;
		});
		if (changed) {
			return true;
		}
	}
	for (const auto &reaction : _list) {
		if (!existing.contains(reaction.id)) {
			return true;
		}
	}
	auto parsed = base::flat_map<ReactionId, std::vector<RecentReaction>>();
	for (const auto &reaction : recent) {
		reaction.match([&](const MTPDmessagePeerReaction &data) {
			const auto id = ReactionFromMTP(data.vreaction());
			if (!ranges::contains(_list, id, &MessageReaction::id)) {
				return;
			}
			const auto peerId = peerFromMTP(data.vpeer_id());
			const auto peer = owner.peer(peerId);
			const auto my = IsMyRecent(data, id, peer, _recent, min);
			parsed[id].push_back({
				.peer = peer,
				.unread = data.is_unread(),
				.big = data.is_big(),
				.my = my,
			});
		});
	}
	return !ranges::equal(_recent, parsed, [](
			const auto &a,
			const auto &b) {
		return ranges::equal(a.second, b.second, [](
				const RecentReaction &a,
				const RecentReaction &b) {
			return (a.peer == b.peer) && (a.big == b.big) && (a.my == b.my);
		});
	});
}

bool MessageReactions::change(
		const QVector<MTPReactionCount> &list,
		const QVector<MTPMessagePeerReaction> &recent,
		const QVector<MTPMessageReactor> &top,
		bool min) {
	auto &owner = _item->history()->owner();
	if (owner.reactions().sending(_item)) {
		// We'll apply non-stale data from the request response.
		return false;
	}
	auto changed = false;
	auto existing = base::flat_set<ReactionId>();
	auto order = base::flat_map<ReactionId, int>();
	for (const auto &count : list) {
		count.match([&](const MTPDreactionCount &data) {
			const auto id = ReactionFromMTP(data.vreaction());
			const auto &chosen = data.vchosen_order();
			if (!min && chosen) {
				order[id] = chosen->v;
			}
			const auto i = ranges::find(_list, id, &MessageReaction::id);
			const auto nowCount = data.vcount().v;
			if (i == end(_list)) {
				changed = true;
				_list.push_back({
					.id = id,
					.count = nowCount,
					.my = (!min && chosen)
				});
			} else {
				const auto nowMy = min ? i->my : chosen.has_value();
				if (i->count != nowCount || i->my != nowMy) {
					i->count = nowCount;
					i->my = nowMy;
					changed = true;
				}
			}
			existing.emplace(id);
		});
	}
	if (!min && !order.empty()) {
		const auto minimal = std::numeric_limits<int>::min();
		const auto proj = [&](const MessageReaction &reaction) {
			return reaction.my ? order[reaction.id] : minimal;
		};
		const auto correctOrder = [&] {
			auto previousOrder = minimal;
			for (const auto &reaction : _list) {
				const auto nowOrder = proj(reaction);
				if (nowOrder < previousOrder) {
					return false;
				}
				previousOrder = nowOrder;
			}
			return true;
		}();
		if (!correctOrder) {
			changed = true;
			ranges::sort(_list, std::less(), proj);
		}
	}
	if (_list.size() != existing.size()) {
		changed = true;
		for (auto i = begin(_list); i != end(_list);) {
			if (!existing.contains(i->id)) {
				i = _list.erase(i);
			} else {
				++i;
			}
		}
	}
	auto parsed = base::flat_map<ReactionId, std::vector<RecentReaction>>();
	for (const auto &reaction : recent) {
		reaction.match([&](const MTPDmessagePeerReaction &data) {
			const auto id = ReactionFromMTP(data.vreaction());
			const auto i = ranges::find(_list, id, &MessageReaction::id);
			if (i == end(_list)) {
				return;
			}
			auto &list = parsed[id];
			if (list.size() >= i->count) {
				return;
			}
			const auto peer = owner.peer(peerFromMTP(data.vpeer_id()));
			const auto my = IsMyRecent(data, id, peer, _recent, min);
			list.push_back({
				.peer = peer,
				.unread = data.is_unread(),
				.big = data.is_big(),
				.my = my,
			});
		});
	}
	if (_recent != parsed) {
		_recent = std::move(parsed);
		changed = true;
	}

	auto paidTop = std::vector<TopPaid>();
	const auto &paindTopNow = _paid ? _paid->top : std::vector<TopPaid>();
	for (const auto &reactor : top) {
		const auto &data = reactor.data();
		const auto peerId = (data.is_anonymous() || !data.vpeer_id())
			? PeerId()
			: peerFromMTP(*data.vpeer_id());
		const auto peer = peerId ? owner.peer(peerId).get() : nullptr;
		paidTop.push_back({
			.peer = peer,
			.count = uint32(data.vcount().v),
			.top = data.is_top(),
			.my = IsMyTop(data, peer, paindTopNow, min),
		});
	}
	if (paidTop.empty()) {
		if (_paid && !_paid->top.empty()) {
			changed = true;
			if (localPaidData()) {
				_paid->top.clear();
			} else {
				_paid = nullptr;
			}
		}
	} else {
		if (min && _paid) {
			const auto mine = [](const TopPaid &entry) {
				return entry.my != 0;
			};
			if (!ranges::contains(paidTop, true, mine)) {
				const auto nonTopMine = [](const TopPaid &entry) {
					return entry.my && !entry.top;
				};
				const auto i = ranges::find(_paid->top, true, nonTopMine);
				if (i != end(_paid->top)) {
					paidTop.push_back(*i);
				}
			}
		}
		ranges::sort(paidTop, std::greater(), [](const TopPaid &entry) {
			return entry.count;
		});
		if (!_paid) {
			_paid = std::make_unique<Paid>();
		}
		if (_paid->top != paidTop) {
			_paid->top = std::move(paidTop);
			changed = true;
		}
	}
	return changed;
}

const std::vector<MessageReaction> &MessageReactions::list() const {
	return _list;
}

auto MessageReactions::recent() const
-> const base::flat_map<ReactionId, std::vector<RecentReaction>> & {
	return _recent;
}

auto MessageReactions::topPaid() const -> const std::vector<TopPaid> & {
	static const auto kEmpty = std::vector<TopPaid>();
	return _paid ? _paid->top : kEmpty;
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

void MessageReactions::scheduleSendPaid(
		int count,
		std::optional<bool> anonymous) {
	Expects(count >= 0);

	if (!_paid) {
		_paid = std::make_unique<Paid>();
	}
	_paid->scheduled += count;
	_paid->scheduledFlag = 1;
	if (anonymous.has_value()) {
		_paid->scheduledAnonymous = anonymous.value_or(false) ? 1 : 0;
		_paid->scheduledPrivacySet = anonymous.has_value();
	}
	if (count > 0) {
		_item->history()->session().credits().lock(count);
	}
	_item->history()->owner().reactions().schedulePaid(_item);
}

int MessageReactions::scheduledPaid() const {
	return _paid ? _paid->scheduled : 0;
}

void MessageReactions::cancelScheduledPaid() {
	if (_paid) {
		if (_paid->scheduledFlag) {
			if (const auto amount = int(_paid->scheduled)) {
				_item->history()->session().credits().unlock(amount);
			}
			_paid->scheduled = 0;
			_paid->scheduledFlag = 0;
			_paid->scheduledAnonymous = 0;
			_paid->scheduledPrivacySet = 0;
		}
		if (!_paid->sendingFlag && _paid->top.empty()) {
			_paid = nullptr;
		}
	}
}

PaidReactionSend MessageReactions::startPaidSending() {
	if (!_paid || !_paid->scheduledFlag || _paid->sendingFlag) {
		return {};
	}
	_paid->sending = _paid->scheduled;
	_paid->sendingFlag = _paid->scheduledFlag;
	_paid->sendingAnonymous = _paid->scheduledAnonymous;
	_paid->sendingPrivacySet = _paid->scheduledPrivacySet;
	_paid->scheduled = 0;
	_paid->scheduledFlag = 0;
	_paid->scheduledAnonymous = 0;
	_paid->scheduledPrivacySet = 0;
	return {
		.count = int(_paid->sending),
		.valid = true,
		.anonymous = MaybeAnonymous(
			_paid->sendingPrivacySet,
			_paid->sendingAnonymous),
	};
}

void MessageReactions::finishPaidSending(
		PaidReactionSend send,
		bool success) {
	Expects(_paid != nullptr);
	Expects(send.count == _paid->sending);
	Expects(send.valid == (_paid->sendingFlag == 1));
	Expects(send.anonymous == MaybeAnonymous(
		_paid->sendingPrivacySet,
		_paid->sendingAnonymous));

	_paid->sending = 0;
	_paid->sendingFlag = 0;
	_paid->sendingAnonymous = 0;
	_paid->sendingPrivacySet = 0;
	if (!_paid->scheduledFlag && _paid->top.empty()) {
		_paid = nullptr;
	} else if (!send.count) {
		const auto i = ranges::find_if(_paid->top, [](const TopPaid &top) {
			return top.my;
		});
		if (i != end(_paid->top)) {
			i->peer = send.anonymous
				? nullptr
				: _item->history()->session().user().get();
		}
	}
	if (const auto amount = send.count) {
		const auto credits = &_item->history()->session().credits();
		if (success) {
			credits->withdrawLocked(amount);
		} else {
			credits->unlock(amount);
		}
	}
}

bool MessageReactions::localPaidData() const {
	return _paid && (_paid->scheduledFlag || _paid->sendingFlag);
}

int MessageReactions::localPaidCount() const {
	return _paid ? (_paid->scheduled + _paid->sending) : 0;
}

bool MessageReactions::localPaidAnonymous() const {
	const auto minePaidAnonymous = [&] {
		for (const auto &entry : _paid->top) {
			if (entry.my) {
				return !entry.peer;
			}
		}
		const auto api = &_item->history()->session().api();
		return api->globalPrivacy().paidReactionAnonymousCurrent();
	};
	return _paid
		&& ((_paid->scheduledFlag && _paid->scheduledPrivacySet)
			? (_paid->scheduledAnonymous == 1)
			: (_paid->sendingFlag && _paid->sendingPrivacySet)
			? (_paid->sendingAnonymous == 1)
			: minePaidAnonymous());
}

bool MessageReactions::clearCloudData() {
	const auto result = !_list.empty();
	_recent.clear();
	_list.clear();
	if (localPaidData()) {
		_paid->top.clear();
	} else {
		_paid = nullptr;
	}
	return result;
}

std::vector<ReactionId> MessageReactions::chosen() const {
	return _list
		| ranges::views::filter(&MessageReaction::my)
		| ranges::views::transform(&MessageReaction::id)
		| ranges::to_vector;
}

} // namespace Data
