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
namespace {

} // namespace

Draft::Draft(
	const TextWithTags &textWithTags,
	MsgId msgId,
	const MessageCursor &cursor,
	bool previewCancelled,
	mtpRequestId saveRequestId)
: textWithTags(textWithTags)
, msgId(msgId)
, cursor(cursor)
, previewCancelled(previewCancelled)
, saveRequestId(saveRequestId) {
}

Draft::Draft(
	not_null<const Ui::InputField*> field,
	MsgId msgId,
	bool previewCancelled,
	mtpRequestId saveRequestId)
: textWithTags(field->getTextWithTags())
, msgId(msgId)
, cursor(field)
, previewCancelled(previewCancelled) {
}

void ApplyPeerCloudDraft(
		not_null<Main::Session*> session,
		PeerId peerId,
		const MTPDdraftMessage &draft) {
	const auto history = session->data().history(peerId);
	const auto textWithTags = TextWithTags {
		qs(draft.vmessage()),
		TextUtilities::ConvertEntitiesToTextTags(
			Api::EntitiesFromMTP(
				session,
				draft.ventities().value_or_empty()))
	};
	const auto replyTo = draft.vreply_to_msg_id().value_or_empty();
	if (history->skipCloudDraft(textWithTags.text, replyTo, draft.vdate().v)) {
		return;
	}
	auto cloudDraft = std::make_unique<Draft>(
		textWithTags,
		replyTo,
		MessageCursor(QFIXED_MAX, QFIXED_MAX, QFIXED_MAX),
		draft.is_no_webpage());
	cloudDraft->date = draft.vdate().v;

	history->setCloudDraft(std::move(cloudDraft));
	history->applyCloudDraft();
}

void ClearPeerCloudDraft(
		not_null<Main::Session*> session,
		PeerId peerId,
		TimeId date) {
	const auto history = session->data().history(peerId);
	if (history->skipCloudDraft(QString(), MsgId(0), date)) {
		return;
	}

	history->clearCloudDraft();
	history->applyCloudDraft();
}

} // namespace Data
