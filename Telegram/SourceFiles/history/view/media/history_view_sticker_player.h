/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/media/history_view_sticker_player_abstract.h"

#include "lottie/lottie_single_player.h"
#include "media/clip/media_clip_reader.h"

namespace Core {
class FileLocation;
} // namespace Core

namespace HistoryView {

class LottiePlayer final : public StickerPlayer {
public:
	explicit LottiePlayer(std::unique_ptr<Lottie::SinglePlayer> lottie);

	void setRepaintCallback(Fn<void()> callback) override;
	bool ready() override;
	int framesCount() override;
	FrameInfo frame(
		QSize size,
		QColor colored,
		bool mirrorHorizontal,
		crl::time now,
		bool paused) override;
	bool markFrameShown() override;

private:
	std::unique_ptr<Lottie::SinglePlayer> _lottie;
	rpl::lifetime _repaintLifetime;

};

class WebmPlayer final : public StickerPlayer {
public:
	WebmPlayer(
		const Core::FileLocation &location,
		const QByteArray &data,
		QSize size);

	void setRepaintCallback(Fn<void()> callback) override;
	bool ready() override;
	int framesCount() override;
	FrameInfo frame(
		QSize size,
		QColor colored,
		bool mirrorHorizontal,
		crl::time now,
		bool paused) override;
	bool markFrameShown() override;

private:
	void clipCallback(::Media::Clip::Notification notification);

	::Media::Clip::ReaderPointer _reader;
	Fn<void()> _repaintCallback;
	QSize _size;

};

class StaticStickerPlayer final : public StickerPlayer {
public:
	StaticStickerPlayer(
		const Core::FileLocation &location,
		const QByteArray &data,
		QSize size);

	void setRepaintCallback(Fn<void()> callback) override;
	bool ready() override;
	int framesCount() override;
	FrameInfo frame(
		QSize size,
		QColor colored,
		bool mirrorHorizontal,
		crl::time now,
		bool paused) override;
	bool markFrameShown() override;

private:
	QImage _frame;

};

} // namespace HistoryView
