/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_message_reaction_id.h"
#include "data/data_search_controller.h"
#include "info/peer_gifts/info_peer_gifts_common.h"
#include "info/statistics/info_statistics_tag.h"
#include "info/stories/info_stories_common.h"
#include "window/window_session_controller.h"

namespace Api {
struct WhoReadList;
} // namespace Api

namespace Data {
class ForumTopic;
class SavedSublist;
} // namespace Data

namespace Ui {
class SearchFieldController;
} // namespace Ui

namespace Info::Settings {

struct Tag {
	explicit Tag(not_null<UserData*> self) : self(self) {
	}

	not_null<UserData*> self;
};

} // namespace Info::Settings

namespace Info::Downloads {

struct Tag {
};

} // namespace Info::Downloads

namespace Info::GlobalMedia {

struct Tag {
	explicit Tag(not_null<UserData*> self) : self(self) {
	}

	not_null<UserData*> self;
};

} // namespace Info::GlobalMedia

namespace Info::BotStarRef {

enum class Type : uchar {
	Setup,
	Join,
};
struct Tag {
	Tag(not_null<PeerData*> peer, Type type) : peer(peer), type(type) {
	}

	not_null<PeerData*> peer;
	Type type = {};
};

} // namespace Info::BotStarRef

namespace Info {

class Key {
public:
	explicit Key(not_null<PeerData*> peer);
	explicit Key(not_null<Data::ForumTopic*> topic);
	explicit Key(not_null<Data::SavedSublist*> sublist);
	Key(Settings::Tag settings);
	Key(Downloads::Tag downloads);
	Key(Stories::Tag stories);
	Key(Statistics::Tag statistics);
	Key(PeerGifts::Tag gifts);
	Key(BotStarRef::Tag starref);
	Key(GlobalMedia::Tag global);
	Key(not_null<PollData*> poll, FullMsgId contextId);
	Key(
		std::shared_ptr<Api::WhoReadList> whoReadIds,
		Data::ReactionId selected,
		FullMsgId contextId);

	[[nodiscard]] PeerData *peer() const;
	[[nodiscard]] Data::ForumTopic *topic() const;
	[[nodiscard]] Data::SavedSublist *sublist() const;
	[[nodiscard]] UserData *settingsSelf() const;
	[[nodiscard]] bool isDownloads() const;
	[[nodiscard]] bool isGlobalMedia() const;
	[[nodiscard]] PeerData *storiesPeer() const;
	[[nodiscard]] int storiesAlbumId() const;
	[[nodiscard]] int storiesAddToAlbumId() const;
	[[nodiscard]] PeerData *giftsPeer() const;
	[[nodiscard]] int giftsCollectionId() const;
	[[nodiscard]] Statistics::Tag statisticsTag() const;
	[[nodiscard]] PeerData *starrefPeer() const;
	[[nodiscard]] BotStarRef::Type starrefType() const;
	[[nodiscard]] PollData *poll() const;
	[[nodiscard]] FullMsgId pollContextId() const;
	[[nodiscard]] auto reactionsWhoReadIds() const
		-> std::shared_ptr<Api::WhoReadList>;
	[[nodiscard]] Data::ReactionId reactionsSelected() const;
	[[nodiscard]] FullMsgId reactionsContextId() const;

private:
	struct PollKey {
		not_null<PollData*> poll;
		FullMsgId contextId;
	};
	struct ReactionsKey {
		std::shared_ptr<Api::WhoReadList> whoReadIds;
		Data::ReactionId selected;
		FullMsgId contextId;
	};
	std::variant<
		not_null<PeerData*>,
		not_null<Data::ForumTopic*>,
		not_null<Data::SavedSublist*>,
		Settings::Tag,
		Downloads::Tag,
		Stories::Tag,
		Statistics::Tag,
		PeerGifts::Tag,
		BotStarRef::Tag,
		GlobalMedia::Tag,
		PollKey,
		ReactionsKey> _value;

};

enum class Wrap;
class WrapWidget;
class Memento;
class ContentMemento;

class Section final {
public:
	enum class Type {
		Profile,
		Media,
		GlobalMedia,
		CommonGroups,
		SimilarPeers,
		RequestsList,
		ReactionsList,
		SavedSublists,
		PeerGifts,
		Members,
		Settings,
		Downloads,
		Stories,
		PollResults,
		Statistics,
		BotStarRef,
		Boosts,
		ChannelEarn,
		BotEarn,
	};
	using SettingsType = ::Settings::Type;
	using MediaType = Storage::SharedMediaType;

	Section(Type type) : _type(type) {
		Expects(type != Type::Media
			&& type != Type::GlobalMedia
			&& type != Type::Settings);
	}
	Section(MediaType mediaType, Type type = Type::Media)
	: _type(type)
	, _mediaType(mediaType) {
	}
	Section(SettingsType settingsType)
	: _type(Type::Settings)
	, _settingsType(settingsType) {
	}

	[[nodiscard]] Type type() const {
		return _type;
	}
	[[nodiscard]] MediaType mediaType() const {
		Expects(_type == Type::Media || _type == Type::GlobalMedia);

		return _mediaType;
	}
	[[nodiscard]] SettingsType settingsType() const {
		Expects(_type == Type::Settings);

		return _settingsType;
	}

private:
	Type _type;
	MediaType _mediaType = MediaType();
	SettingsType _settingsType = SettingsType();

};

class AbstractController : public Window::SessionNavigation {
public:
	AbstractController(not_null<Window::SessionController*> parent);

	[[nodiscard]] virtual Key key() const = 0;
	[[nodiscard]] virtual PeerData *migrated() const = 0;
	[[nodiscard]] virtual Section section() const = 0;

	[[nodiscard]] PeerData *peer() const;
	[[nodiscard]] PeerId migratedPeerId() const;
	[[nodiscard]] Data::ForumTopic *topic() const {
		return key().topic();
	}
	[[nodiscard]] Data::SavedSublist *sublist() const {
		return key().sublist();
	}
	[[nodiscard]] UserData *settingsSelf() const {
		return key().settingsSelf();
	}
	[[nodiscard]] bool isDownloads() const {
		return key().isDownloads();
	}
	[[nodiscard]] bool isGlobalMedia() const {
		return key().isGlobalMedia();
	}
	[[nodiscard]] PeerData *storiesPeer() const {
		return key().storiesPeer();
	}
	[[nodiscard]] int storiesAlbumId() const {
		return key().storiesAlbumId();
	}
	[[nodiscard]] int storiesAddToAlbumId() const {
		return key().storiesAddToAlbumId();
	}
	[[nodiscard]] PeerData *giftsPeer() const {
		return key().giftsPeer();
	}
	[[nodiscard]] int giftsCollectionId() const {
		return key().giftsCollectionId();
	}
	[[nodiscard]] Statistics::Tag statisticsTag() const {
		return key().statisticsTag();
	}
	[[nodiscard]] PeerData *starrefPeer() const {
		return key().starrefPeer();
	}
	[[nodiscard]] BotStarRef::Type starrefType() const {
		return key().starrefType();
	}
	[[nodiscard]] PollData *poll() const;
	[[nodiscard]] FullMsgId pollContextId() const {
		return key().pollContextId();
	}
	[[nodiscard]] auto reactionsWhoReadIds() const
		-> std::shared_ptr<Api::WhoReadList>;
	[[nodiscard]] Data::ReactionId reactionsSelected() const;
	[[nodiscard]] FullMsgId reactionsContextId() const;

	virtual void setSearchEnabledByContent(bool enabled) {
	}
	virtual rpl::producer<SparseIdsMergedSlice> mediaSource(
		SparseIdsMergedSlice::UniversalMsgId aroundId,
		int limitBefore,
		int limitAfter) const;
	virtual rpl::producer<QString> mediaSourceQueryValue() const;
	virtual rpl::producer<QString> searchQueryValue() const;

	void showSection(
		std::shared_ptr<Window::SectionMemento> memento,
		const Window::SectionShow &params = Window::SectionShow()) override;
	void showBackFromStack(
		const Window::SectionShow &params = Window::SectionShow()) override;

	void showPeerHistory(
		PeerId peerId,
		const Window::SectionShow &params = Window::SectionShow::Way::ClearStack,
		MsgId msgId = ShowAtUnreadMsgId) override;

	not_null<Window::SessionController*> parentController() override {
		return _parent;
	}

private:
	not_null<Window::SessionController*> _parent;

};

class Controller : public AbstractController {
public:
	Controller(
		not_null<WrapWidget*> widget,
		not_null<Window::SessionController*> window,
		not_null<ContentMemento*> memento);

	Key key() const override {
		return _key;
	}
	PeerData *migrated() const override {
		return _migrated;
	}
	Section section() const override {
		return _section;
	}

	void replaceKey(Key key);
	[[nodiscard]] bool validateMementoPeer(
		not_null<ContentMemento*> memento) const;

	[[nodiscard]] Wrap wrap() const;
	[[nodiscard]] rpl::producer<Wrap> wrapValue() const;
	[[nodiscard]] not_null<Ui::RpWidget*> wrapWidget() const;
	void setSection(not_null<ContentMemento*> memento);
	[[nodiscard]] bool hasBackButton() const;

	Ui::SearchFieldController *searchFieldController() const {
		return _searchFieldController.get();
	}
	void setSearchEnabledByContent(bool enabled) override {
		_seachEnabledByContent = enabled;
	}
	rpl::producer<bool> searchEnabledByContent() const;
	rpl::producer<SparseIdsMergedSlice> mediaSource(
		SparseIdsMergedSlice::UniversalMsgId aroundId,
		int limitBefore,
		int limitAfter) const override;
	rpl::producer<QString> mediaSourceQueryValue() const override;
	rpl::producer<QString> searchQueryValue() const override;
	bool takeSearchStartsFocused() {
		return base::take(_searchStartsFocused);
	}

	void saveSearchState(not_null<ContentMemento*> memento);

	void showSection(
		std::shared_ptr<Window::SectionMemento> memento,
		const Window::SectionShow &params = Window::SectionShow()) override;
	void showBackFromStack(
		const Window::SectionShow &params = Window::SectionShow()) override;

	void removeFromStack(const std::vector<Section> &sections) const;

	void takeStepData(not_null<Controller*> another);
	std::any &stepDataReference();

	rpl::lifetime &lifetime() {
		return _lifetime;
	}

	~Controller();

private:
	using SearchQuery = Api::DelayedSearchController::Query;

	void updateSearchControllers(not_null<ContentMemento*> memento);
	SearchQuery produceSearchQuery(const QString &query) const;
	void setupMigrationViewer();
	void setupTopicViewer();

	void replaceWith(std::shared_ptr<Memento> memento);

	not_null<WrapWidget*> _widget;
	Key _key;
	PeerData *_migrated = nullptr;
	rpl::variable<Wrap> _wrap;
	Section _section;

	std::unique_ptr<Ui::SearchFieldController> _searchFieldController;
	std::unique_ptr<Api::DelayedSearchController> _searchController;
	rpl::variable<bool> _seachEnabledByContent = false;
	bool _searchStartsFocused = false;

	// Data between sections based on steps.
	std::any _stepData;

	rpl::lifetime _lifetime;

};

} // namespace Info
