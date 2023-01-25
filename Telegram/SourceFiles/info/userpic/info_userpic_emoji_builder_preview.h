/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

#include "ui/effects/animations.h"

namespace Data {
class DocumentMedia;
} // namespace Data

namespace HistoryView {
class StickerPlayer;
} // namespace HistoryView

class DocumentData;

namespace UserpicBuilder {

class PreviewPainter final {
public:
	PreviewPainter(int size);

	[[nodiscard]] not_null<DocumentData*> document() const;

	void setDocument(
		not_null<DocumentData*> document,
		Fn<void()> updateCallback);

	void paintBackground(QPainter &p, const QImage &image);
	bool paintForeground(QPainter &p);

private:
	const int _size;
	const int _emojiSize;
	const QRect _frameRect;

	std::shared_ptr<Data::DocumentMedia> _media;
	std::unique_ptr<HistoryView::StickerPlayer> _player;
	bool _paused = true;
	rpl::lifetime _lifetime;

};

class EmojiUserpic final : public Ui::RpWidget {
public:
	EmojiUserpic(not_null<Ui::RpWidget*> parent, const QSize &size);

	void result(int size, Fn<void(QImage &&image)> done);
	void setGradientColors(std::vector<QColor> colors);
	void setDocument(not_null<DocumentData*> document);
	void setDuration(crl::time duration);

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	PreviewPainter _painter;

	QImage _previousImage;
	QImage _image;
	std::vector<QColor> _colors;

	crl::time _duration;
	Ui::Animations::Simple _animation;

};

} // namespace UserpicBuilder
