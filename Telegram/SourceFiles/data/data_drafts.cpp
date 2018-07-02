/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_drafts.h"

#include "ui/widgets/input_fields.h"
#include "chat_helpers/message_field.h"
#include "history/history.h"
#include "history/history_widget.h"
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

void applyPeerCloudDraft(PeerId peerId, const MTPDdraftMessage &draft) {
	auto history = App::history(peerId);
	auto textWithTags = TextWithTags {
		qs(draft.vmessage),
		ConvertEntitiesToTextTags(
			draft.has_entities()
			? TextUtilities::EntitiesFromMTP(draft.ventities.v)
			: EntitiesInText())
	};
	if (history->skipCloudDraft(textWithTags.text, draft.vdate.v)) {
		return;
	}
	auto replyTo = draft.has_reply_to_msg_id() ? draft.vreply_to_msg_id.v : MsgId(0);
	auto cloudDraft = std::make_unique<Draft>(textWithTags, replyTo, MessageCursor(QFIXED_MAX, QFIXED_MAX, QFIXED_MAX), draft.is_no_webpage());
	cloudDraft->date = draft.vdate.v;

	history->setCloudDraft(std::move(cloudDraft));
	history->createLocalDraftFromCloud();
	history->updateChatListSortPosition();

	if (auto main = App::main()) {
		main->applyCloudDraft(history);
	}
}

void clearPeerCloudDraft(PeerId peerId, TimeId date) {
	const auto history = App::history(peerId);
	if (history->skipCloudDraft(QString(), date)) {
		return;
	}

	history->clearCloudDraft();
	history->clearLocalDraft();

	history->updateChatListSortPosition();

	if (auto main = App::main()) {
		main->applyCloudDraft(history);
	}
}

} // namespace Data
