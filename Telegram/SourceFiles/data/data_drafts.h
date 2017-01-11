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
#pragma once

namespace Ui {
class FlatTextarea;
} // namespace Ui

namespace Data {

void applyPeerCloudDraft(PeerId peerId, const MTPDdraftMessage &draft);
void clearPeerCloudDraft(PeerId peerId);

struct Draft {
	Draft() {
	}
	Draft(const TextWithTags &textWithTags, MsgId msgId, const MessageCursor &cursor, bool previewCancelled, mtpRequestId saveRequestId = 0)
		: textWithTags(textWithTags)
		, msgId(msgId)
		, cursor(cursor)
		, previewCancelled(previewCancelled)
		, saveRequestId(saveRequestId) {
	}
	Draft(const Ui::FlatTextarea *field, MsgId msgId, bool previewCancelled, mtpRequestId saveRequestId = 0);

	QDateTime date;
	TextWithTags textWithTags;
	MsgId msgId = 0; // replyToId for message draft, editMsgId for edit draft
	MessageCursor cursor;
	bool previewCancelled = false;
	mtpRequestId saveRequestId = 0;
};

inline bool draftStringIsEmpty(const QString &text) {
	for_const (auto ch, text) {
		if (!ch.isSpace()) {
			return false;
		}
	}
	return true;
}

inline bool draftIsNull(Draft *draft) {
	return !draft || (draftStringIsEmpty(draft->textWithTags.text) && !draft->msgId);
}

inline bool draftsAreEqual(Draft *a, Draft *b) {
	bool aIsNull = draftIsNull(a);
	bool bIsNull = draftIsNull(b);
	if (aIsNull) {
		return bIsNull;
	} else if (bIsNull) {
		return false;
	}

	return (a->textWithTags == b->textWithTags) && (a->msgId == b->msgId) && (a->previewCancelled == b->previewCancelled);
}

} // namespace Data
