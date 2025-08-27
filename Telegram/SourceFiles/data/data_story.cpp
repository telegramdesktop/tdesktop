/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_story.h"

#include "base/unixtime.h"
#include "api/api_text_entities.h"
#include "data/data_document.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_file_origin.h"
#include "data/data_media_preload.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "data/data_stories.h"
#include "data/data_thread.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "media/streaming/media_streaming_reader.h"
#include "storage/download_manager_mtproto.h"
#include "storage/file_download.h" // kMaxFileInMemory
#include "ui/text/text_utilities.h"
#include "ui/color_int_conversion.h"

namespace Data {
namespace {

using UpdateFlag = StoryUpdate::Flag;

[[nodiscard]] StoryArea ParseArea(const MTPMediaAreaCoordinates &area) {
	const auto &data = area.data();
	const auto center = QPointF(data.vx().v, data.vy().v);
	const auto size = QSizeF(data.vw().v, data.vh().v);
	const auto corner = center - QPointF(size.width(), size.height()) / 2.;
	return {
		.geometry = { corner / 100., size / 100. },
		.rotation = data.vrotation().v,
		.radius = data.vradius().value_or_empty(),
	};
}

[[nodiscard]] TextWithEntities StripLinks(TextWithEntities text) {
	const auto link = [&](const EntityInText &entity) {
		return (entity.type() == EntityType::CustomUrl)
			|| (entity.type() == EntityType::Url)
			|| (entity.type() == EntityType::Mention)
			|| (entity.type() == EntityType::Hashtag);
	};
	text.entities.erase(
		ranges::remove_if(text.entities, link),
		text.entities.end());
	return text;
}

[[nodiscard]] auto ParseLocation(const MTPMediaArea &area)
-> std::optional<StoryLocation> {
	auto result = std::optional<StoryLocation>();
	area.match([&](const MTPDmediaAreaVenue &data) {
		data.vgeo().match([&](const MTPDgeoPoint &geo) {
			result.emplace(StoryLocation{
				.area = ParseArea(data.vcoordinates()),
				.point = Data::LocationPoint(geo),
				.title = qs(data.vtitle()),
				.address = qs(data.vaddress()),
				.provider = qs(data.vprovider()),
				.venueId = qs(data.vvenue_id()),
				.venueType = qs(data.vvenue_type()),
			});
		}, [](const MTPDgeoPointEmpty &) {
		});
	}, [&](const MTPDmediaAreaGeoPoint &data) {
		data.vgeo().match([&](const MTPDgeoPoint &geo) {
			result.emplace(StoryLocation{
				.area = ParseArea(data.vcoordinates()),
				.point = Data::LocationPoint(geo),
			});
		}, [](const MTPDgeoPointEmpty &) {
		});
	}, [&](const MTPDmediaAreaSuggestedReaction &data) {
	}, [&](const MTPDmediaAreaChannelPost &data) {
	}, [&](const MTPDmediaAreaUrl &data) {
	}, [&](const MTPDmediaAreaWeather &data) {
	}, [&](const MTPDmediaAreaStarGift &data) {
	}, [&](const MTPDinputMediaAreaChannelPost &data) {
		LOG(("API Error: Unexpected inputMediaAreaChannelPost from API."));
	}, [&](const MTPDinputMediaAreaVenue &data) {
		LOG(("API Error: Unexpected inputMediaAreaVenue from API."));
	});
	return result;
}

[[nodiscard]] auto ParseSuggestedReaction(const MTPMediaArea &area)
-> std::optional<SuggestedReaction> {
	auto result = std::optional<SuggestedReaction>();
	area.match([&](const MTPDmediaAreaVenue &data) {
	}, [&](const MTPDmediaAreaGeoPoint &data) {
	}, [&](const MTPDmediaAreaSuggestedReaction &data) {
		result.emplace(SuggestedReaction{
			.area = ParseArea(data.vcoordinates()),
			.reaction = Data::ReactionFromMTP(data.vreaction()),
			.flipped = data.is_flipped(),
			.dark = data.is_dark(),
		});
	}, [&](const MTPDmediaAreaChannelPost &data) {
	}, [&](const MTPDmediaAreaUrl &data) {
	}, [&](const MTPDmediaAreaWeather &data) {
	}, [&](const MTPDmediaAreaStarGift &data) {
	}, [&](const MTPDinputMediaAreaChannelPost &data) {
		LOG(("API Error: Unexpected inputMediaAreaChannelPost from API."));
	}, [&](const MTPDinputMediaAreaVenue &data) {
		LOG(("API Error: Unexpected inputMediaAreaVenue from API."));
	});
	return result;
}

[[nodiscard]] auto ParseChannelPost(const MTPMediaArea &area)
-> std::optional<ChannelPost> {
	auto result = std::optional<ChannelPost>();
	area.match([&](const MTPDmediaAreaVenue &data) {
	}, [&](const MTPDmediaAreaGeoPoint &data) {
	}, [&](const MTPDmediaAreaSuggestedReaction &data) {
	}, [&](const MTPDmediaAreaChannelPost &data) {
		result.emplace(ChannelPost{
			.area = ParseArea(data.vcoordinates()),
			.itemId = FullMsgId(
				peerFromChannel(data.vchannel_id()),
				data.vmsg_id().v),
		});
	}, [&](const MTPDmediaAreaUrl &data) {
	}, [&](const MTPDmediaAreaWeather &data) {
	}, [&](const MTPDmediaAreaStarGift &data) {
	}, [&](const MTPDinputMediaAreaChannelPost &data) {
		LOG(("API Error: Unexpected inputMediaAreaChannelPost from API."));
	}, [&](const MTPDinputMediaAreaVenue &data) {
		LOG(("API Error: Unexpected inputMediaAreaVenue from API."));
	});
	return result;
}

[[nodiscard]] auto ParseUrlArea(const MTPMediaArea &area)
-> std::optional<UrlArea> {
	auto result = std::optional<UrlArea>();
	area.match([&](const MTPDmediaAreaVenue &data) {
	}, [&](const MTPDmediaAreaGeoPoint &data) {
	}, [&](const MTPDmediaAreaSuggestedReaction &data) {
	}, [&](const MTPDmediaAreaChannelPost &data) {
	}, [&](const MTPDmediaAreaUrl &data) {
		result.emplace(UrlArea{
			.area = ParseArea(data.vcoordinates()),
			.url = qs(data.vurl()),
		});
	}, [&](const MTPDmediaAreaWeather &data) {
	}, [&](const MTPDmediaAreaStarGift &data) {
		result.emplace(UrlArea{
			.area = ParseArea(data.vcoordinates()),
			.url = u"tg://nft?slug="_q + qs(data.vslug()),
		});
	}, [&](const MTPDinputMediaAreaChannelPost &data) {
		LOG(("API Error: Unexpected inputMediaAreaChannelPost from API."));
	}, [&](const MTPDinputMediaAreaVenue &data) {
		LOG(("API Error: Unexpected inputMediaAreaVenue from API."));
	});
	return result;
}

[[nodiscard]] auto ParseWeatherArea(const MTPMediaArea &area)
-> std::optional<WeatherArea> {
	auto result = std::optional<WeatherArea>();
	area.match([&](const MTPDmediaAreaVenue &data) {
	}, [&](const MTPDmediaAreaGeoPoint &data) {
	}, [&](const MTPDmediaAreaSuggestedReaction &data) {
	}, [&](const MTPDmediaAreaChannelPost &data) {
	}, [&](const MTPDmediaAreaUrl &data) {
	}, [&](const MTPDmediaAreaWeather &data) {
		result.emplace(WeatherArea{
			.area = ParseArea(data.vcoordinates()),
			.emoji = qs(data.vemoji()),
			.color = Ui::Color32FromSerialized(data.vcolor().v),
			.millicelsius = int(1000. * std::clamp(
				data.vtemperature_c().v,
				-274.,
				1'000'000.)),
		});
	}, [&](const MTPDmediaAreaStarGift &data) {
	}, [&](const MTPDinputMediaAreaChannelPost &data) {
		LOG(("API Error: Unexpected inputMediaAreaChannelPost from API."));
	}, [&](const MTPDinputMediaAreaVenue &data) {
		LOG(("API Error: Unexpected inputMediaAreaVenue from API."));
	});
	return result;
}

[[nodiscard]] PeerData *RepostSourcePeer(
		not_null<Session*> owner,
		const MTPDstoryItem &data) {
	if (const auto forwarded = data.vfwd_from()) {
		if (const auto from = forwarded->data().vfrom()) {
			return owner->peer(peerFromMTP(*from));
		}
	}
	return nullptr;
}

[[nodiscard]] QString RepostSourceName(const MTPDstoryItem &data) {
	if (const auto forwarded = data.vfwd_from()) {
		return qs(forwarded->data().vfrom_name().value_or_empty());
	}
	return {};
}

[[nodiscard]] StoryId RepostSourceId(const MTPDstoryItem &data) {
	if (const auto forwarded = data.vfwd_from()) {
		return forwarded->data().vstory_id().value_or_empty();
	}
	return {};
}

[[nodiscard]] bool RepostModified(const MTPDstoryItem &data) {
	if (const auto forwarded = data.vfwd_from()) {
		return forwarded->data().is_modified();
	}
	return false;
}

[[nodiscard]] PeerData *FromPeer(
		not_null<Session*> owner,
		const MTPDstoryItem &data) {
	if (const auto from = data.vfrom_id()) {
		return owner->peer(peerFromMTP(*from));
	}
	return nullptr;
}

} // namespace

Story::Story(
	StoryId id,
	not_null<PeerData*> peer,
	StoryMedia media,
	const MTPDstoryItem &data,
	TimeId now)
: _id(id)
, _peer(peer)
, _repostSourcePeer(RepostSourcePeer(&peer->owner(), data))
, _repostSourceName(RepostSourceName(data))
, _repostSourceId(RepostSourceId(data))
, _fromPeer(FromPeer(&peer->owner(), data))
, _date(data.vdate().v)
, _expires(data.vexpire_date().v)
, _repostModified(RepostModified(data)) {
	applyFields(std::move(media), data, now, true);
}

Session &Story::owner() const {
	return _peer->owner();
}

Main::Session &Story::session() const {
	return _peer->session();
}

not_null<PeerData*> Story::peer() const {
	return _peer;
}

StoryId Story::id() const {
	return _id;
}

bool Story::mine() const {
	return _peer->isSelf();
}

StoryIdDates Story::idDates() const {
	return { _id, _date, _expires };
}

FullStoryId Story::fullId() const {
	return { _peer->id, _id };
}

TimeId Story::date() const {
	return _date;
}

TimeId Story::expires() const {
	return _expires;
}

bool Story::expired(TimeId now) const {
	return _expires <= (now ? now : base::unixtime::now());
}

bool Story::unsupported() const {
	return v::is_null(_media.data);
}

const StoryMedia &Story::media() const {
	return _media;
}

PhotoData *Story::photo() const {
	const auto result = std::get_if<not_null<PhotoData*>>(&_media.data);
	return result ? result->get() : nullptr;
}

DocumentData *Story::document() const {
	const auto result = std::get_if<not_null<DocumentData*>>(&_media.data);
	return result ? result->get() : nullptr;
}

bool Story::hasReplyPreview() const {
	return v::match(_media.data, [](not_null<PhotoData*> photo) {
		return !photo->isNull();
	}, [](not_null<DocumentData*> document) {
		return document->hasThumbnail();
	}, [](v::null_t) {
		return false;
	});
}

Image *Story::replyPreview() const {
	return v::match(_media.data, [&](not_null<PhotoData*> photo) {
		return photo->getReplyPreview(fullId(), _peer, false);
	}, [&](not_null<DocumentData*> document) {
		return document->getReplyPreview(fullId(), _peer, false);
	}, [](v::null_t) {
		return (Image*)nullptr;
	});
}

TextWithEntities Story::inReplyText() const {
	const auto type = tr::lng_in_dlg_story(tr::now);
	return _caption.text.isEmpty()
		? Ui::Text::Colorized(type)
		: tr::lng_dialogs_text_media(
			tr::now,
			lt_media_part,
			tr::lng_dialogs_text_media_wrapped(
				tr::now,
				lt_media,
				Ui::Text::Colorized(type),
				Ui::Text::WithEntities),
			lt_caption,
			_caption,
			Ui::Text::WithEntities);
}

void Story::setPinnedToTop(bool pinned) {
	if (_pinnedToTop != pinned) {
		_pinnedToTop = pinned;
		if (const auto item = _peer->owner().stories().lookupItem(this)) {
			item->setIsPinned(pinned);
		}
	}
}

bool Story::pinnedToTop() const {
	return _pinnedToTop;
}

void Story::setInProfile(bool value) {
	if (_inProfile != value) {
		_inProfile = value;
		if (const auto item = _peer->owner().stories().lookupItem(this)) {
			item->setStoryInProfile(value);
		}
	}
}

bool Story::inProfile() const {
	return _inProfile;
}

StoryPrivacy Story::privacy() const {
	return _privacyPublic
		? StoryPrivacy::Public
		: _privacyCloseFriends
		? StoryPrivacy::CloseFriends
		: _privacyContacts
		? StoryPrivacy::Contacts
		: _privacySelectedContacts
		? StoryPrivacy::SelectedContacts
		: StoryPrivacy::Other;
}

bool Story::forbidsForward() const {
	return _noForwards;
}

bool Story::edited() const {
	return _edited;
}

bool Story::out() const {
	return _out;
}

bool Story::canDownloadIfPremium() const {
	return !forbidsForward() || _peer->isSelf();
}

bool Story::canDownloadChecked() const {
	return _peer->isSelf()
		|| (canDownloadIfPremium() && _peer->session().premium());
}

bool Story::canShare() const {
	return _privacyPublic
		&& !forbidsForward()
		&& (inProfile() || !expired());
}

bool Story::canDelete() const {
	return _peer->canDeleteStories() || (out() && _peer->canPostStories());
}

bool Story::canReport() const {
	return !_peer->isSelf();
}

bool Story::hasDirectLink() const {
	if (!_privacyPublic || (!_inProfile && expired())) {
		return false;
	}
	return !_peer->username().isEmpty();
}

Data::SendError Story::errorTextForForward(
		not_null<Thread*> to) const {
	const auto peer = to->peer();
	const auto holdsPhoto = v::is<not_null<PhotoData*>>(_media.data);
	const auto first = holdsPhoto
		? ChatRestriction::SendPhotos
		: ChatRestriction::SendVideos;
	const auto second = holdsPhoto
		? ChatRestriction::SendVideos
		: ChatRestriction::SendPhotos;
	if (const auto one = Data::RestrictionError(peer, first)) {
		return one;
	} else if (const auto two = Data::RestrictionError(peer, second)) {
		return two;
	} else if (!Data::CanSend(to, first, false)
		|| !Data::CanSend(to, second, false)) {
		return tr::lng_forward_cant(tr::now);
	}
	return {};
}

void Story::setCaption(TextWithEntities &&caption) {
	_caption = std::move(caption);
}

const TextWithEntities &Story::caption() const {
	static const auto empty = TextWithEntities();
	return unsupported() ? empty : _caption;
}

Data::ReactionId Story::sentReactionId() const {
	return _sentReactionId;
}

void Story::setReactionId(Data::ReactionId id) {
	if (_sentReactionId != id) {
		const auto wasEmpty = _sentReactionId.empty();
		changeSuggestedReactionCount(_sentReactionId, -1);
		_sentReactionId = id;
		changeSuggestedReactionCount(id, 1);

		if (_views.known && _sentReactionId.empty() != wasEmpty) {
			const auto delta = wasEmpty ? 1 : -1;
			if (_views.reactions + delta >= 0) {
				_views.reactions += delta;
			}
		}
		session().changes().storyUpdated(this, UpdateFlag::Reaction);
	}
}

void Story::changeSuggestedReactionCount(Data::ReactionId id, int delta) {
	if (id.empty() || !_peer->isChannel()) {
		return;
	}
	for (auto &suggested : _suggestedReactions) {
		if (suggested.reaction == id && suggested.count + delta >= 0) {
			suggested.count += delta;
		}
	}
}

const std::vector<not_null<PeerData*>> &Story::recentViewers() const {
	return _recentViewers;
}

const StoryViews &Story::viewsList() const {
	return _views;
}

const StoryViews &Story::channelReactionsList() const {
	return _channelReactions;
}

int Story::interactions() const {
	return _views.total;
}

int Story::views() const {
	return _views.views;
}

int Story::forwards() const {
	return _views.forwards;
}

int Story::reactions() const {
	return _views.reactions;
}

void Story::applyViewsSlice(
		const QString &offset,
		const StoryViews &slice) {
	const auto changed = (_views.reactions != slice.reactions)
		|| (_views.views != slice.views)
		|| (_views.forwards != slice.forwards)
		|| (_views.total != slice.total);
	_views.reactions = slice.reactions;
	_views.forwards = slice.forwards;
	_views.views = slice.views;
	_views.total = slice.total;
	_views.known = true;
	if (offset.isEmpty()) {
		_views = slice;
		if (!_channelReactions.total) {
			_channelReactions.total = _views.reactions + _views.forwards;
		}
	} else if (_views.nextOffset == offset) {
		_views.list.insert(
			end(_views.list),
			begin(slice.list),
			end(slice.list));
		_views.nextOffset = slice.nextOffset;
		if (_views.nextOffset.isEmpty()) {
			_views.total = int(_views.list.size());
			_views.reactions = _views.total
				- ranges::count(
					_views.list,
					Data::ReactionId(),
					&StoryView::reaction);
			_views.forwards = _views.total
				- ranges::count(
					_views.list,
					0,
					[](const StoryView &view) {
						return view.repostId
							? view.repostId
							: view.forwardId.bare;
					});
		}
	}
	const auto known = int(_views.list.size());
	if (known >= _recentViewers.size()) {
		const auto take = std::min(known, kRecentViewersMax);
		auto viewers = _views.list
			| ranges::views::take(take)
			| ranges::views::transform(&StoryView::peer)
			| ranges::to_vector;
		if (_recentViewers != viewers) {
			_recentViewers = std::move(viewers);
			if (!changed) {
				// Count not changed, but list of recent viewers changed.
				_peer->session().changes().storyUpdated(
					this,
					UpdateFlag::ViewsChanged);
			}
		}
	}
	if (changed) {
		_peer->session().changes().storyUpdated(
			this,
			UpdateFlag::ViewsChanged);
	}
}

void Story::applyChannelReactionsSlice(
		const QString &offset,
		const StoryViews &slice) {
	const auto changed = (_channelReactions.reactions != slice.reactions)
		|| (_channelReactions.total != slice.total);
	_channelReactions.reactions = slice.reactions;
	_channelReactions.total = slice.total;
	_channelReactions.known = true;
	if (offset.isEmpty()) {
		_channelReactions = slice;
	} else if (_channelReactions.nextOffset == offset) {
		_channelReactions.list.insert(
			end(_channelReactions.list),
			begin(slice.list),
			end(slice.list));
		_channelReactions.nextOffset = slice.nextOffset;
		if (_channelReactions.nextOffset.isEmpty()) {
			_channelReactions.total = int(_channelReactions.list.size());
		}
	}
	if (changed) {
		_peer->session().changes().storyUpdated(
			this,
			UpdateFlag::ViewsChanged);
	}
}

const std::vector<StoryLocation> &Story::locations() const {
	return _locations;
}

const std::vector<SuggestedReaction> &Story::suggestedReactions() const {
	return _suggestedReactions;
}

const std::vector<ChannelPost> &Story::channelPosts() const {
	return _channelPosts;
}

const std::vector<UrlArea> &Story::urlAreas() const {
	return _urlAreas;
}

const std::vector<WeatherArea> &Story::weatherAreas() const {
	return _weatherAreas;
}

void Story::applyChanges(
		StoryMedia media,
		const MTPDstoryItem &data,
		TimeId now) {
	applyFields(std::move(media), data, now, false);
}

Story::ViewsCounts Story::parseViewsCounts(
		const MTPDstoryViews &data,
		const Data::ReactionId &mine) {
	auto result = ViewsCounts{
		.views = data.vviews_count().v,
		.forwards = data.vforwards_count().value_or_empty(),
		.reactions = data.vreactions_count().value_or_empty(),
	};
	if (const auto list = data.vrecent_viewers()) {
		result.viewers.reserve(list->v.size());
		auto &owner = _peer->owner();
		auto &&cut = list->v
			| ranges::views::take(kRecentViewersMax);
		for (const auto &id : cut) {
			result.viewers.push_back(owner.peer(peerFromUser(id)));
		}
	}
	auto total = 0;
	if (const auto list = data.vreactions()
		; list && _peer->isChannel()) {
		result.reactionsCounts.reserve(list->v.size());
		for (const auto &reaction : list->v) {
			const auto &data = reaction.data();
			const auto id = Data::ReactionFromMTP(data.vreaction());
			const auto count = data.vcount().v;
			result.reactionsCounts[id] = count;
			total += count;
		}
	}
	if (!mine.empty()) {
		if (auto &count = result.reactionsCounts[mine]; !count) {
			count = 1;
			++total;
		}
	}
	if (result.reactions < total) {
		result.reactions = total;
	}
	return result;
}

void Story::applyFields(
		StoryMedia media,
		const MTPDstoryItem &data,
		TimeId now,
		bool initial) {
	_lastUpdateTime = now;

	const auto reaction = data.is_min()
		? _sentReactionId
		: data.vsent_reaction()
		? Data::ReactionFromMTP(*data.vsent_reaction())
		: Data::ReactionId();
	const auto inProfile = data.is_pinned();
	const auto edited = data.is_edited();
	const auto privacy = data.is_public()
		? StoryPrivacy::Public
		: data.is_close_friends()
		? StoryPrivacy::CloseFriends
		: data.is_contacts()
		? StoryPrivacy::Contacts
		: data.is_selected_contacts()
		? StoryPrivacy::SelectedContacts
		: StoryPrivacy::Other;
	const auto noForwards = data.is_noforwards();
	const auto out = data.is_min() ? _out : data.is_out();
	auto caption = TextWithEntities{
		data.vcaption().value_or_empty(),
		Api::EntitiesFromMTP(
			&owner().session(),
			data.ventities().value_or_empty()),
	};
	if (const auto user = _peer->asUser()) {
		if (!user->isVerified() && !user->isPremium()) {
			caption = StripLinks(std::move(caption));
		}
	}
	auto counts = ViewsCounts();
	auto viewsKnown = _views.known;
	if (const auto info = data.vviews()) {
		counts = parseViewsCounts(info->data(), reaction);
		viewsKnown = true;
	} else {
		counts.views = _views.total;
		counts.forwards = _views.forwards;
		counts.reactions = _views.reactions;
		counts.viewers = _recentViewers;
		for (const auto &suggested : _suggestedReactions) {
			if (const auto count = suggested.count) {
				counts.reactionsCounts[suggested.reaction] = count;
			}
		}
	}
	auto locations = std::vector<StoryLocation>();
	auto suggestedReactions = std::vector<SuggestedReaction>();
	auto channelPosts = std::vector<ChannelPost>();
	auto urlAreas = std::vector<UrlArea>();
	auto weatherAreas = std::vector<WeatherArea>();
	if (const auto areas = data.vmedia_areas()) {
		for (const auto &area : areas->v) {
			if (const auto location = ParseLocation(area)) {
				locations.push_back(*location);
			} else if (auto reaction = ParseSuggestedReaction(area)) {
				const auto i = counts.reactionsCounts.find(
					reaction->reaction);
				if (i != end(counts.reactionsCounts)) {
					reaction->count = i->second;
				}
				suggestedReactions.push_back(*reaction);
			} else if (auto post = ParseChannelPost(area)) {
				channelPosts.push_back(*post);
			} else if (auto url = ParseUrlArea(area)) {
				urlAreas.push_back(*url);
			} else if (auto weather = ParseWeatherArea(area)) {
				weatherAreas.push_back(*weather);
			}
		}
	}

	const auto inProfileChanged = (_inProfile != inProfile);
	const auto editedChanged = (_edited != edited);
	const auto mediaChanged = (_media != media);
	const auto captionChanged = (_caption != caption);
	const auto locationsChanged = (_locations != locations);
	const auto suggestedReactionsChanged
		= (_suggestedReactions != suggestedReactions);
	const auto channelPostsChanged = (_channelPosts != channelPosts);
	const auto urlAreasChanged = (_urlAreas != urlAreas);
	const auto weatherAreasChanged = (_weatherAreas != weatherAreas);
	const auto reactionChanged = (_sentReactionId != reaction);

	_out = out;
	_privacyPublic = (privacy == StoryPrivacy::Public);
	_privacyCloseFriends = (privacy == StoryPrivacy::CloseFriends);
	_privacyContacts = (privacy == StoryPrivacy::Contacts);
	_privacySelectedContacts = (privacy == StoryPrivacy::SelectedContacts);
	_edited = edited;
	_inProfile = inProfile;
	_noForwards = noForwards;
	if (mediaChanged) {
		_media = std::move(media);
	}
	if (captionChanged) {
		_caption = std::move(caption);
	}
	if (locationsChanged) {
		_locations = std::move(locations);
	}
	if (suggestedReactionsChanged) {
		_suggestedReactions = std::move(suggestedReactions);
	}
	if (channelPostsChanged) {
		_channelPosts = std::move(channelPosts);
	}
	if (urlAreasChanged) {
		_urlAreas = std::move(urlAreas);
	}
	if (weatherAreasChanged) {
		_weatherAreas = std::move(weatherAreas);
	}
	if (reactionChanged) {
		_sentReactionId = reaction;
	}
	updateViewsCounts(std::move(counts), viewsKnown, initial);

	const auto changed = editedChanged
		|| captionChanged
		|| mediaChanged
		|| locationsChanged
		|| channelPostsChanged
		|| urlAreasChanged
		|| weatherAreasChanged;
	const auto reactionsChanged = reactionChanged
		|| suggestedReactionsChanged;
	if (!initial && (changed || reactionsChanged)) {
		_peer->session().changes().storyUpdated(this, UpdateFlag()
			| (changed ? UpdateFlag::Edited : UpdateFlag())
			| (reactionsChanged ? UpdateFlag::Reaction : UpdateFlag()));
	}
	if (!initial && (captionChanged || mediaChanged)) {
		if (const auto item = _peer->owner().stories().lookupItem(this)) {
			item->applyChanges(this);
		}
		_peer->owner().refreshStoryItemViews(fullId());
	}
	if (inProfileChanged) {
		_peer->owner().stories().savedStateChanged(this);
	}
}

void Story::updateViewsCounts(ViewsCounts &&counts, bool known, bool initial) {
	const auto total = _views.total
		? _views.total
		: (counts.views + counts.forwards);
	const auto viewsChanged = (_views.total != total)
		|| (_views.forwards != counts.forwards)
		|| (_views.reactions != counts.reactions)
		|| (_recentViewers != counts.viewers);
	if (_views.reactions != counts.reactions
		|| _views.forwards != counts.forwards
		|| _views.total != total
		|| _views.known != known) {
		_views = StoryViews{
			.reactions = counts.reactions,
			.forwards = counts.forwards,
			.views = counts.views,
			.total = total,
			.known = known,
		};
		if (!_channelReactions.total) {
			_channelReactions.total = _views.reactions + _views.forwards;
		}
	}
	if (viewsChanged) {
		_recentViewers = std::move(counts.viewers);
		_peer->session().changes().storyUpdated(
			this,
			UpdateFlag::ViewsChanged);
	}
}

void Story::applyViewsCounts(const MTPDstoryViews &data) {
	auto counts = parseViewsCounts(data, _sentReactionId);
	auto suggestedCountsChanged = false;
	for (auto &suggested : _suggestedReactions) {
		const auto i = counts.reactionsCounts.find(suggested.reaction);
		const auto v = (i != end(counts.reactionsCounts)) ? i->second : 0;
		if (suggested.count != v) {
			suggested.count = v;
			suggestedCountsChanged = true;
		}
	}
	updateViewsCounts(std::move(counts), true, false);
	if (suggestedCountsChanged) {
		_peer->session().changes().storyUpdated(this, UpdateFlag::Reaction);
	}
}

TimeId Story::lastUpdateTime() const {
	return _lastUpdateTime;
}

bool Story::repost() const {
	return _repostSourcePeer || !_repostSourceName.isEmpty();
}

bool Story::repostModified() const {
	return _repostModified;
}

PeerData *Story::repostSourcePeer() const {
	return _repostSourcePeer;
}

QString Story::repostSourceName() const {
	return _repostSourceName;
}

StoryId Story::repostSourceId() const {
	return _repostSourceId;
}

const base::flat_set<int> &Story::albumIds() const {
	return _albumIds;
}

void Story::setAlbumIds(base::flat_set<int> ids) {
	_albumIds = std::move(ids);
}

PeerData *Story::fromPeer() const {
	return _fromPeer;
}

StoryPreload::StoryPreload(not_null<Story*> story, Fn<void()> done)
: _story(story) {
	if (const auto photo = _story->photo()) {
		if (PhotoPreload::Should(photo, story->peer())) {
			_task = std::make_unique<PhotoPreload>(
				photo,
				story->fullId(),
				std::move(done));
		} else {
			done();
		}
	} else if (const auto video = _story->document()) {
		if (VideoPreload::Can(video)) {
			_task = std::make_unique<VideoPreload>(
				video,
				story->fullId(),
				std::move(done));
		} else {
			done();
		}
	} else {
		done();
	}
}

StoryPreload::~StoryPreload() = default;

FullStoryId StoryPreload::id() const {
	return _story->fullId();
}

not_null<Story*> StoryPreload::story() const {
	return _story;
}

} // namespace Data
