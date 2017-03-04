/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "data/data_drafts.h"

#include "ui/widgets/input_fields.h"
#include "historywidget.h"
#include "mainwidget.h"
#include "localstorage.h"

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
	auto text = qs(draft.vmessage);
	auto entities = draft.has_entities() ? entitiesFromMTP(draft.ventities.c_vector().v) : EntitiesInText();
	TextWithTags textWithTags = { textApplyEntities(text, entities), textTagsFromEntities(entities) };
	MsgId replyTo = draft.has_reply_to_msg_id() ? draft.vreply_to_msg_id.v : 0;
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
