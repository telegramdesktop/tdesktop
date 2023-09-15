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
#include "main/main_session.h"
#include "data/data_session.h"
#include "mainwidget.h"
#include "storage/localstorage.h"

namespace Data {

Draft::Draft(
	const TextWithTags &textWithTags,
	MsgId msgId,
	MsgId topicRootId,
	const MessageCursor &cursor,
	PreviewState previewState,
	mtpRequestId saveRequestId)
: textWithTags(textWithTags)
, msgId(msgId)
, topicRootId(topicRootId)
, cursor(cursor)
, previewState(previewState)
, saveRequestId(saveRequestId) {
}

Draft::Draft(
	not_null<const Ui::InputField*> field,
	MsgId msgId,
	MsgId topicRootId,
	PreviewState previewState,
	mtpRequestId saveRequestId)
: textWithTags(field->getTextWithTags())
, msgId(msgId)
, topicRootId(topicRootId)
, cursor(field)
, previewState(previewState) {
}

void ApplyPeerCloudDraft(
		not_null<Main::Session*> session,
		PeerId peerId,
		MsgId topicRootId,
		const MTPDdraftMessage &draft) {
	const auto history = session->data().history(peerId);
	const auto date = draft.vdate().v;
	if (history->skipCloudDraftUpdate(topicRootId, date)) {
		return;
	}
	const auto textWithTags = TextWithTags{
		qs(draft.vmessage()),
		TextUtilities::ConvertEntitiesToTextTags(
			Api::EntitiesFromMTP(
				session,
				draft.ventities().value_or_empty()))
	};
	const auto replyTo = draft.vreply_to_msg_id().value_or_empty();
	auto cloudDraft = std::make_unique<Draft>(
		textWithTags,
		replyTo,
		topicRootId,
		MessageCursor(QFIXED_MAX, QFIXED_MAX, QFIXED_MAX),
		(draft.is_no_webpage()
			? Data::PreviewState::Cancelled
			: Data::PreviewState::Allowed));
	cloudDraft->date = date;

	history->setCloudDraft(std::move(cloudDraft));
	history->applyCloudDraft(topicRootId);
}

void ClearPeerCloudDraft(
		not_null<Main::Session*> session,
		PeerId peerId,
		MsgId topicRootId,
		TimeId date) {
	const auto history = session->data().history(peerId);
	if (history->skipCloudDraftUpdate(topicRootId, date)) {
		return;
	}

	history->clearCloudDraft(topicRootId);
	history->applyCloudDraft(topicRootId);
}

} // namespace Data
