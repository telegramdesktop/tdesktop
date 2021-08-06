/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/image/image_prepare.h"

class QPainterPath;

namespace Images {

[[nodiscard]] QByteArray ExpandInlineBytes(const QByteArray &bytes);
[[nodiscard]] QImage FromInlineBytes(const QByteArray &bytes);
[[nodiscard]] QPainterPath PathFromInlineBytes(const QByteArray &bytes);

} // namespace Images

class Image final {
public:
	explicit Image(const QString &path);
	explicit Image(const QByteArray &content);
	explicit Image(QImage &&data);

	[[nodiscard]] static not_null<Image*> Empty(); // 1x1 transparent
	[[nodiscard]] static not_null<Image*> BlankMedia(); // 1x1 black

	[[nodiscard]] int width() const {
		return _data.width();
	}
	[[nodiscard]] int height() const {
		return _data.height();
	}
	[[nodiscard]] QSize size() const {
		return { width(), height() };
	}

	[[nodiscard]] bool isNull() const {
		return (this == Empty());
	}

	[[nodiscard]] QImage original() const;

	[[nodiscard]] const QPixmap &pix(int w = 0, int h = 0) const;
	[[nodiscard]] const QPixmap &pixRounded(
		int w = 0,
		int h = 0,
		ImageRoundRadius radius = ImageRoundRadius::None,
		RectParts corners = RectPart::AllCorners) const;
	[[nodiscard]] const QPixmap &pixBlurred(int w = 0, int h = 0) const;
	[[nodiscard]] const QPixmap &pixColored(style::color add, int w = 0, int h = 0) const;
	[[nodiscard]] const QPixmap &pixBlurredColored(
		style::color add,
		int w = 0,
		int h = 0) const;
	[[nodiscard]] const QPixmap &pixSingle(
		int w,
		int h,
		int outerw,
		int outerh,
		ImageRoundRadius radius,
		RectParts corners = RectPart::AllCorners,
		const style::color *colored = nullptr) const;
	[[nodiscard]] const QPixmap &pixBlurredSingle(
		int w,
		int h,
		int outerw,
		int outerh,
		ImageRoundRadius radius,
		RectParts corners = RectPart::AllCorners,
		const style::color *colored = nullptr) const;
	[[nodiscard]] const QPixmap &pixCircled(int w = 0, int h = 0) const;
	[[nodiscard]] const QPixmap &pixBlurredCircled(int w = 0, int h = 0) const;
	[[nodiscard]] QPixmap pixNoCache(
		int w = 0,
		int h = 0,
		Images::Options options = 0,
		int outerw = -1,
		int outerh = -1,
		const style::color *colored = nullptr) const;
	[[nodiscard]] QPixmap pixColoredNoCache(
		style::color add,
		int w = 0,
		int h = 0,
		bool smooth = false) const;
	[[nodiscard]] QPixmap pixBlurredColoredNoCache(
		style::color add,
		int w,
		int h = 0) const;

private:
	const QImage _data;
	mutable base::flat_map<uint64, QPixmap> _cache;

};
