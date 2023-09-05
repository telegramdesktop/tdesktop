/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

namespace HistoryView {

class StickerPlayer {
public:
	virtual ~StickerPlayer() = default;

	struct FrameInfo {
		QImage image;
		int index = 0;
	};
	virtual void setRepaintCallback(Fn<void()> callback) = 0;
	[[nodiscard]] virtual bool ready() = 0;
	[[nodiscard]] virtual int framesCount() = 0;
	[[nodiscard]] virtual FrameInfo frame(
		QSize size,
		QColor colored,
		bool mirrorHorizontal,
		crl::time now,
		bool paused) = 0;
	virtual bool markFrameShown() = 0;

};

} // namespace HistoryView
