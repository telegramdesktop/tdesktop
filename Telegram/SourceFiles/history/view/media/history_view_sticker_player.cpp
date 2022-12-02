/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_sticker_player.h"

#include "core/file_location.h"

namespace HistoryView {
namespace {

using ClipNotification = ::Media::Clip::Notification;

} // namespace

LottiePlayer::LottiePlayer(std::unique_ptr<Lottie::SinglePlayer> lottie)
: _lottie(std::move(lottie)) {
}

void LottiePlayer::setRepaintCallback(Fn<void()> callback) {
	_repaintLifetime = _lottie->updates(
	) | rpl::start_with_next([=](Lottie::Update update) {
		v::match(update.data, [&](const Lottie::Information &) {
			callback();
			//markFramesTillExternal();
		}, [&](const Lottie::DisplayFrameRequest &) {
			callback();
		});
	});
}

bool LottiePlayer::ready() {
	return _lottie->ready();
}

int LottiePlayer::framesCount() {
	return _lottie->information().framesCount;
}

LottiePlayer::FrameInfo LottiePlayer::frame(
		QSize size,
		QColor colored,
		bool mirrorHorizontal,
		crl::time now,
		bool paused) {
	auto request = Lottie::FrameRequest();
	request.box = size * style::DevicePixelRatio();
	request.colored = colored;
	request.mirrorHorizontal = mirrorHorizontal;
	const auto info = _lottie->frameInfo(request);
	return { .image = info.image, .index = info.index };
}

bool LottiePlayer::markFrameShown() {
	return _lottie->markFrameShown();
}

WebmPlayer::WebmPlayer(
	const Core::FileLocation &location,
	const QByteArray &data,
	QSize size)
: _reader(
	::Media::Clip::MakeReader(location, data, [=](ClipNotification update) {
	clipCallback(update);
}))
, _size(size) {
}

void WebmPlayer::clipCallback(ClipNotification notification) {
	switch (notification) {
	case ClipNotification::Reinit: {
		if (_reader->state() == ::Media::Clip::State::Error) {
			_reader.setBad();
		} else if (_reader->ready() && !_reader->started()) {
			_reader->start({ .frame = _size, .keepAlpha = true });
		}
	} break;

	case ClipNotification::Repaint: break;
	}

	_repaintCallback();
}

void WebmPlayer::setRepaintCallback(Fn<void()> callback) {
	_repaintCallback = std::move(callback);
}

bool WebmPlayer::ready() {
	return _reader && _reader->started();
}

int WebmPlayer::framesCount() {
	return -1;
}

WebmPlayer::FrameInfo WebmPlayer::frame(
		QSize size,
		QColor colored,
		bool mirrorHorizontal,
		crl::time now,
		bool paused) {
	auto request = ::Media::Clip::FrameRequest();
	request.frame = size;
	request.factor = style::DevicePixelRatio();
	request.keepAlpha = true;
	request.colored = colored;
	const auto info = _reader->frameInfo(request, paused ? 0 : now);
	return { .image = info.image, .index = info.index };
}

bool WebmPlayer::markFrameShown() {
	return _reader->moveToNextFrame();
}

StaticStickerPlayer::StaticStickerPlayer(
	const Core::FileLocation &location,
	const QByteArray &data,
	QSize size)
: _frame(Images::Read({
	.path = location.name(),
	.content = data,
}).image) {
	if (!_frame.isNull()) {
		size = _frame.size().scaled(size, Qt::KeepAspectRatio);
		const auto ratio = style::DevicePixelRatio();
		_frame = Images::Prepare(std::move(_frame), size * ratio, {});
		_frame.setDevicePixelRatio(ratio);
	}
}

void StaticStickerPlayer::setRepaintCallback(Fn<void()> callback) {
	callback();
}

bool StaticStickerPlayer::ready() {
	return true;
}

int StaticStickerPlayer::framesCount() {
	return 1;
}

StaticStickerPlayer::FrameInfo StaticStickerPlayer::frame(
		QSize size,
		QColor colored,
		bool mirrorHorizontal,
		crl::time now,
		bool paused) {
	return { _frame };
}

bool StaticStickerPlayer::markFrameShown() {
	return false;
}

} // namespace HistoryView
