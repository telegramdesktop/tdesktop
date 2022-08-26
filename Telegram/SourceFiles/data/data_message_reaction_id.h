/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {

struct ReactionId {
	std::variant<QString, DocumentId> data;

	[[nodiscard]] bool empty() const {
		const auto emoji = std::get_if<QString>(&data);
		return emoji && emoji->isEmpty();
	}
	[[nodiscard]] QString emoji() const {
		const auto emoji = std::get_if<QString>(&data);
		return emoji ? *emoji : QString();
	}
	[[nodiscard]] DocumentId custom() const {
		const auto custom = std::get_if<DocumentId>(&data);
		return custom ? *custom : DocumentId();
	}
};

struct MessageReaction {
	ReactionId id;
	int count = 0;
	bool my = false;
};

inline bool operator<(const ReactionId &a, const ReactionId &b) {
	return a.data < b.data;
}
inline bool operator==(const ReactionId &a, const ReactionId &b) {
	return a.data == b.data;
}

[[nodiscard]] QString ReactionEntityData(const ReactionId &id);

[[nodiscard]] ReactionId ReactionFromMTP(const MTPReaction &reaction);
[[nodiscard]] MTPReaction ReactionToMTP(ReactionId id);

} // namespace Data

Q_DECLARE_METATYPE(Data::ReactionId);
