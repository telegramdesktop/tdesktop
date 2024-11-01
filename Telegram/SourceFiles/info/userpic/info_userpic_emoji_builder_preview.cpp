/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/userpic/info_userpic_emoji_builder_preview.h"

#include "chat_helpers/stickers_lottie.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_session.h"
#include "history/view/media/history_view_sticker_player.h"
#include "info/userpic/info_userpic_emoji_builder_common.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "ui/painter.h"
#include "ui/rect.h"

namespace UserpicBuilder {

PreviewPainter::PreviewPainter(int size)
: _size(size)
, _emojiSize(base::SafeRound(_size / M_SQRT2))
, _frameGeometry(Rect(Size(_size)) - Margins((_size - _emojiSize) / 2))
, _frameRect(Rect(_frameGeometry.size()))
, _mask(
	_frameRect.size() * style::DevicePixelRatio(),
	QImage::Format_ARGB32_Premultiplied)
, _frame(_mask.size(), QImage::Format_ARGB32_Premultiplied) {
	_frame.setDevicePixelRatio(style::DevicePixelRatio());
	_mask.setDevicePixelRatio(style::DevicePixelRatio());
	_mask.fill(Qt::transparent);
	{
		auto p = QPainter(&_mask);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBg);
		constexpr auto kFrameRadiusPercent = 25;
		p.drawRoundedRect(
			_frameRect,
			kFrameRadiusPercent,
			kFrameRadiusPercent,
			Qt::RelativeSize);
	}
}

DocumentData *PreviewPainter::document() const {
	return _media ? _media->owner().get() : nullptr;
}

void PreviewPainter::setPlayOnce(bool value) {
	_playOnce = value;
}

void PreviewPainter::setDocument(
		not_null<DocumentData*> document,
		Fn<void()> updateCallback) {
	if (_media && (document == _media->owner())) {
		return;
	}
	_lifetime.destroy();

	const auto sticker = document->sticker();
	Assert(sticker != nullptr);
	_media = document->createMediaView();
	_media->checkStickerLarge();
	_media->goodThumbnailWanted();

	if (_playOnce) {
		_firstFrameShown = false;
		_paused = false;
	} else {
		_paused = true;
	}

	rpl::single() | rpl::then(
		document->owner().session().downloaderTaskFinished()
	) | rpl::start_with_next([=] {
		if (!_media->loaded()) {
			return;
		}
		_lifetime.destroy();
		const auto emojiSize = Size(_size * style::DevicePixelRatio());
		if (sticker->isLottie()) {
			_player = std::make_unique<HistoryView::LottiePlayer>(
				ChatHelpers::LottiePlayerFromDocument(
					_media.get(),
					//
					ChatHelpers::StickerLottieSize::EmojiInteractionReserved7,
					emojiSize,
					Lottie::Quality::High));
		} else if (sticker->isWebm()) {
			_player = std::make_unique<HistoryView::WebmPlayer>(
				_media->owner()->location(),
				_media->bytes(),
				emojiSize);
		} else if (sticker) {
			_player = std::make_unique<HistoryView::StaticStickerPlayer>(
				_media->owner()->location(),
				_media->bytes(),
				emojiSize);
		}
		if (_player) {
			_player->setRepaintCallback(updateCallback);
		} else {
			updateCallback();
		}
	}, _lifetime);
}

void PreviewPainter::paintBackground(QPainter &p, const QImage &image) {
	p.drawImage(0, 0, image);
}

bool PreviewPainter::paintForeground(QPainter &p) {
	if (_player && _player->ready()) {
		const auto c = _media->owner()->emojiUsesTextColor() ? 255 : 0;
		auto frame = _player->frame(
			Size(_emojiSize),
			QColor(c, c, c, c),
			false,
			crl::now(),
			_paused);

		if (_playOnce) {
			if (!_firstFrameShown && (frame.index == 1)) {
				_firstFrameShown = true;
			} else if (_firstFrameShown && !frame.index) {
				_paused = true;
			}
		}

		_frame.fill(Qt::transparent);
		{
			QPainter q(&_frame);
			if (frame.image.width() == frame.image.height()) {
				q.drawImage(_frameRect, frame.image);
			} else {
				auto frameRect = Rect(frame.image.size().scaled(
					_frameRect.size(),
					Qt::KeepAspectRatio));
				frameRect.moveCenter(_frameRect.center());
				q.drawImage(frameRect, frame.image);
			}
			q.setCompositionMode(QPainter::CompositionMode_DestinationIn);
			q.drawImage(0, 0, _mask);
		}

		p.drawImage(_frameGeometry.topLeft(), _frame);
		if (!_paused) {
			_player->markFrameShown();
		}
		return true;
	}
	return false;
}

EmojiUserpic::EmojiUserpic(
	not_null<Ui::RpWidget*> parent,
	const QSize &size,
	bool isForum)
: Ui::RpWidget(parent)
, _forum(isForum)
, _painter(size.width())
, _duration(st::slideWrapDuration) {
	resize(size);
}

void EmojiUserpic::setDocument(not_null<DocumentData*> document) {
	if (!_playOnce.has_value()) {
		const auto &c = document->owner().session().appConfig();
		_playOnce = !c.get<bool>(u"upload_markup_video"_q, false);
	}
	_painter.setDocument(document, [=] { update(); });
	_painter.setPlayOnce(*_playOnce);
}

void EmojiUserpic::result(int size, Fn<void(UserpicBuilder::Result)> done) {
	const auto painter = lifetime().make_state<PreviewPainter>(size);
	// Reset to the first frame.
	const auto document = _painter.document();
	const auto callback = [=] {
		auto background = GenerateGradient(Size(size), _colors, false);

		{
			constexpr auto kAttemptsToDrawFirstFrame = 3000;
			auto attempts = 0;
			auto p = QPainter(&background);
			while (attempts < kAttemptsToDrawFirstFrame) {
				if (painter->paintForeground(p)) {
					break;
				}
				attempts++;
			}
		}
		if (*_playOnce && document) {
			done({ std::move(background), document->id, _colors });
		} else {
			done({ std::move(background) });
		}
	};
	if (document) {
		painter->setDocument(document, callback);
	} else {
		callback();
	}
}

void EmojiUserpic::setGradientColors(std::vector<QColor> colors) {
	if (_colors == colors) {
		return;
	}
	if (const auto colors = base::take(_colors); !colors.empty()) {
		_previousImage = GenerateGradient(size(), colors, !_forum, _forum);
	}
	_colors = std::move(colors);
	{
		_image = GenerateGradient(size(), _colors, !_forum, _forum);
	}
	if (_duration) {
		_animation.stop();
		_animation.start([=] { update(); }, 0., 1., _duration);
	} else {
		update();
	}
}

void EmojiUserpic::paintEvent(QPaintEvent *event) {
	auto p = QPainter(this);

	if (_animation.animating() && !_previousImage.isNull()) {
		_painter.paintBackground(p, _previousImage);

		p.setOpacity(_animation.value(1.));
	}

	_painter.paintBackground(p, _image);

	p.setOpacity(1.);
	_painter.paintForeground(p);
}

void EmojiUserpic::setDuration(crl::time duration) {
	_duration = duration;
}

} // namespace UserpicBuilder
