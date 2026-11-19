/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "emoji.h"

class QPainter;

namespace Tgw {

struct EmojiSides {
	int normal = 0;
	int large = 0;
	int isolated = 0;
};

class EmojiAccount final {
public:
	EmojiAccount();
	EmojiAccount(const EmojiAccount &) = delete;
	EmojiAccount &operator=(const EmojiAccount &) = delete;
	~EmojiAccount();

	[[nodiscard]] uint64 id() const;
	void close();

private:
	const uint64 _id = 0;

};

class EmojiWaits final {
public:
	EmojiWaits();
	EmojiWaits(const EmojiWaits &) = delete;
	EmojiWaits &operator=(const EmojiWaits &) = delete;
	EmojiWaits(EmojiWaits &&) = default;
	EmojiWaits &operator=(EmojiWaits &&) = default;

	void tile();
	void glyph(int index);
	void add(const EmojiWaits &other);
	void clear();

	[[nodiscard]] bool wants(std::optional<int> glyph) const;

private:
	std::vector<uint64> _glyphs;
	bool _tiles = false;

};

[[nodiscard]] EmojiSides CurrentEmojiSides();
[[nodiscard]] rpl::producer<> EmojiRefreshes();
void FinishEmojiImages();

void ConfigureEmojiTiles(
	EmojiSides sides,
	Fn<void(std::optional<int> glyph)> landed);
void PaintEmoji(
	QPainter &p,
	EmojiPtr emoji,
	const QRectF &box,
	float64 deviceSide,
	const EmojiAccount &account);
void TakeEmojiWaits(const EmojiAccount &account, EmojiWaits &into);
void ClearEmojiTiles();

} // namespace Tgw
