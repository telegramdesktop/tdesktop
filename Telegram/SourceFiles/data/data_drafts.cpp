/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_drafts.h"

#include "ui/widgets/input_fields.h"
#include "chat_helpers/message_field.h"
#include "history/history_widget.h"
#include "mainwidget.h"
#include "storage/localstorage.h"

namespace Data {
namespace {

} // namespace

Draft::Draft(const Ui::FlatTextarea *field, MsgId msgId, bool previewCancelled, mtpRequestId saveRequestId)
	: textWithTags(field->getTextWithTags())
	, msgId(msgId)
	, cursor(field)
	, previewCancelled(previewCancelled) {
}

void applyPeerCloudDraft(PeerId peerId, const MTPDdraftMessage &draft) {
	auto history = App::history(peerId);
	auto text = TextWithEntities { qs(draft.vmessage), draft.has_entities() ? TextUtilities::EntitiesFromMTP(draft.ventities.v) : EntitiesInText() };
	auto textWithTags = TextWithTags { TextUtilities::ApplyEntities(text), ConvertEntitiesToTextTags(text.entities) };
	auto replyTo = draft.has_reply_to_msg_id() ? draft.vreply_to_msg_id.v : MsgId(0);
	auto cloudDraft = std::make_unique<Draft>(textWithTags, replyTo, MessageCursor(QFIXED_MAX, QFIXED_MAX, QFIXED_MAX), draft.is_no_webpage());
	cloudDraft->date = ::date(draft.vdate);

	history->setCloudDraft(std::move(cloudDraft));
	history->createLocalDraftFromCloud();
	history->updateChatListSortPosition();

	if (auto main = App::main()) {
		main->applyCloudDraft(history);
	}
}

void clearPeerCloudDraft(PeerId peerId) {
	auto history = App::history(peerId);

	history->clearCloudDraft();
	history->clearLocalDraft();

	history->updateChatListSortPosition();

	if (auto main = App::main()) {
		main->applyCloudDraft(history);
	}
}

} // namespace Data
