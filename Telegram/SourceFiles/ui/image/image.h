/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/image/image_prepare.h"

class HistoryItem;

namespace Images {

[[nodiscard]] QByteArray ExpandInlineBytes(const QByteArray &bytes);
[[nodiscard]] QImage FromInlineBytes(const QByteArray &bytes);

[[nodiscard]] QSize GetSizeForDocument(
	const QVector<MTPDocumentAttribute> &attributes);

class Source {
public:
	Source() = default;
	Source(const Source &other) = delete;
	Source(Source &&other) = delete;
	Source &operator=(const Source &other) = delete;
	Source &operator=(Source &&other) = delete;
	virtual ~Source() = default;

	virtual void load() = 0;
	virtual QImage takeLoaded() = 0;

	virtual int width() = 0;
	virtual int height() = 0;

};

} // namespace Images

class Image final {
public:
	explicit Image(std::unique_ptr<Images::Source> &&source);

	static not_null<Image*> Empty(); // 1x1 transparent
	static not_null<Image*> BlankMedia(); // 1x1 black

	QImage original() const;

	const QPixmap &pix(
		Data::FileOrigin origin,
		int32 w = 0,
		int32 h = 0) const;
	const QPixmap &pixRounded(
		Data::FileOrigin origin,
		int32 w = 0,
		int32 h = 0,
		ImageRoundRadius radius = ImageRoundRadius::None,
		RectParts corners = RectPart::AllCorners) const;
	const QPixmap &pixBlurred(
		Data::FileOrigin origin,
		int32 w = 0,
		int32 h = 0) const;
	const QPixmap &pixColored(
		Data::FileOrigin origin,
		style::color add,
		int32 w = 0,
		int32 h = 0) const;
	const QPixmap &pixBlurredColored(
		Data::FileOrigin origin,
		style::color add,
		int32 w = 0,
		int32 h = 0) const;
	const QPixmap &pixSingle(
		Data::FileOrigin origin,
		int32 w,
		int32 h,
		int32 outerw,
		int32 outerh,
		ImageRoundRadius radius,
		RectParts corners = RectPart::AllCorners,
		const style::color *colored = nullptr) const;
	const QPixmap &pixBlurredSingle(
		Data::FileOrigin origin,
		int32 w,
		int32 h,
		int32 outerw,
		int32 outerh,
		ImageRoundRadius radius,
		RectParts corners = RectPart::AllCorners) const;
	const QPixmap &pixCircled(
		Data::FileOrigin origin,
		int32 w = 0,
		int32 h = 0) const;
	const QPixmap &pixBlurredCircled(
		Data::FileOrigin origin,
		int32 w = 0,
		int32 h = 0) const;
	QPixmap pixNoCache(
		Data::FileOrigin origin,
		int w = 0,
		int h = 0,
		Images::Options options = 0,
		int outerw = -1,
		int outerh = -1,
		const style::color *colored = nullptr) const;
	QPixmap pixColoredNoCache(
		Data::FileOrigin origin,
		style::color add,
		int32 w = 0,
		int32 h = 0,
		bool smooth = false) const;
	QPixmap pixBlurredColoredNoCache(
		Data::FileOrigin origin,
		style::color add,
		int32 w,
		int32 h = 0) const;

	int width() const {
		return _source->width();
	}
	int height() const {
		return _source->height();
	}
	QSize size() const {
		return { width(), height() };
	}
	void load();

	bool loaded() const;
	bool isNull() const;

	~Image();

private:
	void checkSource() const;
	void invalidateSizeCache() const;

	std::unique_ptr<Images::Source> _source;
	mutable QMap<uint64, QPixmap> _sizesCache;
	mutable QImage _data;

};
