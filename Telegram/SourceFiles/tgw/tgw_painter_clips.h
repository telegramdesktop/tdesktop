/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class QPainter;

namespace Tgw {

inline constexpr auto kClipHistoryEntryBytes = int64(192);

[[nodiscard]] bool ShortensClipHistory();
void ShortenClipHistory(QPainter &painter);
[[nodiscard]] int64 ClipBaseBytes(const QPainter &painter);
[[nodiscard]] int64 ClipDataBytes(QPainter &painter);

} // namespace Tgw
