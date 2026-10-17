/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/editor_message_source.h"

#include "base/unixtime.h"
#include "data/data_groups.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_web_page.h"
#include "editor/editor_message_video.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_fake_items.h"
#include "main/main_session.h"

namespace Editor {
namespace {

[[nodiscard]] TextWithEntities LinkText(const LinkPreview &link) {
	auto result = TextWithEntities{
		link.name.isEmpty() ? link.url : link.name,
	};
	result.entities.push_back(link.name.isEmpty()
		? EntityInText(EntityType::Url, 0, result.text.size())
		: EntityInText(
			EntityType::CustomUrl,
			0,
			result.text.size(),
			link.url));
	return result;
}

[[nodiscard]] MTPMessageMedia LinkMedia(
		const LinkPreview &link,
		WebPageData *webpage) {
	if (!webpage) {
		return MTP_messageMediaEmpty();
	}
	using Flag = MTPDmessageMediaWebPage::Flag;
	return MTP_messageMediaWebPage(
		MTP_flags(link.largePhoto
			? Flag::f_force_large_media
			: Flag::f_force_small_media),
		MTP_webPagePending(
			MTP_flags(0),
			MTP_long(webpage->id),
			MTPstring(),
			MTP_int(0)));
}

[[nodiscard]] not_null<History*> LinkHistory(
		not_null<Main::Session*> session) {
	const auto peerId = HistoryView::GenerateUser(
		session->data().history(PeerData::kServiceNotificationsId),
		u"Link"_q);
	const auto result = session->data().history(peerId);
	result->peer->setBarSettings(PeerBarSettings());
	return result;
}

[[nodiscard]] not_null<HistoryItem*> MakeLinkItem(
		not_null<Main::Session*> session,
		const LinkPreview &link,
		WebPageData *webpage) {
	const auto history = LinkHistory(session);
	return history->makeMessage(
		{
			.id = history->nextNonHistoryEntryId(),
			.flags = (MessageFlag::FakeHistoryItem
				| MessageFlag::HasFromId
				| (link.captionAbove
					? MessageFlag()
					: MessageFlag::InvertMedia)),
			.from = session->userPeerId(),
			.date = base::unixtime::now(),
		},
		LinkText(link),
		LinkMedia(link, webpage));
}

} // namespace

bool MessageForbidsRender(not_null<HistoryItem*> item) {
	return item->isSponsored()
		|| item->forbidsSaving()
		|| !item->history()->peer->allowsForwarding();
}

bool CanRenderMessage(not_null<HistoryItem*> item) {
	const auto media = item->media();
	return !MessageForbidsRender(item)
		&& !item->isEphemeral()
		&& !item->showSimilarChannels()
		&& !item->isLegacyMessage()
		&& !dynamic_cast<const Data::MediaDice*>(media);
}

not_null<HistoryItem*> MessageToRender(not_null<HistoryItem*> item) {
	const auto group = item->history()->owner().groups().find(item);
	return group ? group->items.front() : item;
}

MessageSource::MessageSource(not_null<HistoryItem*> item)
: _session(&item->history()->session())
, _item(item) {
	if (MessageVideo::Find(item)) {
		_video = std::make_unique<MessageVideo>(item);
	}
	watchRemoval();
}

MessageSource::MessageSource(
	not_null<Main::Session*> session,
	LinkPreview link,
	WebPageData *webpage)
: _session(session)
, _link(std::move(link))
, _webpage(webpage)
, _item(MakeLinkItem(session, *_link, webpage))
, _owned(true) {
	watchRemoval();
}

MessageSource::~MessageSource() {
	if (_owned) {
		if (const auto item = base::take(_item)) {
			item->destroy();
		}
	}
}

void MessageSource::watchRemoval() {
	_session->data().itemRemoved(
	) | rpl::filter([=](not_null<const HistoryItem*> item) {
		return (item == _item);
	}) | rpl::on_next([=] {
		_item = nullptr;
		_removed.fire({});
	}, _lifetime);
}

Main::Session &MessageSource::session() const {
	return *_session;
}

HistoryItem *MessageSource::item() const {
	return _item;
}

const std::optional<LinkPreview> &MessageSource::link() const {
	return _link;
}

WebPageData *MessageSource::webpage() const {
	return _webpage;
}

MessageVideo *MessageSource::video() const {
	return _video.get();
}

rpl::producer<> MessageSource::removed() const {
	return _removed.events();
}

} // namespace Editor
