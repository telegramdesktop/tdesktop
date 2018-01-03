/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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
