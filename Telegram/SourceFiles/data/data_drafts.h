/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
class InputField;
} // namespace Ui

namespace Main {
class Session;
} // namespace Main

namespace Data {

void ApplyPeerCloudDraft(
	not_null<Main::Session*> session,
	PeerId peerId,
	const MTPDdraftMessage &draft);
void ClearPeerCloudDraft(
	not_null<Main::Session*> session,
	PeerId peerId,
	TimeId date);

enum class PreviewState : char {
	Allowed,
	Cancelled,
	EmptyOnEdit,
};

struct Draft {
	Draft() = default;
	Draft(
		const TextWithTags &textWithTags,
		MsgId msgId,
		const MessageCursor &cursor,
		PreviewState previewState,
		mtpRequestId saveRequestId = 0);
	Draft(
		not_null<const Ui::InputField*> field,
		MsgId msgId,
		PreviewState previewState,
		mtpRequestId saveRequestId = 0);

	TimeId date = 0;
	TextWithTags textWithTags;
	MsgId msgId = 0; // replyToId for message draft, editMsgId for edit draft
	MessageCursor cursor;
	PreviewState previewState = PreviewState::Allowed;
	mtpRequestId saveRequestId = 0;
};

class DraftKey {
public:
	[[nodiscard]] static DraftKey None() {
		return 0;
	}
	[[nodiscard]] static DraftKey Local() {
		return kLocalDraftIndex;
	}
	[[nodiscard]] static DraftKey LocalEdit() {
		return kLocalDraftIndex + kEditDraftShift;
	}
	[[nodiscard]] static DraftKey Cloud() {
		return kCloudDraftIndex;
	}
	[[nodiscard]] static DraftKey Scheduled() {
		return kScheduledDraftIndex;
	}
	[[nodiscard]] static DraftKey ScheduledEdit() {
		return kScheduledDraftIndex + kEditDraftShift;
	}
	[[nodiscard]] static DraftKey Replies(MsgId rootId) {
		return rootId;
	}
	[[nodiscard]] static DraftKey RepliesEdit(MsgId rootId) {
		return rootId + kEditDraftShift;
	}

	[[nodiscard]] static DraftKey FromSerialized(int32 value) {
		return value;
	}
	[[nodiscard]] int32 serialize() const {
		return _value;
	}

	inline bool operator<(const DraftKey &other) const {
		return _value < other._value;
	}
	inline bool operator==(const DraftKey &other) const {
		return _value == other._value;
	}
	inline bool operator>(const DraftKey &other) const {
		return (other < *this);
	}
	inline bool operator<=(const DraftKey &other) const {
		return !(other < *this);
	}
	inline bool operator>=(const DraftKey &other) const {
		return !(*this < other);
	}
	inline bool operator!=(const DraftKey &other) const {
		return !(*this == other);
	}
	inline explicit operator bool() const {
		return _value != 0;
	}

private:
	DraftKey(int value) : _value(value) {
	}

	static constexpr auto kLocalDraftIndex = -1;
	static constexpr auto kCloudDraftIndex = -2;
	static constexpr auto kScheduledDraftIndex = -3;
	static constexpr auto kEditDraftShift = ServerMaxMsgId;

	int _value = 0;

};

using HistoryDrafts = base::flat_map<DraftKey, std::unique_ptr<Draft>>;

inline bool draftStringIsEmpty(const QString &text) {
	for_const (auto ch, text) {
		if (!ch.isSpace()) {
			return false;
		}
	}
	return true;
}

inline bool draftIsNull(const Draft *draft) {
	return !draft
		|| (draftStringIsEmpty(draft->textWithTags.text) && !draft->msgId);
}

inline bool draftsAreEqual(const Draft *a, const Draft *b) {
	bool aIsNull = draftIsNull(a);
	bool bIsNull = draftIsNull(b);
	if (aIsNull) {
		return bIsNull;
	} else if (bIsNull) {
		return false;
	}

	return (a->textWithTags == b->textWithTags)
		&& (a->msgId == b->msgId)
		&& (a->previewState == b->previewState);
}

} // namespace Data
