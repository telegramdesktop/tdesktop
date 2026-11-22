/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "overview/overview_layout.h"

#include "core/ui_integration.h"
#include "data/data_changes.h"
#include "data/data_forum.h"
#include "data/data_forum_topic.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "overview/overview_layout_delegate.h"
#include "ui/text/text_options.h"

#include "styles/style_basic.h"

namespace Overview::Layout {
namespace {

struct VoiceSearchSource {
	PeerData *sender = nullptr;
	PeerData *chat = nullptr;
};

[[nodiscard]] VoiceSearchSource SearchSource(not_null<HistoryItem*> item) {
	const auto chat = item->history()->peer;
	const auto sender = item->from();
	if (chat->isSelf()) {
		if (const auto saved = item->history()->owner().peerLoaded(
				item->sublistPeerId())) {
			return saved->isUser()
				? VoiceSearchSource{ .sender = saved }
				: VoiceSearchSource{ .chat = saved };
		}
	}
	return chat->isUser()
		? VoiceSearchSource{ .sender = sender }
		: VoiceSearchSource{
			.sender = sender->asUser(),
			.chat = chat,
		};
}

[[nodiscard]] TextWithEntities SearchTitle(not_null<HistoryItem*> item) {
	const auto source = SearchSource(item);
	if (!source.chat) {
		return { source.sender->name() };
	}
	const auto forum = source.chat->forum();
	const auto rootId = item->topicRootId();
	const auto topic = forum && !forum->topicDeleted(rootId)
		? forum->topicFor(rootId)
		: nullptr;
	auto title = (topic && !topic->title().isEmpty())
		? topic->titleWithIconOrLogo()
		: TextWithEntities{ st::wrap_rtl(source.chat->name()) };
	if (const auto user = source.sender
		? source.sender->asUser()
		: nullptr) {
		const auto name = user->firstName.isEmpty()
			? user->name()
			: user->firstName;
		auto result = TextWithEntities{ st::wrap_rtl(name) };
		result.append(st::rtl() ? u" ← "_q : u" → "_q).append(
			std::move(title));
		return result;
	}
	return title;
}

} // namespace

void Voice::watchSearchName() {
	const auto source = SearchSource(parent());
	const auto refresh = [=] {
		updateSearchName();
		delegate()->repaintItem(this);
	};
	auto &changes = parent()->history()->session().changes();
	for (const auto peer : { source.sender, source.chat }) {
		if (peer) {
			changes.peerUpdates(
				peer,
				Data::PeerUpdate::Flag::Name
			) | rpl::on_next(refresh, _searchNameLifetime);
		}
	}
	const auto forum = source.chat ? source.chat->forum() : nullptr;
	const auto rootId = parent()->topicRootId();
	if (!forum || !rootId) {
		return;
	}
	using Flag = Data::TopicUpdate::Flag;
	changes.topicUpdates(
		Flag::Title | Flag::IconId | Flag::ColorId | Flag::Destroyed
	) | rpl::filter([=](const Data::TopicUpdate &update) {
		return (update.topic->peer() == source.chat)
			&& (update.topic->rootId() == rootId);
	}) | rpl::on_next(refresh, _searchNameLifetime);

	const auto topic = forum->topicFor(rootId);
	if ((!topic || topic->title().isEmpty())
		&& !forum->topicDeleted(rootId)) {
		forum->requestTopic(rootId);
	}
}

void Voice::updateSearchName() {
	_name.setMarkedText(
		st::semiboldTextStyle,
		SearchTitle(parent()),
		Ui::NameTextOptions(),
		Core::TextContext({
			.session = &parent()->history()->session(),
			.repaint = [=] { delegate()->repaintItem(this); },
		}));
}

} // namespace Overview::Layout
