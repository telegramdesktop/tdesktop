/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_msg_id.h"

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
	MsgId topicRootId,
	const MTPDdraftMessage &draft);
void ClearPeerCloudDraft(
	not_null<Main::Session*> session,
	PeerId peerId,
	MsgId topicRootId,
	TimeId date);

struct WebPageDraft {
	[[nodiscard]] static WebPageDraft FromItem(not_null<HistoryItem*> item);

	WebPageId id = 0;
	QString url;
	bool forceLargeMedia : 1 = false;
	bool forceSmallMedia : 1 = false;
	bool invert : 1 = false;
	bool manual : 1 = false;
	bool removed : 1 = false;

	friend inline bool operator==(const WebPageDraft&, const WebPageDraft&)
		= default;
};

struct Draft {
	Draft() = default;
	Draft(
		const TextWithTags &textWithTags,
		FullReplyTo reply,
		const MessageCursor &cursor,
		WebPageDraft webpage,
		mtpRequestId saveRequestId = 0);
	Draft(
		not_null<const Ui::InputField*> field,
		FullReplyTo reply,
		WebPageDraft webpage,
		mtpRequestId saveRequestId = 0);

	TimeId date = 0;
	TextWithTags textWithTags;
	FullReplyTo reply; // reply.messageId.msg is editMsgId for edit draft.
	MessageCursor cursor;
	WebPageDraft webpage;
	mtpRequestId saveRequestId = 0;
};

class DraftKey {
public:
	[[nodiscard]] static constexpr DraftKey None() {
		return 0;
	}
	[[nodiscard]] static constexpr DraftKey Local(MsgId topicRootId) {
		return (topicRootId < 0 || topicRootId >= ServerMaxMsgId)
			? None()
			: (topicRootId ? topicRootId.bare : kLocalDraftIndex);
	}
	[[nodiscard]] static constexpr DraftKey LocalEdit(MsgId topicRootId) {
		return (topicRootId < 0 || topicRootId >= ServerMaxMsgId)
			? None()
			: ((topicRootId ? topicRootId.bare : kLocalDraftIndex)
				+ kEditDraftShift);
	}
	[[nodiscard]] static constexpr DraftKey Cloud(MsgId topicRootId) {
		return (topicRootId < 0 || topicRootId >= ServerMaxMsgId)
			? None()
			: topicRootId
			? (kCloudDraftShift + topicRootId.bare)
			: kCloudDraftIndex;
	}
	[[nodiscard]] static constexpr DraftKey Scheduled() {
		return kScheduledDraftIndex;
	}
	[[nodiscard]] static constexpr DraftKey ScheduledEdit() {
		return kScheduledDraftIndex + kEditDraftShift;
	}
	[[nodiscard]] static constexpr DraftKey Shortcut(
			BusinessShortcutId shortcutId) {
		return (shortcutId < 0 || shortcutId >= ServerMaxMsgId)
			? None()
			: (kShortcutDraftShift + shortcutId);
	}
	[[nodiscard]] static constexpr DraftKey ShortcutEdit(
			BusinessShortcutId shortcutId) {
		return (shortcutId < 0 || shortcutId >= ServerMaxMsgId)
			? None()
			: (kShortcutDraftShift + kEditDraftShift + shortcutId);
	}

	[[nodiscard]] static constexpr DraftKey FromSerialized(qint64 value) {
		return value;
	}
	[[nodiscard]] constexpr qint64 serialize() const {
		return _value;
	}

	[[nodiscard]] static constexpr DraftKey FromSerializedOld(int32 value) {
		return !value
			? None()
			: (value == kLocalDraftIndex + kEditDraftShiftOld)
			? LocalEdit(0)
			: (value == kScheduledDraftIndex + kEditDraftShiftOld)
			? ScheduledEdit()
			: (value > 0 && value < 0x4000'0000)
			? Local(MsgId(value))
			: (value > kEditDraftShiftOld
				&& value < kEditDraftShiftOld + 0x4000'000)
			? LocalEdit(int64(value - kEditDraftShiftOld))
			: None();
	}
	[[nodiscard]] constexpr bool isLocal() const {
		return (_value == kLocalDraftIndex)
			|| (_value > 0 && _value < ServerMaxMsgId.bare);
	}
	[[nodiscard]] constexpr bool isCloud() const {
		return (_value == kCloudDraftIndex)
			|| (_value > kCloudDraftShift
				&& _value < kCloudDraftShift + ServerMaxMsgId.bare);
	}

	[[nodiscard]] constexpr MsgId topicRootId() const {
		const auto max = ServerMaxMsgId.bare;
		if (_value > kCloudDraftShift && _value < kCloudDraftShift + max) {
			return (_value - kCloudDraftShift);
		} else if (_value > kEditDraftShift && _value < kEditDraftShift + max) {
			return (_value - kEditDraftShift);
		} else if (_value > 0 && _value < max) {
			return _value;
		}
		return 0;
	}


	friend inline constexpr auto operator<=>(DraftKey, DraftKey) = default;

	inline explicit operator bool() const {
		return _value != 0;
	}

private:
	constexpr DraftKey(int64 value) : _value(value) {
	}

	static constexpr auto kLocalDraftIndex = -1;
	static constexpr auto kCloudDraftIndex = -2;
	static constexpr auto kScheduledDraftIndex = -3;
	static constexpr auto kEditDraftShift = ServerMaxMsgId.bare;
	static constexpr auto kCloudDraftShift = 2 * ServerMaxMsgId.bare;
	static constexpr auto kShortcutDraftShift = 3 * ServerMaxMsgId.bare;
	static constexpr auto kEditDraftShiftOld = 0x3FFF'FFFF;

	int64 _value = 0;

};

using HistoryDrafts = base::flat_map<DraftKey, std::unique_ptr<Draft>>;

[[nodiscard]] inline bool DraftStringIsEmpty(const QString &text) {
	for (const auto &ch : text) {
		if (!ch.isSpace()) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] inline bool DraftIsNull(const Draft *draft) {
	return !draft
		|| (!draft->reply.messageId
			&& DraftStringIsEmpty(draft->textWithTags.text));
}

[[nodiscard]] inline bool DraftsAreEqual(const Draft *a, const Draft *b) {
	const auto aIsNull = DraftIsNull(a);
	const auto bIsNull = DraftIsNull(b);
	if (aIsNull) {
		return bIsNull;
	} else if (bIsNull) {
		return false;
	}
	return (a->textWithTags == b->textWithTags)
		&& (a->reply == b->reply)
		&& (a->webpage == b->webpage);
}

void SetChatLinkDraft(not_null<PeerData*> peer, TextWithEntities draft);

} // namespace Data
