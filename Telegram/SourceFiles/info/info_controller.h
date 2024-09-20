/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_search_controller.h"
#include "window/window_session_controller.h"

namespace Data {
class ForumTopic;
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

namespace Info::Stories {

enum class Tab {
	Saved,
	Archive,
};

struct Tag {
	explicit Tag(not_null<PeerData*> peer, Tab tab = {})
	: peer(peer)
	, tab(tab) {
	}

	not_null<PeerData*> peer;
	Tab tab = {};
};

} // namespace Info::Stories

namespace Info::Statistics {

struct Tag {
	explicit Tag(
		not_null<PeerData*> peer,
		FullMsgId contextId,
		FullStoryId storyId)
	: peer(peer)
	, contextId(contextId)
	, storyId(storyId) {
	}

	not_null<PeerData*> peer;
	FullMsgId contextId;
	FullStoryId storyId;
};

} // namespace Info::Statistics

namespace Info {

class Key {
public:
	explicit Key(not_null<PeerData*> peer);
	explicit Key(not_null<Data::ForumTopic*> topic);
	Key(Settings::Tag settings);
	Key(Downloads::Tag downloads);
	Key(Stories::Tag stories);
	Key(Statistics::Tag statistics);
	Key(not_null<PollData*> poll, FullMsgId contextId);

	PeerData *peer() const;
	Data::ForumTopic *topic() const;
	UserData *settingsSelf() const;
	bool isDownloads() const;
	PeerData *storiesPeer() const;
	Stories::Tab storiesTab() const;
	PeerData *statisticsPeer() const;
	FullMsgId statisticsContextId() const;
	FullStoryId statisticsStoryId() const;
	PollData *poll() const;
	FullMsgId pollContextId() const;

private:
	struct PollKey {
		not_null<PollData*> poll;
		FullMsgId contextId;
	};
	std::variant<
		not_null<PeerData*>,
		not_null<Data::ForumTopic*>,
		Settings::Tag,
		Downloads::Tag,
		Stories::Tag,
		Statistics::Tag,
		PollKey> _value;

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
		CommonGroups,
		SimilarChannels,
		SavedSublists,
		PeerGifts,
		Members,
		Settings,
		Downloads,
		Stories,
		PollResults,
		Statistics,
		Boosts,
		ChannelEarn,
		BotEarn,
	};
	using SettingsType = ::Settings::Type;
	using MediaType = Storage::SharedMediaType;

	Section(Type type) : _type(type) {
		Expects(type != Type::Media && type != Type::Settings);
	}
	Section(MediaType mediaType)
	: _type(Type::Media)
	, _mediaType(mediaType) {
	}
	Section(SettingsType settingsType)
	: _type(Type::Settings)
	, _settingsType(settingsType) {
	}

	Type type() const {
		return _type;
	}
	MediaType mediaType() const {
		Expects(_type == Type::Media);

		return _mediaType;
	}
	SettingsType settingsType() const {
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
	[[nodiscard]] UserData *settingsSelf() const {
		return key().settingsSelf();
	}
	[[nodiscard]] bool isDownloads() const {
		return key().isDownloads();
	}
	[[nodiscard]] PeerData *storiesPeer() const {
		return key().storiesPeer();
	}
	[[nodiscard]] Stories::Tab storiesTab() const {
		return key().storiesTab();
	}
	[[nodiscard]] PeerData *statisticsPeer() const {
		return key().statisticsPeer();
	}
	[[nodiscard]] FullMsgId statisticsContextId() const {
		return key().statisticsContextId();
	}
	[[nodiscard]] FullStoryId statisticsStoryId() const {
		return key().statisticsStoryId();
	}
	[[nodiscard]] PollData *poll() const;
	[[nodiscard]] FullMsgId pollContextId() const {
		return key().pollContextId();
	}

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

	bool validateMementoPeer(
		not_null<ContentMemento*> memento) const;

	Wrap wrap() const;
	rpl::producer<Wrap> wrapValue() const;
	void setSection(not_null<ContentMemento*> memento);

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
