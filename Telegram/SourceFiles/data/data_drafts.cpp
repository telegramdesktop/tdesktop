/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_drafts.h"

#include "api/api_text_entities.h"
#include "ui/widgets/input_fields.h"
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
	const MessageCursor &cursor,
	PreviewState previewState,
	mtpRequestId saveRequestId)
: textWithTags(textWithTags)
, msgId(msgId)
, cursor(cursor)
, previewState(previewState)
, saveRequestId(saveRequestId) {
}

Draft::Draft(
	not_null<const Ui::InputField*> field,
	MsgId msgId,
	PreviewState previewState,
	mtpRequestId saveRequestId)
: textWithTags(field->getTextWithTags())
, msgId(msgId)
, cursor(field)
, previewState(previewState) {
}

void ApplyPeerCloudDraft(
		not_null<Main::Session*> session,
		PeerId peerId,
		const MTPDdraftMessage &draft) {
	const auto history = session->data().history(peerId);
	const auto date = draft.vdate().v;
	if (history->skipCloudDraftUpdate(date)) {
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
		MessageCursor(QFIXED_MAX, QFIXED_MAX, QFIXED_MAX),
		(draft.is_no_webpage()
			? Data::PreviewState::Cancelled
			: Data::PreviewState::Allowed));
	cloudDraft->date = date;

	history->setCloudDraft(std::move(cloudDraft));
	history->applyCloudDraft();
}

void ClearPeerCloudDraft(
		not_null<Main::Session*> session,
		PeerId peerId,
		TimeId date) {
	const auto history = session->data().history(peerId);
	if (history->skipCloudDraftUpdate(date)) {
		return;
	}

	history->clearCloudDraft();
	history->applyCloudDraft();
}

} // namespace Data
