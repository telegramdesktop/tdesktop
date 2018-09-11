/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/variable.h>
#include "data/data_search_controller.h"
#include "window/window_controller.h"
#include "settings/settings_common.h"

namespace Ui {
class SearchFieldController;
} // namespace Ui

namespace Info {
namespace Settings {

struct Tag {
	explicit Tag(not_null<UserData*> self) : self(self) {
	}

	not_null<UserData*> self;
};

} // namespace Settings

class Key {
public:
	Key(not_null<PeerData*> peer);
	Key(not_null<Data::Feed*> feed);
	Key(Settings::Tag settings);

	PeerData *peer() const;
	Data::Feed *feed() const;
	UserData *settingsSelf() const;

private:
	base::variant<
		not_null<PeerData*>,
		not_null<Data::Feed*>,
		Settings::Tag> _value;

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
		Members,
		Channels,
		Settings,
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

class AbstractController : public Window::Navigation {
public:
	AbstractController(not_null<Window::Controller*> parent)
	: _parent(parent) {
	}

	virtual Key key() const = 0;
	virtual PeerData *migrated() const = 0;
	virtual Section section() const = 0;

	PeerId peerId() const {
		if (const auto peer = key().peer()) {
			return peer->id;
		}
		return PeerId(0);
	}
	PeerId migratedPeerId() const {
		if (auto peer = migrated()) {
			return peer->id;
		}
		return PeerId(0);
	}
	Data::Feed *feed() const {
		return key().feed();
	}
	UserData *settingsSelf() const {
		return key().settingsSelf();
	}

	virtual void setSearchEnabledByContent(bool enabled) {
	}
	virtual rpl::producer<SparseIdsMergedSlice> mediaSource(
		SparseIdsMergedSlice::UniversalMsgId aroundId,
		int limitBefore,
		int limitAfter) const;
	virtual rpl::producer<QString> mediaSourceQueryValue() const;

	void showSection(
		Window::SectionMemento &&memento,
		const Window::SectionShow &params = Window::SectionShow()) override;
	void showBackFromStack(
		const Window::SectionShow &params = Window::SectionShow()) override;
	not_null<Window::Controller*> parentController() override {
		return _parent;
	}

private:
	not_null<Window::Controller*> _parent;

};

class Controller : public AbstractController {
public:
	Controller(
		not_null<WrapWidget*> widget,
		not_null<Window::Controller*> window,
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
	bool takeSearchStartsFocused() {
		return base::take(_searchStartsFocused);
	}

	void setCanSaveChanges(rpl::producer<bool> can);
	rpl::producer<bool> canSaveChanges() const;
	bool canSaveChangesNow() const;

	void saveSearchState(not_null<ContentMemento*> memento);

	void showSection(
		Window::SectionMemento &&memento,
		const Window::SectionShow &params = Window::SectionShow()) override;
	void showBackFromStack(
		const Window::SectionShow &params = Window::SectionShow()) override;

	rpl::lifetime &lifetime() {
		return _lifetime;
	}

	~Controller();

private:
	using SearchQuery = Api::DelayedSearchController::Query;

	void updateSearchControllers(not_null<ContentMemento*> memento);
	SearchQuery produceSearchQuery(const QString &query) const;
	void setupMigrationViewer();

	not_null<WrapWidget*> _widget;
	Key _key;
	PeerData *_migrated = nullptr;
	rpl::variable<Wrap> _wrap;
	Section _section;

	std::unique_ptr<Ui::SearchFieldController> _searchFieldController;
	std::unique_ptr<Api::DelayedSearchController> _searchController;
	rpl::variable<bool> _seachEnabledByContent = false;
	rpl::variable<bool> _canSaveChanges = false;
	bool _searchStartsFocused = false;

	rpl::lifetime _lifetime;

};

} // namespace Info