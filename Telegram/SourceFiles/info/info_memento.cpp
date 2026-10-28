/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/info_memento.h"

#include "info/global_media/info_global_media_widget.h"
#include "info/profile/info_profile_widget.h"
#include "info/profile/tabs/adapters/info_profile_tab_media.h"
#include "info/profile/tabs/info_profile_tabs_host.h"
#include "info/media/info_media_widget.h"
#include "info/members/info_members_widget.h"
#include "info/common_groups/info_common_groups_widget.h"
#include "info/saved/info_saved_sublists_widget.h"
#include "info/settings/info_settings_widget.h"
#include "info/similar_peers/info_similar_peers_widget.h"
#include "info/reactions_list/info_reactions_list_widget.h"
#include "info/requests_list/info_requests_list_widget.h"
#include "info/community/info_community_widget.h"
#include "info/community_requests/info_community_requests_widget.h"
#include "info/peer_gifts/info_peer_gifts_widget.h"
#include "info/polls/info_polls_list_widget.h"
#include "info/polls/info_polls_results_widget.h"
#include "info/info_section_widget.h"
#include "info/info_layer_widget.h"
#include "info/info_controller.h"
#include "ui/ui_utility.h"
#include "boxes/peer_list_box.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_forum_topic.h"
#include "data/data_saved_messages.h"
#include "data/data_saved_sublist.h"
#include "data/data_session.h"
#include "history/view/history_view_chat_section.h"
#include "main/main_session.h"

namespace Info {
namespace {

[[nodiscard]] History *DyingThreadHistory(
		not_null<ContentMemento*> content) {
	if (const auto topic = content->topic()) {
		return topic->dying() ? topic->owningHistory().get() : nullptr;
	} else if (const auto sublist = content->sublist()) {
		const auto chat = sublist->parentChat();
		const auto dying = chat && (chat->monoforum() != sublist->parent());
		return dying ? sublist->owningHistory().get() : nullptr;
	}
	return nullptr;
}

} // namespace

Memento::Memento(not_null<PeerData*> peer)
: Memento(peer, [&] {
	const auto channel = peer->asChannel();
	return (channel && channel->isCommunity())
		? Section::Type::Community
		: Section::Type::Profile;
}()) {
}

Memento::Memento(not_null<PeerData*> peer, Section section)
: Memento(DefaultStack(peer, section)) {
}

Memento::Memento(not_null<Data::ForumTopic*> topic)
: Memento(topic, Section::Type::Profile) {
}

Memento::Memento(not_null<Data::ForumTopic*> topic, Section section)
: Memento(DefaultStack(topic, section)) {
}

Memento::Memento(not_null<Data::SavedSublist*> sublist)
: Memento(sublist, Section::Type::Profile) {
}

Memento::Memento(not_null<Data::SavedSublist*> sublist, Section section)
: Memento(DefaultStack(sublist, section)) {
}

Memento::Memento(not_null<Data::SavedMessages*> savedMessages)
: Memento(DefaultStack(savedMessages)) {
}

Memento::Memento(Settings::Tag settings, Section section)
: Memento(DefaultStack(settings, section)) {
}

Memento::Memento(not_null<PollData*> poll, FullMsgId contextId)
: Memento(DefaultStack(poll, contextId)) {
}

Memento::Memento(
	std::shared_ptr<Api::WhoReadList> whoReadIds,
	FullMsgId contextId,
	Data::ReactionId selected)
: Memento(DefaultStack(std::move(whoReadIds), contextId, selected)) {
}

Memento::Memento(
	std::vector<std::shared_ptr<ContentMemento>> stack,
	std::unique_ptr<MementoThreadLifetime> threadLifetime)
: _stack(std::move(stack))
, _threadLifetime(std::move(threadLifetime)) {
	if (_threadLifetime) {
		_threadLifetime->memento = this;
	}
	auto topics = base::flat_set<not_null<Data::ForumTopic*>>();
	auto sublists = base::flat_set<not_null<Data::SavedSublist*>>();
	for (auto &entry : _stack) {
		if (const auto topic = entry->topic()) {
			topics.emplace(topic);
		} else if (const auto sublist = entry->sublist()) {
			sublists.emplace(sublist);
		}
	}
	for (const auto &topic : topics) {
		topic->destroyed(
		) | rpl::on_next([=] {
			removeThread(topic);
		}, _lifetime);
	}
	for (const auto &sublist : sublists) {
		sublist->destroyed(
		) | rpl::on_next([=] {
			removeThread(sublist);
		}, _lifetime);
	}
}

std::unique_ptr<MementoThreadLifetime> Memento::WatchThread(
		not_null<Data::Thread*> thread,
		rpl::producer<> destroyed) {
	auto result = std::make_unique<MementoThreadLifetime>();
	const auto raw = result.get();

	// WHY: made when the content is shown, before any section opened from
	// it subscribes, so the saved memento drops the thread before such a
	// section's destroyed() handler goes back to it.
	std::move(
		destroyed
	) | rpl::on_next([=] {
		if (const auto memento = raw->memento) {
			memento->removeThread(thread);
		}
	}, raw->lifetime);
	return result;
}

void Memento::removeThread(not_null<Data::Thread*> thread) {
	const auto was = _stack.size();
	for (auto i = begin(_stack); i != end(_stack);) {
		const auto content = i->get();
		if ((content->topic() == thread) || (content->sublist() == thread)) {
			i = _stack.erase(i);
		} else {
			++i;
		}
	}
	if (_stack.empty()) {
		_removeRequests.fire({});
	} else if (_stack.size() != was) {
		_prunedThreads.push_back(base::make_weak(thread));
	}
}

bool Memento::hasDyingContent() const {
	return ranges::any_of(_stack, [](const auto &content) {
		return DyingThreadHistory(content.get()) != nullptr;
	});
}

History *Memento::removeDyingContent() {
	auto result = (History*)nullptr;
	for (auto i = begin(_stack); i != end(_stack);) {
		if (const auto history = DyingThreadHistory(i->get())) {
			result = history;
			i = _stack.erase(i);
		} else {
			++i;
		}
	}
	return _stack.empty() ? result : nullptr;
}

std::vector<std::shared_ptr<ContentMemento>> Memento::DefaultStack(
		not_null<PeerData*> peer,
		Section section) {
	auto result = std::vector<std::shared_ptr<ContentMemento>>();
	result.push_back(DefaultContent(peer, section));
	return result;
}

std::vector<std::shared_ptr<ContentMemento>> Memento::DefaultStack(
		not_null<Data::ForumTopic*> topic,
		Section section) {
	auto result = std::vector<std::shared_ptr<ContentMemento>>();
	result.push_back(DefaultContent(topic, section));
	return result;
}

std::vector<std::shared_ptr<ContentMemento>> Memento::DefaultStack(
		not_null<Data::SavedSublist*> sublist,
		Section section) {
	auto result = std::vector<std::shared_ptr<ContentMemento>>();
	result.push_back(DefaultContent(sublist, section));
	return result;
}

std::vector<std::shared_ptr<ContentMemento>> Memento::DefaultStack(
		not_null<Data::SavedMessages*> savedMessages) {
	auto result = std::vector<std::shared_ptr<ContentMemento>>();
	result.push_back(std::make_shared<Profile::Memento>(savedMessages));
	return result;
}

std::vector<std::shared_ptr<ContentMemento>> Memento::DefaultStack(
		Settings::Tag settings,
		Section section) {
	auto result = std::vector<std::shared_ptr<ContentMemento>>();
	result.push_back(std::make_shared<Settings::Memento>(
		settings.self,
		section.settingsType()));
	return result;
}

std::vector<std::shared_ptr<ContentMemento>> Memento::DefaultStack(
		not_null<PollData*> poll,
		FullMsgId contextId) {
	auto result = std::vector<std::shared_ptr<ContentMemento>>();
	result.push_back(std::make_shared<Polls::Memento>(poll, contextId));
	return result;
}

std::vector<std::shared_ptr<ContentMemento>> Memento::DefaultStack(
		std::shared_ptr<Api::WhoReadList> whoReadIds,
		FullMsgId contextId,
		Data::ReactionId selected) {
	auto result = std::vector<std::shared_ptr<ContentMemento>>();
	result.push_back(std::make_shared<ReactionsList::Memento>(
		std::move(whoReadIds),
		contextId,
		selected));
	return result;
}

Section Memento::DefaultSection(not_null<PeerData*> peer) {
	if (peer->savedSublistsInfo()) {
		return Section(Section::Type::SavedSublists);
	} else if (peer->sharedMediaInfo()) {
		return Section(Section::MediaType::Photo);
	}
	return Section(Section::Type::Profile);
}

std::shared_ptr<Memento> Memento::Default(not_null<PeerData*> peer) {
	if (peer->savedSublistsInfo() && Profile::UseProfileMediaTabs()) {
		return std::make_shared<Memento>(&peer->owner().savedMessages());
	}
	return std::make_shared<Memento>(peer, DefaultSection(peer));
}

std::shared_ptr<Memento> Memento::ProfileWithMediaTab(
		not_null<PeerData*> peer,
		Storage::SharedMediaType mediaTab) {
	// Same section the plain ?profile link opens on this peer.
	auto result = std::make_shared<Memento>(peer);
	const auto profile = dynamic_cast<Profile::Memento*>(
		result->content().get());
	if (profile) {
		auto tabs = std::make_unique<Profile::TabsState>();
		// The polls tab descriptor uses its own literal id.
		tabs->activeId = (mediaTab == Storage::SharedMediaType::Poll)
			? u"media:polls"_q
			: Profile::MediaTabId(mediaTab);
		profile->setTabsState(std::move(tabs));
	}
	return result;
}

std::shared_ptr<ContentMemento> Memento::DefaultContent(
		not_null<PeerData*> peer,
		Section section) {
	if (auto to = peer->migrateTo()) {
		peer = to;
	}
	auto migrated = peer->migrateFrom();
	auto migratedPeerId = migrated ? migrated->id : PeerId(0);

	switch (section.type()) {
	case Section::Type::Profile:
		return std::make_shared<Profile::Memento>(
			peer,
			migratedPeerId);
	case Section::Type::Media:
		if (section.mediaType() == Storage::SharedMediaType::Poll) {
			return std::make_shared<Polls::ListMemento>(
				peer,
				migratedPeerId);
		}
		return std::make_shared<Media::Memento>(
			peer,
			migratedPeerId,
			section.mediaType());
	case Section::Type::GlobalMedia:
		return std::make_shared<GlobalMedia::Memento>(
			peer->asUser(),
			section.mediaType());
	case Section::Type::CommonGroups:
		return std::make_shared<CommonGroups::Memento>(peer->asUser());
	case Section::Type::SimilarPeers:
		return std::make_shared<SimilarPeers::Memento>(peer);
	case Section::Type::RequestsList:
		return std::make_shared<RequestsList::Memento>(peer);
	case Section::Type::Community:
		return std::make_shared<Community::Memento>(peer);
	case Section::Type::CommunityRequests:
		return std::make_shared<CommunityRequests::Memento>(peer);
	case Section::Type::SavedSublists:
		return std::make_shared<Saved::SublistsMemento>(&peer->session());
	case Section::Type::Members:
		return std::make_shared<Members::Memento>(
			peer,
			migratedPeerId);
	}
	Unexpected("Wrong section type in Info::Memento::DefaultContent()");
}

std::shared_ptr<ContentMemento> Memento::DefaultContent(
		not_null<Data::ForumTopic*> topic,
		Section section) {
	const auto peer = topic->peer();
	const auto migrated = peer->migrateFrom();
	const auto migratedPeerId = migrated ? migrated->id : PeerId(0);
	switch (section.type()) {
	case Section::Type::Profile:
		return std::make_shared<Profile::Memento>(topic);
	case Section::Type::Media:
		if (section.mediaType() == Storage::SharedMediaType::Poll) {
			return std::make_shared<Polls::ListMemento>(topic);
		}
		return std::make_shared<Media::Memento>(topic, section.mediaType());
	case Section::Type::Members:
		return std::make_shared<Members::Memento>(peer, migratedPeerId);
	}
	Unexpected("Wrong section type in Info::Memento::DefaultContent()");
}

std::shared_ptr<ContentMemento> Memento::DefaultContent(
		not_null<Data::SavedSublist*> sublist,
		Section section) {
	switch (section.type()) {
	case Section::Type::Profile:
		return std::make_shared<Profile::Memento>(sublist);
	case Section::Type::Media:
		if (section.mediaType() == Storage::SharedMediaType::Poll) {
			return std::make_shared<Polls::ListMemento>(sublist);
		}
		return std::make_shared<Media::Memento>(
			sublist,
			section.mediaType());
	}
	Unexpected("Wrong section type in Info::Memento::DefaultContent()");
}

object_ptr<Window::SectionWidget> Memento::createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) {
	// Back navigation inside a thread's teardown must not rebuild its Info.
	if (column == Window::Column::Third) {
		if (hasDyingContent()) {
			return nullptr;
		}
	} else if (const auto history = removeDyingContent()) {
		auto memento = HistoryView::ChatMemento(
			HistoryView::ChatViewId{ .history = history });
		return memento.createWidget(parent, controller, column, geometry);
	}
	auto wrap = (column == Window::Column::Third)
		? Wrap::Side
		: Wrap::Narrow;
	auto result = object_ptr<SectionWidget>(
		parent,
		controller,
		wrap,
		this);
	result->setGeometry(geometry);
	return result;
}

object_ptr<Ui::LayerWidget> Memento::createLayer(
		not_null<Window::SessionController*> controller,
		const QRect &geometry) {
	// A layer would leave the dying thread's section in the main column.
	const auto pruning = ranges::any_of(_prunedThreads, [](const auto &weak) {
		return !weak.empty();
	});
	if (!pruning
		&& !hasDyingContent()
		&& geometry.width() >= LayerWidget::MinimalSupportedWidth()) {
		return object_ptr<LayerWidget>(controller, this);
	}
	return nullptr;
}

std::vector<std::shared_ptr<ContentMemento>> Memento::takeStack() {
	return std::move(_stack);
}

Memento::~Memento() = default;

MoveMemento::MoveMemento(object_ptr<WrapWidget> content)
: _content(std::move(content)) {
	_content->hide();
	_content->setParent(nullptr);
}

object_ptr<Window::SectionWidget> MoveMemento::createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) {
	auto wrap = (column == Window::Column::Third)
		? Wrap::Side
		: Wrap::Narrow;
	auto result = object_ptr<SectionWidget>(
		parent,
		controller,
		wrap,
		this);
	result->setGeometry(geometry);
	return result;
}

object_ptr<Ui::LayerWidget> MoveMemento::createLayer(
		not_null<Window::SessionController*> controller,
		const QRect &geometry) {
	if (geometry.width() < LayerWidget::MinimalSupportedWidth()) {
		return nullptr;
	}
	return object_ptr<LayerWidget>(controller, this);
}

object_ptr<WrapWidget> MoveMemento::takeContent(
		QWidget *parent,
		Wrap wrap) {
	Ui::AttachParentChild(parent, _content);
	_content->setWrap(wrap);
	return std::move(_content);
}

} // namespace Info
