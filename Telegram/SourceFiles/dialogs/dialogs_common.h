/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#ifdef _DEBUG
#include <QtCore/QDebug>
#endif // _DEBUG

namespace style {
struct DialogRightButton;
} // namespace style

namespace Ui {
class RippleAnimation;
} // namespace Ui

namespace Dialogs {

class Row;

enum class SortMode {
	Date    = 0x00,
	Name    = 0x01,
	Add     = 0x02,
};

struct PositionChange {
	int from = -1;
	int to = -1;
	int height = 0;
};

struct UnreadState {
	int messages = 0;
	int messagesMuted = 0;
	int chats = 0;
	int chatsMuted = 0;
	int marks = 0;
	int marksMuted = 0;
	int reactions = 0;
	int reactionsMuted = 0;
	int mentions = 0;
	bool known = false;

	UnreadState &operator+=(const UnreadState &other) {
		messages += other.messages;
		messagesMuted += other.messagesMuted;
		chats += other.chats;
		chatsMuted += other.chatsMuted;
		marks += other.marks;
		marksMuted += other.marksMuted;
		reactions += other.reactions;
		reactionsMuted += other.reactionsMuted;
		mentions += other.mentions;
		return *this;
	}
	UnreadState &operator-=(const UnreadState &other) {
		messages -= other.messages;
		messagesMuted -= other.messagesMuted;
		chats -= other.chats;
		chatsMuted -= other.chatsMuted;
		marks -= other.marks;
		marksMuted -= other.marksMuted;
		reactions -= other.reactions;
		reactionsMuted -= other.reactionsMuted;
		mentions -= other.mentions;
		return *this;
	}
};

inline UnreadState operator+(const UnreadState &a, const UnreadState &b) {
	auto result = a;
	result += b;
	return result;
}

inline UnreadState operator-(const UnreadState &a, const UnreadState &b) {
	auto result = a;
	result -= b;
	return result;
}

#ifdef _DEBUG
inline QDebug operator<<(QDebug debug, const UnreadState &state) {
	return debug.nospace() << "UnreadState(messages:" << state.messages
	<< ", messagesMuted:" << state.messagesMuted
	<< ", chats:" << state.chats
	<< ", chatsMuted:" << state.chatsMuted
	<< ", marks:" << state.marks
	<< ", marksMuted:" << state.marksMuted
	<< ", reactions:" << state.reactions
	<< ", reactionsMuted:" << state.reactionsMuted
	<< ", mentions:" << state.mentions
	<< ", known:" << state.known << ")";
}
#endif // _DEBUG

struct BadgesState {
	int unreadCounter = 0;
	bool unread : 1 = false;
	bool unreadMuted : 1 = false;
	bool mention : 1 = false;
	bool mentionMuted : 1 = false;
	bool reaction : 1 = false;
	bool reactionMuted : 1 = false;

	friend inline constexpr auto operator<=>(
		BadgesState,
		BadgesState) = default;
	friend inline constexpr bool operator==(
		BadgesState,
		BadgesState) = default;

	[[nodiscard]] bool empty() const {
		return !unread && !mention && !reaction;
	}
};

enum class CountInBadge : uchar {
	Default,
	Chats,
	Messages,
};

enum class IncludeInBadge : uchar {
	Default,
	Unmuted,
	All,
	UnmutedOrAll,
};

struct RowsByLetter {
	not_null<Row*> main;
	base::flat_map<QChar, not_null<Row*>> letters;
};

struct RightButton final {
	const style::DialogRightButton *st = nullptr;
	QImage bg;
	QImage selectedBg;
	QImage activeBg;
	Ui::Text::String text;
	std::unique_ptr<Ui::RippleAnimation> ripple;

	explicit operator bool() const {
		return st != nullptr;
	}
};

} // namespace Dialogs
