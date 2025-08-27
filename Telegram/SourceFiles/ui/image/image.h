/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/image/image_prepare.h"

class QPainterPath;

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

	[[nodiscard]] const QPixmap &pix(
			QSize size,
			const Images::PrepareArgs &args = {}) const {
		return cached(size.width(), size.height(), args, false);
	}
	[[nodiscard]] const QPixmap &pix(
			int w,
			int h,
			const Images::PrepareArgs &args = {}) const {
		return cached(w, h, args, false);
	}
	[[nodiscard]] const QPixmap &pix(
			int w = 0,
			const Images::PrepareArgs &args = {}) const {
		return cached(w, 0, args, false);
	}

	[[nodiscard]] const QPixmap &pixSingle(
			QSize size,
			const Images::PrepareArgs &args = {}) const {
		return cached(size.width(), size.height(), args, true);
	}
	[[nodiscard]] const QPixmap &pixSingle(
			int w,
			int h,
			const Images::PrepareArgs &args = {}) const {
		return cached(w, h, args, true);
	}
	[[nodiscard]] const QPixmap &pixSingle(
			int w = 0,
			const Images::PrepareArgs &args = {}) const {
		return cached(w, 0, args, true);
	}

	[[nodiscard]] QPixmap pixNoCache(
			QSize size,
			const Images::PrepareArgs &args = {}) const {
		return prepare(size.width(), size.height(), args);
	}
	[[nodiscard]] QPixmap pixNoCache(
			int w,
			int h,
			const Images::PrepareArgs &args = {}) const {
		return prepare(w, h, args);
	}
	[[nodiscard]] QPixmap pixNoCache(
			int w = 0,
			const Images::PrepareArgs &args = {}) const {
		return prepare(w, 0, args);
	}

private:
	[[nodiscard]] QPixmap prepare(
		int w,
		int h,
		const Images::PrepareArgs &args) const;
	[[nodiscard]] const QPixmap &cached(
		int w,
		int h,
		const Images::PrepareArgs &args,
		bool single) const;

	const QImage _data;
	mutable base::flat_map<uint64, QPixmap> _cache;

};
