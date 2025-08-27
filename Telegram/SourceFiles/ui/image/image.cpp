/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/image/image.h"

#include "storage/cache/storage_cache_database.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "ui/ui_utility.h"

using namespace Images;

namespace Images {
namespace {

[[nodiscard]] uint64 PixKey(int width, int height, Options options) {
	return static_cast<uint64>(width)
		| (static_cast<uint64>(height) << 24)
		| (static_cast<uint64>(options) << 48);
}

[[nodiscard]] uint64 SinglePixKey(Options options) {
	return PixKey(0, 0, options);
}

[[nodiscard]] Options OptionsByArgs(const PrepareArgs &args) {
	return args.options | (args.colored ? Option::Colorize : Option::None);
}

[[nodiscard]] uint64 PixKey(int width, int height, const PrepareArgs &args) {
	return PixKey(width, height, OptionsByArgs(args));
}

[[nodiscard]] uint64 SinglePixKey(const PrepareArgs &args) {
	return SinglePixKey(OptionsByArgs(args));
}

} // namespace

} // namespace Images

Image::Image(const QString &path)
: Image(Read({ .path = path }).image) {
}

Image::Image(const QByteArray &content)
: Image(Read({ .content = content }).image) {
}

Image::Image(QImage &&data)
: _data(data.isNull() ? Empty()->original() : std::move(data)) {
	Expects(!_data.isNull());
}

not_null<Image*> Image::Empty() {
	static auto result = Image([] {
		const auto factor = style::DevicePixelRatio();
		auto data = QImage(
			factor,
			factor,
			QImage::Format_ARGB32_Premultiplied);
		data.fill(Qt::transparent);
		data.setDevicePixelRatio(style::DevicePixelRatio());
		return data;
	}());
	return &result;
}

not_null<Image*> Image::BlankMedia() {
	static auto result = Image([] {
		const auto factor = style::DevicePixelRatio();
		auto data = QImage(
			factor,
			factor,
			QImage::Format_ARGB32_Premultiplied);
		data.fill(Qt::black);
		data.setDevicePixelRatio(style::DevicePixelRatio());
		return data;
	}());
	return &result;
}

QImage Image::original() const {
	return _data;
}

const QPixmap &Image::cached(
		int w,
		int h,
		const Images::PrepareArgs &args,
		bool single) const {
	const auto ratio = style::DevicePixelRatio();
	if (w <= 0 || !width() || !height()) {
		w = width();
	} else if (h <= 0) {
		h = std::max(int(int64(height()) * w / width()), 1) * ratio;
		w *= ratio;
	} else {
		w *= ratio;
		h *= ratio;
	}
	const auto outer = args.outer;
	const auto size = outer.isEmpty() ? QSize(w, h) : outer * ratio;
	const auto k = single ? SinglePixKey(args) : PixKey(w, h, args);
	const auto i = _cache.find(k);
	return (i != _cache.cend() && i->second.size() == size)
		? i->second
		: _cache.emplace_or_assign(k, prepare(w, h, args)).first->second;
}

QPixmap Image::prepare(int w, int h, const Images::PrepareArgs &args) const {
	if (_data.isNull()) {
		if (h <= 0 && height() > 0) {
			h = qRound(width() * w / float64(height()));
		}
		return Empty()->prepare(w, h, args);
	}

	auto outer = args.outer;
	if (!isNull() || outer.isEmpty()) {
		return Ui::PixmapFromImage(Prepare(_data, w, h, args));
	}

	const auto ratio = style::DevicePixelRatio();
	const auto outerw = outer.width() * ratio;
	const auto outerh = outer.height() * ratio;

	auto result = QImage(
		QSize(outerw, outerh),
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);

	auto p = QPainter(&result);
	if (w < outerw) {
		p.fillRect(0, 0, (outerw - w) / 2, result.height(), Qt::black);
		p.fillRect(((outerw - w) / 2) + w, 0, result.width() - (((outerw - w) / 2) + w), result.height(), Qt::black);
	}
	if (h < outerh) {
		p.fillRect(qMax(0, (outerw - w) / 2), 0, qMin(result.width(), w), (outerh - h) / 2, Qt::black);
		p.fillRect(qMax(0, (outerw - w) / 2), ((outerh - h) / 2) + h, qMin(result.width(), w), result.height() - (((outerh - h) / 2) + h), Qt::black);
	}
	p.fillRect(qMax(0, (outerw - w) / 2), qMax(0, (outerh - h) / 2), qMin(result.width(), w), qMin(result.height(), h), Qt::white);
	p.end();

	result = Round(std::move(result), args.options);
	if (args.colored) {
		result = Colored(std::move(result), *args.colored);
	}
	return Ui::PixmapFromImage(std::move(result));
}
