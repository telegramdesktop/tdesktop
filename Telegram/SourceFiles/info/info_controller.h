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

namespace Ui {
class SearchFieldController;
} // namespace Ui

namespace Info {

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
	};
	using MediaType = Storage::SharedMediaType;

	Section(Type type) : _type(type) {
		Expects(type != Type::Media);
	}
	Section(MediaType mediaType)
	: _type(Type::Media)
	, _mediaType(mediaType) {
	}

	Type type() const {
		return _type;
	}
	MediaType mediaType() const {
		Expects(_type == Type::Media);
		return _mediaType;
	}

private:
	Type _type;
	Storage::SharedMediaType _mediaType;

};

class AbstractController : public Window::Navigation {
public:
	AbstractController(not_null<Window::Controller*> parent)
	: _parent(parent) {
	}

	virtual not_null<PeerData*> peer() const = 0;
	virtual PeerData *migrated() const = 0;
	virtual Section section() const = 0;

	PeerId peerId() const {
		return peer()->id;
	}
	PeerId migratedPeerId() const {
		if (auto peer = migrated()) {
			return peer->id;
		}
		return PeerId(0);
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

	not_null<PeerData*> peer() const override {
		return _peer;
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
	not_null<PeerData*> _peer;
	PeerData *_migrated = nullptr;
	rpl::variable<Wrap> _wrap;
	Section _section;

	std::unique_ptr<Ui::SearchFieldController> _searchFieldController;
	std::unique_ptr<Api::DelayedSearchController> _searchController;
	rpl::variable<bool> _seachEnabledByContent = false;
	bool _searchStartsFocused = false;

	rpl::lifetime _lifetime;

};

} // namespace Info