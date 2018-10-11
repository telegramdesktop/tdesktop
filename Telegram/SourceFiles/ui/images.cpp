/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/images.h"

#include "ui/image.h"
#include "mainwidget.h"
#include "storage/localstorage.h"
#include "storage/cache/storage_cache_database.h"
#include "platform/platform_specific.h"
#include "auth_session.h"
#include "history/history_item.h"
#include "history/history.h"
#include "data/data_session.h"

namespace Images {
namespace {

TG_FORCE_INLINE uint64 blurGetColors(const uchar *p) {
	return (uint64)p[0] + ((uint64)p[1] << 16) + ((uint64)p[2] << 32) + ((uint64)p[3] << 48);
}

const QPixmap &circleMask(int width, int height) {
	Assert(Global::started());

	uint64 key = uint64(uint32(width)) << 32 | uint64(uint32(height));

	Global::CircleMasksMap &masks(Global::RefCircleMasks());
	auto i = masks.constFind(key);
	if (i == masks.cend()) {
		QImage mask(width, height, QImage::Format_ARGB32_Premultiplied);
		{
			Painter p(&mask);
			PainterHighQualityEnabler hq(p);

			p.setCompositionMode(QPainter::CompositionMode_Source);
			p.fillRect(0, 0, width, height, Qt::transparent);
			p.setBrush(Qt::white);
			p.setPen(Qt::NoPen);
			p.drawEllipse(0, 0, width, height);
		}
		mask.setDevicePixelRatio(cRetinaFactor());
		i = masks.insert(key, App::pixmapFromImageInPlace(std::move(mask)));
	}
	return i.value();
}

} // namespace

QPixmap PixmapFast(QImage &&image) {
	Expects(image.format() == QImage::Format_ARGB32_Premultiplied
		|| image.format() == QImage::Format_RGB32);

	return QPixmap::fromImage(std::move(image), Qt::NoFormatConversion);
}

QImage prepareBlur(QImage img) {
	auto ratio = img.devicePixelRatio();
	auto fmt = img.format();
	if (fmt != QImage::Format_RGB32 && fmt != QImage::Format_ARGB32_Premultiplied) {
		img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
		img.setDevicePixelRatio(ratio);
		Assert(!img.isNull());
	}

	uchar *pix = img.bits();
	if (pix) {
		int w = img.width(), h = img.height(), wold = w, hold = h;
		const int radius = 3;
		const int r1 = radius + 1;
		const int div = radius * 2 + 1;
		const int stride = w * 4;
		if (radius < 16 && div < w && div < h && stride <= w * 4) {
			bool withalpha = img.hasAlphaChannel();
			if (withalpha) {
				QImage imgsmall(w, h, img.format());
				{
					Painter p(&imgsmall);
					PainterHighQualityEnabler hq(p);

					p.setCompositionMode(QPainter::CompositionMode_Source);
					p.fillRect(0, 0, w, h, Qt::transparent);
					p.drawImage(QRect(radius, radius, w - 2 * radius, h - 2 * radius), img, QRect(0, 0, w, h));
				}
				imgsmall.setDevicePixelRatio(ratio);
				auto was = img;
				img = std::move(imgsmall);
				imgsmall = QImage();
				Assert(!img.isNull());

				pix = img.bits();
				if (!pix) return was;
			}
			uint64 *rgb = new uint64[w * h];

			int x, y, i;

			int yw = 0;
			const int we = w - r1;
			for (y = 0; y < h; y++) {
				uint64 cur = blurGetColors(&pix[yw]);
				uint64 rgballsum = -radius * cur;
				uint64 rgbsum = cur * ((r1 * (r1 + 1)) >> 1);

				for (i = 1; i <= radius; i++) {
					uint64 cur = blurGetColors(&pix[yw + i * 4]);
					rgbsum += cur * (r1 - i);
					rgballsum += cur;
				}

				x = 0;

#define update(start, middle, end) \
rgb[y * w + x] = (rgbsum >> 4) & 0x00FF00FF00FF00FFLL; \
rgballsum += blurGetColors(&pix[yw + (start) * 4]) - 2 * blurGetColors(&pix[yw + (middle) * 4]) + blurGetColors(&pix[yw + (end) * 4]); \
rgbsum += rgballsum; \
x++;

				while (x < r1) {
					update(0, x, x + r1);
				}
				while (x < we) {
					update(x - r1, x, x + r1);
				}
				while (x < w) {
					update(x - r1, x, w - 1);
				}

#undef update

				yw += stride;
			}

			const int he = h - r1;
			for (x = 0; x < w; x++) {
				uint64 rgballsum = -radius * rgb[x];
				uint64 rgbsum = rgb[x] * ((r1 * (r1 + 1)) >> 1);
				for (i = 1; i <= radius; i++) {
					rgbsum += rgb[i * w + x] * (r1 - i);
					rgballsum += rgb[i * w + x];
				}

				y = 0;
				int yi = x * 4;

#define update(start, middle, end) \
uint64 res = rgbsum >> 4; \
pix[yi] = res & 0xFF; \
pix[yi + 1] = (res >> 16) & 0xFF; \
pix[yi + 2] = (res >> 32) & 0xFF; \
pix[yi + 3] = (res >> 48) & 0xFF; \
rgballsum += rgb[x + (start) * w] - 2 * rgb[x + (middle) * w] + rgb[x + (end) * w]; \
rgbsum += rgballsum; \
y++; \
yi += stride;

				while (y < r1) {
					update(0, y, y + r1);
				}
				while (y < he) {
					update(y - r1, y, y + r1);
				}
				while (y < h) {
					update(y - r1, y, h - 1);
				}

#undef update
			}

			delete[] rgb;
		}
	}
	return img;
}

void prepareCircle(QImage &img) {
	Assert(!img.isNull());

	img.setDevicePixelRatio(cRetinaFactor());
	img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
	Assert(!img.isNull());

	QPixmap mask = circleMask(img.width(), img.height());
	Painter p(&img);
	p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
	p.drawPixmap(0, 0, mask);
}

void prepareRound(
		QImage &image,
		QImage *cornerMasks,
		RectParts corners,
		QRect target) {
	if (target.isNull()) {
		target = QRect(QPoint(), image.size());
	} else {
		Assert(QRect(QPoint(), image.size()).contains(target));
	}
	auto cornerWidth = cornerMasks[0].width();
	auto cornerHeight = cornerMasks[0].height();
	auto imageWidth = image.width();
	auto imageHeight = image.height();
	if (imageWidth < 2 * cornerWidth || imageHeight < 2 * cornerHeight) {
		return;
	}
	constexpr auto imageIntsPerPixel = 1;
	auto imageIntsPerLine = (image.bytesPerLine() >> 2);
	Assert(image.depth() == static_cast<int>((imageIntsPerPixel * sizeof(uint32)) << 3));
	Assert(image.bytesPerLine() == (imageIntsPerLine << 2));

	auto ints = reinterpret_cast<uint32*>(image.bits());
	auto intsTopLeft = ints + target.x() + target.y() * imageWidth;
	auto intsTopRight = ints + target.x() + target.width() - cornerWidth + target.y() * imageWidth;
	auto intsBottomLeft = ints + target.x() + (target.y() + target.height() - cornerHeight) * imageWidth;
	auto intsBottomRight = ints + target.x() + target.width() - cornerWidth + (target.y() + target.height() - cornerHeight) * imageWidth;
	auto maskCorner = [&](uint32 *imageInts, const QImage &mask) {
		auto maskWidth = mask.width();
		auto maskHeight = mask.height();
		auto maskBytesPerPixel = (mask.depth() >> 3);
		auto maskBytesPerLine = mask.bytesPerLine();
		auto maskBytesAdded = maskBytesPerLine - maskWidth * maskBytesPerPixel;
		auto maskBytes = mask.constBits();
		Assert(maskBytesAdded >= 0);
		Assert(mask.depth() == (maskBytesPerPixel << 3));
		auto imageIntsAdded = imageIntsPerLine - maskWidth * imageIntsPerPixel;
		Assert(imageIntsAdded >= 0);
		for (auto y = 0; y != maskHeight; ++y) {
			for (auto x = 0; x != maskWidth; ++x) {
				auto opacity = static_cast<anim::ShiftedMultiplier>(*maskBytes) + 1;
				*imageInts = anim::unshifted(anim::shifted(*imageInts) * opacity);
				maskBytes += maskBytesPerPixel;
				imageInts += imageIntsPerPixel;
			}
			maskBytes += maskBytesAdded;
			imageInts += imageIntsAdded;
		}
	};
	if (corners & RectPart::TopLeft) maskCorner(intsTopLeft, cornerMasks[0]);
	if (corners & RectPart::TopRight) maskCorner(intsTopRight, cornerMasks[1]);
	if (corners & RectPart::BottomLeft) maskCorner(intsBottomLeft, cornerMasks[2]);
	if (corners & RectPart::BottomRight) maskCorner(intsBottomRight, cornerMasks[3]);
}

void prepareRound(
		QImage &image,
		ImageRoundRadius radius,
		RectParts corners,
		QRect target) {
	if (!static_cast<int>(corners)) {
		return;
	} else if (radius == ImageRoundRadius::Ellipse) {
		Assert((corners & RectPart::AllCorners) == RectPart::AllCorners);
		Assert(target.isNull());
		prepareCircle(image);
	}
	Assert(!image.isNull());

	image.setDevicePixelRatio(cRetinaFactor());
	image = std::move(image).convertToFormat(QImage::Format_ARGB32_Premultiplied);
	Assert(!image.isNull());

	auto masks = App::cornersMask(radius);
	prepareRound(image, masks, corners, target);
}

QImage prepareColored(style::color add, QImage image) {
	auto format = image.format();
	if (format != QImage::Format_RGB32 && format != QImage::Format_ARGB32_Premultiplied) {
		image = std::move(image).convertToFormat(QImage::Format_ARGB32_Premultiplied);
	}

	if (auto pix = image.bits()) {
		int ca = int(add->c.alphaF() * 0xFF), cr = int(add->c.redF() * 0xFF), cg = int(add->c.greenF() * 0xFF), cb = int(add->c.blueF() * 0xFF);
		const int w = image.width(), h = image.height(), size = w * h * 4;
		for (auto i = index_type(); i < size; i += 4) {
			int b = pix[i], g = pix[i + 1], r = pix[i + 2], a = pix[i + 3], aca = a * ca;
			pix[i + 0] = uchar(b + ((aca * (cb - b)) >> 16));
			pix[i + 1] = uchar(g + ((aca * (cg - g)) >> 16));
			pix[i + 2] = uchar(r + ((aca * (cr - r)) >> 16));
			pix[i + 3] = uchar(a + ((aca * (0xFF - a)) >> 16));
		}
	}
	return image;
}

QImage prepareOpaque(QImage image) {
	if (image.hasAlphaChannel()) {
		image = std::move(image).convertToFormat(QImage::Format_ARGB32_Premultiplied);
		auto ints = reinterpret_cast<uint32*>(image.bits());
		auto bg = anim::shifted(st::imageBgTransparent->c);
		auto width = image.width();
		auto height = image.height();
		auto addPerLine = (image.bytesPerLine() / sizeof(uint32)) - width;
		for (auto y = 0; y != height; ++y) {
			for (auto x = 0; x != width; ++x) {
				auto components = anim::shifted(*ints);
				*ints++ = anim::unshifted(components * 256 + bg * (256 - anim::getAlpha(components)));
			}
			ints += addPerLine;
		}
	}
	return image;
}

QImage prepare(QImage img, int w, int h, Images::Options options, int outerw, int outerh, const style::color *colored) {
	Assert(!img.isNull());
	if (options & Images::Option::Blurred) {
		img = prepareBlur(std::move(img));
		Assert(!img.isNull());
	}
	if (w <= 0 || (w == img.width() && (h <= 0 || h == img.height()))) {
	} else if (h <= 0) {
		img = img.scaledToWidth(w, (options & Images::Option::Smooth) ? Qt::SmoothTransformation : Qt::FastTransformation);
		Assert(!img.isNull());
	} else {
		img = img.scaled(w, h, Qt::IgnoreAspectRatio, (options & Images::Option::Smooth) ? Qt::SmoothTransformation : Qt::FastTransformation);
		Assert(!img.isNull());
	}
	if (outerw > 0 && outerh > 0) {
		outerw *= cIntRetinaFactor();
		outerh *= cIntRetinaFactor();
		if (outerw != w || outerh != h) {
			img.setDevicePixelRatio(cRetinaFactor());
			auto result = QImage(outerw, outerh, QImage::Format_ARGB32_Premultiplied);
			result.setDevicePixelRatio(cRetinaFactor());
			if (options & Images::Option::TransparentBackground) {
				result.fill(Qt::transparent);
			}
			{
				QPainter p(&result);
				if (w < outerw || h < outerh) {
					p.fillRect(0, 0, result.width(), result.height(), st::imageBg);
				}
				p.drawImage((result.width() - img.width()) / (2 * cIntRetinaFactor()), (result.height() - img.height()) / (2 * cIntRetinaFactor()), img);
			}
			img = result;
			Assert(!img.isNull());
		}
	}
	auto corners = [](Images::Options options) {
		return ((options & Images::Option::RoundedTopLeft) ? RectPart::TopLeft : RectPart::None)
			| ((options & Images::Option::RoundedTopRight) ? RectPart::TopRight : RectPart::None)
			| ((options & Images::Option::RoundedBottomLeft) ? RectPart::BottomLeft : RectPart::None)
			| ((options & Images::Option::RoundedBottomRight) ? RectPart::BottomRight : RectPart::None);
	};
	if (options & Images::Option::Circled) {
		prepareCircle(img);
		Assert(!img.isNull());
	} else if (options & Images::Option::RoundedLarge) {
		prepareRound(img, ImageRoundRadius::Large, corners(options));
		Assert(!img.isNull());
	} else if (options & Images::Option::RoundedSmall) {
		prepareRound(img, ImageRoundRadius::Small, corners(options));
		Assert(!img.isNull());
	}
	if (options & Images::Option::Colored) {
		Assert(colored != nullptr);
		img = prepareColored(*colored, std::move(img));
	}
	img.setDevicePixelRatio(cRetinaFactor());
	return img;
}

} // namespace Images

ImagePtr::ImagePtr() : _data(Image::Blank()) {
}

ImagePtr::ImagePtr(const QString &file, QByteArray format)
: _data(Images::details::Create(file, format)) {
}

ImagePtr::ImagePtr(const QString &url, QSize box)
: _data(Images::details::Create(url, box)) {
}

ImagePtr::ImagePtr(const QString &url, int width, int height)
: _data(Images::details::Create(url, width, height)) {
}

ImagePtr::ImagePtr(const QByteArray &filecontent, QByteArray format)
: _data(Images::details::Create(filecontent, format)) {
}

ImagePtr::ImagePtr(
	const QByteArray &filecontent,
	QByteArray format,
	const QPixmap &pixmap)
: _data(Images::details::Create(filecontent, format, pixmap)) {
}

ImagePtr::ImagePtr(const QPixmap &pixmap, QByteArray format)
: _data(Images::details::Create(pixmap, format)) {
}

ImagePtr::ImagePtr(const StorageImageLocation &location, int32 size)
: _data(Images::details::Create(location, size)) {
}

ImagePtr::ImagePtr(
	const StorageImageLocation &location,
	const QByteArray &bytes)
: _data(Images::details::Create(location, bytes)) {
}

ImagePtr::ImagePtr(const MTPWebDocument &location)
: _data(Images::details::Create(location)) {
}

ImagePtr::ImagePtr(const MTPWebDocument &location, QSize box)
: _data(Images::details::Create(location, box)) {
}

ImagePtr::ImagePtr(
	const WebFileLocation &location,
	int width,
	int height,
	int size)
: _data(Images::details::Create(location, width, height, size)) {
}

ImagePtr::ImagePtr(const WebFileLocation &location, QSize box, int size)
: _data(Images::details::Create(location, box, size)) {
}

ImagePtr::ImagePtr(const GeoPointLocation &location)
: _data(Images::details::Create(location)) {
}

ImagePtr::ImagePtr(
	int32 width,
	int32 height,
	const MTPFileLocation &location,
	ImagePtr def)
: _data((location.type() != mtpc_fileLocation)
	? def.get()
	: (Image*)(Images::details::Create(
		StorageImageLocation(width, height, location.c_fileLocation())))) {
}

ImagePtr::ImagePtr(int32 width, int32 height)
: _data(Images::details::Create(width, height)) {
}

Image *ImagePtr::operator->() const {
	return _data;
}
Image *ImagePtr::get() const {
	return _data;
}

ImagePtr::operator bool() const {
	return !_data->isNull();
}

StorageImageLocation StorageImageLocation::Null;
WebFileLocation WebFileLocation::Null;

StorageImageLocation::StorageImageLocation(
	int32 width,
	int32 height,
	int32 dc,
	const uint64 &volume,
	int32 local,
	const uint64 &secret,
	const QByteArray &fileReference)
: _widthheight(packIntInt(width, height))
, _dclocal(packIntInt(dc, local))
, _volume(volume)
, _secret(secret)
, _fileReference(fileReference) {
}

StorageImageLocation::StorageImageLocation(
	int32 width,
	int32 height,
	const MTPDfileLocation &location)
: StorageImageLocation(
	width,
	height,
	location.vdc_id.v,
	location.vvolume_id.v,
	location.vlocal_id.v,
	location.vsecret.v,
	location.vfile_reference.v) {
}

ReadAccessEnabler::ReadAccessEnabler(const PsFileBookmark *bookmark)
: _bookmark(bookmark)
, _failed(_bookmark ? !_bookmark->enable() : false) {
}

ReadAccessEnabler::ReadAccessEnabler(
	const std::shared_ptr<PsFileBookmark> &bookmark)
: _bookmark(bookmark.get())
, _failed(_bookmark ? !_bookmark->enable() : false) {
}

ReadAccessEnabler::~ReadAccessEnabler() {
	if (_bookmark && !_failed) _bookmark->disable();
}

FileLocation::FileLocation(const QString &name) : fname(name) {
	if (fname.isEmpty()) {
		size = 0;
	} else {
		setBookmark(psPathBookmark(name));

		QFileInfo f(name);
		if (f.exists()) {
			qint64 s = f.size();
			if (s > INT_MAX) {
				fname = QString();
				_bookmark = nullptr;
				size = 0;
			} else {
				modified = f.lastModified();
				size = qint32(s);
			}
		} else {
			fname = QString();
			_bookmark = nullptr;
			size = 0;
		}
	}
}

bool FileLocation::check() const {
	if (fname.isEmpty()) return false;

	ReadAccessEnabler enabler(_bookmark);
	if (enabler.failed()) {
		const_cast<FileLocation*>(this)->_bookmark = nullptr;
	}

	QFileInfo f(name());
	if (!f.isReadable()) return false;

	quint64 s = f.size();
	if (s > INT_MAX) {
		DEBUG_LOG(("File location check: Wrong size %1").arg(s));
		return false;
	}

	if (qint32(s) != size) {
		DEBUG_LOG(("File location check: Wrong size %1 when should be %2").arg(s).arg(size));
		return false;
	}
	auto realModified = f.lastModified();
	if (realModified != modified) {
		DEBUG_LOG(("File location check: Wrong last modified time %1 when should be %2").arg(realModified.toMSecsSinceEpoch()).arg(modified.toMSecsSinceEpoch()));
		return false;
	}
	return true;
}

const QString &FileLocation::name() const {
	return _bookmark ? _bookmark->name(fname) : fname;
}

QByteArray FileLocation::bookmark() const {
	return _bookmark ? _bookmark->bookmark() : QByteArray();
}

void FileLocation::setBookmark(const QByteArray &bm) {
	_bookmark.reset(bm.isEmpty() ? nullptr : new PsFileBookmark(bm));
}

bool FileLocation::accessEnable() const {
	return isEmpty() ? false : (_bookmark ? _bookmark->enable() : true);
}

void FileLocation::accessDisable() const {
	return _bookmark ? _bookmark->disable() : (void)0;
}
