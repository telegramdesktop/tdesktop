/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_drafts.h"

#include "api/api_text_entities.h"
#include "ui/widgets/fields/input_field.h"
#include "chat_helpers/message_field.h"
#include "history/history.h"
#include "history/history_widget.h"
#include "history/history_item_components.h"
#include "main/main_session.h"
#include "data/data_changes.h"
#include "data/data_session.h"
#include "data/data_web_page.h"
#include "mainwidget.h"
#include "storage/localstorage.h"

namespace Data {

WebPageDraft WebPageDraft::FromItem(not_null<HistoryItem*> item) {
	const auto previewMedia = item->media();
	const auto previewPage = previewMedia
		? previewMedia->webpage()
		: nullptr;
	using PageFlag = MediaWebPageFlag;
	const auto previewFlags = previewMedia
		? previewMedia->webpageFlags()
		: PageFlag();
	return {
		.id = previewPage ? previewPage->id : 0,
		.url = previewPage ? previewPage->url : QString(),
		.forceLargeMedia = !!(previewFlags & PageFlag::ForceLargeMedia),
		.forceSmallMedia = !!(previewFlags & PageFlag::ForceSmallMedia),
		.invert = item->invertMedia(),
		.manual = !!(previewFlags & PageFlag::Manual),
		.removed = !previewPage,
	};
}

Draft::Draft(
	const TextWithTags &textWithTags,
	FullReplyTo reply,
	SuggestPostOptions suggest,
	const MessageCursor &cursor,
	WebPageDraft webpage,
	mtpRequestId saveRequestId)
: textWithTags(textWithTags)
, reply(std::move(reply))
, suggest(suggest)
, cursor(cursor)
, webpage(webpage)
, saveRequestId(saveRequestId) {
}

Draft::Draft(
	not_null<const Ui::InputField*> field,
	FullReplyTo reply,
	SuggestPostOptions suggest,
	WebPageDraft webpage,
	mtpRequestId saveRequestId)
: textWithTags(field->getTextWithTags())
, reply(std::move(reply))
, suggest(suggest)
, cursor(field)
, webpage(webpage) {
}

void ApplyPeerCloudDraft(
		not_null<Main::Session*> session,
		PeerId peerId,
		MsgId topicRootId,
		PeerId monoforumPeerId,
		const MTPDdraftMessage &draft) {
	const auto history = session->data().history(peerId);
	const auto date = draft.vdate().v;
	if (history->skipCloudDraftUpdate(topicRootId, monoforumPeerId, date)) {
		return;
	}
	const auto textWithTags = TextWithTags{
		qs(draft.vmessage()),
		TextUtilities::ConvertEntitiesToTextTags(
			Api::EntitiesFromMTP(
				session,
				draft.ventities().value_or_empty()))
	};
	auto replyTo = draft.vreply_to()
		? ReplyToFromMTP(history, *draft.vreply_to())
		: FullReplyTo();
	replyTo.topicRootId = topicRootId;
	replyTo.monoforumPeerId = monoforumPeerId;
	auto webpage = WebPageDraft{
		.invert = draft.is_invert_media(),
		.removed = draft.is_no_webpage(),
	};
	if (const auto media = draft.vmedia()) {
		media->match([&](const MTPDmessageMediaWebPage &data) {
			const auto parsed = session->data().processWebpage(
				data.vwebpage());
			if (!parsed->failed) {
				webpage.forceLargeMedia = data.is_force_large_media();
				webpage.forceSmallMedia = data.is_force_small_media();
				webpage.manual = data.is_manual();
				webpage.url = parsed->url;
				webpage.id = parsed->id;
			}
		}, [](const auto &) {});
	}
	auto suggest = SuggestPostOptions();
	if (!history->suggestDraftAllowed()) {
		// Don't apply suggest options in unsupported chats.
	} else if (const auto suggested = draft.vsuggested_post()) {
		const auto &data = suggested->data();
		suggest.exists = 1;
		suggest.date = data.vschedule_date().value_or_empty();
		const auto price = CreditsAmountFromTL(data.vprice());
		suggest.priceWhole = price.whole();
		suggest.priceNano = price.nano();
		suggest.ton = price.ton() ? 1 : 0;
	}
	auto cloudDraft = std::make_unique<Draft>(
		textWithTags,
		replyTo,
		suggest,
		MessageCursor(Ui::kQFixedMax, Ui::kQFixedMax, Ui::kQFixedMax),
		std::move(webpage));
	cloudDraft->date = date;

	history->setCloudDraft(std::move(cloudDraft));
	history->applyCloudDraft(topicRootId, monoforumPeerId);
}

void ClearPeerCloudDraft(
		not_null<Main::Session*> session,
		PeerId peerId,
		MsgId topicRootId,
		PeerId monoforumPeerId,
		TimeId date) {
	const auto history = session->data().history(peerId);
	if (history->skipCloudDraftUpdate(topicRootId, monoforumPeerId, date)) {
		return;
	}

	history->clearCloudDraft(topicRootId, monoforumPeerId);
	history->applyCloudDraft(topicRootId, monoforumPeerId);
}

void SetChatLinkDraft(not_null<PeerData*> peer, TextWithEntities draft) {
	static const auto kInlineStart = QRegularExpression("^@[a-zA-Z0-9_]");
	if (kInlineStart.match(draft.text).hasMatch()) {
		draft = TextWithEntities().append(' ').append(std::move(draft));
	}

	const auto textWithTags = TextWithTags{
		draft.text,
		TextUtilities::ConvertEntitiesToTextTags(draft.entities)
	};
	const auto cursor = MessageCursor{
		int(textWithTags.text.size()),
		int(textWithTags.text.size()),
		Ui::kQFixedMax
	};
	const auto history = peer->owner().history(peer->id);
	const auto topicRootId = MsgId();
	const auto monoforumPeerId = PeerId();
	history->setLocalDraft(std::make_unique<Draft>(
		textWithTags,
		FullReplyTo{
			.topicRootId = topicRootId,
			.monoforumPeerId = monoforumPeerId,
		},
		SuggestPostOptions(),
		cursor,
		WebPageDraft()));
	history->clearLocalEditDraft(topicRootId, monoforumPeerId);
	history->session().changes().entryUpdated(
		history,
		EntryUpdate::Flag::LocalDraftSet);
}

} // namespace Data
