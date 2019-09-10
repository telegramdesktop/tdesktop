/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/image/image_prepare.h"

#include "ui/effects/animation_value.h"
#include "ui/style/style_core.h"
#include "ui/painter.h"
#include "base/flat_map.h"
#include "styles/palette.h"
#include "styles/style_basic.h"

namespace Images {
namespace {

TG_FORCE_INLINE uint64 blurGetColors(const uchar *p) {
	return (uint64)p[0] + ((uint64)p[1] << 16) + ((uint64)p[2] << 32) + ((uint64)p[3] << 48);
}

const QImage &circleMask(QSize size) {
	uint64 key = (uint64(uint32(size.width())) << 32)
		| uint64(uint32(size.height()));

	static auto masks = base::flat_map<uint64, QImage>();
	const auto i = masks.find(key);
	if (i != end(masks)) {
		return i->second;
	}
	auto mask = QImage(
		size,
		QImage::Format_ARGB32_Premultiplied);
	mask.fill(Qt::transparent);
	{
		QPainter p(&mask);
		PainterHighQualityEnabler hq(p);
		p.setBrush(Qt::white);
		p.setPen(Qt::NoPen);
		p.drawEllipse(QRect(QPoint(), size));
	}
	return masks.emplace(key, std::move(mask)).first->second;
}

std::array<QImage, 4> PrepareCornersMask(int radius) {
	auto result = std::array<QImage, 4>();
	const auto side = radius * style::DevicePixelRatio();
	auto full = QImage(
		QSize(side, side) * 3,
		QImage::Format_ARGB32_Premultiplied);
	full.fill(Qt::transparent);
	{
		QPainter p(&full);
		PainterHighQualityEnabler hq(p);

		p.setPen(Qt::NoPen);
		p.setBrush(Qt::white);
		p.drawRoundedRect(0, 0, side * 3, side * 3, side, side);
	}
	result[0] = full.copy(0, 0, side, side);
	result[1] = full.copy(side * 2, 0, side, side);
	result[2] = full.copy(0, side * 2, side, side);
	result[3] = full.copy(side * 2, side * 2, side, side);
	for (auto &image : result) {
		image.setDevicePixelRatio(style::DevicePixelRatio());
	}
	return result;
}

} // namespace

QPixmap PixmapFast(QImage &&image) {
	Expects(image.format() == QImage::Format_ARGB32_Premultiplied
		|| image.format() == QImage::Format_RGB32);

	return QPixmap::fromImage(std::move(image), Qt::NoFormatConversion);
}

const std::array<QImage, 4> &CornersMask(ImageRoundRadius radius) {
	if (radius == ImageRoundRadius::Large) {
		static auto Mask = PrepareCornersMask(st::roundRadiusLarge);
		return Mask;
	} else {
		static auto Mask = PrepareCornersMask(st::roundRadiusSmall);
		return Mask;
	}
}

std::array<QImage, 4> PrepareCorners(
		ImageRoundRadius radius,
		const style::color &color) {
	auto result = CornersMask(radius);
	for (auto &image : result) {
		style::colorizeImage(image, color->c, &image);
	}
	return result;
}

QImage prepareBlur(QImage img) {
	if (img.isNull()) {
		return img;
	}
	const auto ratio = img.devicePixelRatio();
	const auto fmt = img.format();
	if (fmt != QImage::Format_RGB32 && fmt != QImage::Format_ARGB32_Premultiplied) {
		img = std::move(img).convertToFormat(QImage::Format_ARGB32_Premultiplied);
		img.setDevicePixelRatio(ratio);
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
					QPainter p(&imgsmall);
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

QImage BlurLargeImage(QImage image, int radius) {
	const auto width = image.width();
	const auto height = image.height();
	if (width <= radius || height <= radius || radius < 1) {
		return image;
	}

	if (image.format() != QImage::Format_RGB32
		&& image.format() != QImage::Format_ARGB32_Premultiplied) {
		image = std::move(image).convertToFormat(
			QImage::Format_ARGB32_Premultiplied);
	}
	const auto pixels = image.bits();

	const auto width_m1 = width - 1;
	const auto height_m1 = height - 1;
	const auto widthxheight = width * height;
	const auto div = 2 * radius + 1;
	const auto radius_p1 = radius + 1;
	const auto divsum = radius_p1 * radius_p1;

	const auto dvcount = 256 * divsum;
	const auto buffers = (div * 3) // stack
		+ std::max(width, height) // vmin
		+ widthxheight * 3 // rgb
		+ dvcount; // dv
	auto storage = std::vector<int>(buffers);
	auto taken = 0;
	const auto take = [&](int size) {
		const auto result = gsl::make_span(storage).subspan(taken, size);
		taken += size;
		return result;
	};

	// Small buffers
	const auto stack = take(div * 3).data();
	const auto vmin = take(std::max(width, height)).data();

	// Large buffers
	const auto rgb = take(widthxheight * 3).data();
	const auto dvs = take(dvcount);

	auto &&ints = ranges::view::ints;
	for (auto &&[value, index] : ranges::view::zip(dvs, ints(0, ranges::unreachable))) {
		value = (index / divsum);
	}
	const auto dv = dvs.data();

	// Variables
	auto stackpointer = 0;
	for (const auto x : ints(0, width)) {
		vmin[x] = std::min(x + radius_p1, width_m1);
	}
	for (const auto y : ints(0, height)) {
		auto rinsum = 0;
		auto ginsum = 0;
		auto binsum = 0;
		auto routsum = 0;
		auto goutsum = 0;
		auto boutsum = 0;
		auto rsum = 0;
		auto gsum = 0;
		auto bsum = 0;

		const auto y_width = y * width;
		for (const auto i : ints(-radius, radius + 1)) {
			const auto sir = &stack[(i + radius) * 3];
			const auto x = std::clamp(i, 0, width_m1);
			const auto offset = (y_width + x) * 4;
			sir[0] = pixels[offset];
			sir[1] = pixels[offset + 1];
			sir[2] = pixels[offset + 2];

			const auto rbs = radius_p1 - std::abs(i);
			rsum += sir[0] * rbs;
			gsum += sir[1] * rbs;
			bsum += sir[2] * rbs;

			if (i > 0) {
				rinsum += sir[0];
				ginsum += sir[1];
				binsum += sir[2];
			} else {
				routsum += sir[0];
				goutsum += sir[1];
				boutsum += sir[2];
			}
		}
		stackpointer = radius;

		for (const auto x : ints(0, width)) {
			const auto position = (y_width + x) * 3;
			rgb[position] = dv[rsum];
			rgb[position + 1] = dv[gsum];
			rgb[position + 2] = dv[bsum];

			rsum -= routsum;
			gsum -= goutsum;
			bsum -= boutsum;

			const auto stackstart = (stackpointer - radius + div) % div;
			const auto sir = &stack[stackstart * 3];

			routsum -= sir[0];
			goutsum -= sir[1];
			boutsum -= sir[2];

			const auto offset = (y_width + vmin[x]) * 4;
			sir[0] = pixels[offset];
			sir[1] = pixels[offset + 1];
			sir[2] = pixels[offset + 2];
			rinsum += sir[0];
			ginsum += sir[1];
			binsum += sir[2];

			rsum += rinsum;
			gsum += ginsum;
			bsum += binsum;
			{
				stackpointer = (stackpointer + 1) % div;
				const auto sir = &stack[stackpointer * 3];

				routsum += sir[0];
				goutsum += sir[1];
				boutsum += sir[2];

				rinsum -= sir[0];
				ginsum -= sir[1];
				binsum -= sir[2];
			}
		}
	}

	for (const auto y : ints(0, height)) {
		vmin[y] = std::min(y + radius_p1, height_m1) * width;
	}
	for (const auto x : ints(0, width)) {
		auto rinsum = 0;
		auto ginsum = 0;
		auto binsum = 0;
		auto routsum = 0;
		auto goutsum = 0;
		auto boutsum = 0;
		auto rsum = 0;
		auto gsum = 0;
		auto bsum = 0;
		for (const auto i : ints(-radius, radius + 1)) {
			const auto y = std::clamp(i, 0, height_m1);
			const auto position = (y * width + x) * 3;
			const auto sir = &stack[(i + radius) * 3];

			sir[0] = rgb[position];
			sir[1] = rgb[position + 1];
			sir[2] = rgb[position + 2];

			const auto rbs = radius_p1 - std::abs(i);
			rsum += sir[0] * rbs;
			gsum += sir[1] * rbs;
			bsum += sir[2] * rbs;
			if (i > 0) {
				rinsum += sir[0];
				ginsum += sir[1];
				binsum += sir[2];
			} else {
				routsum += sir[0];
				goutsum += sir[1];
				boutsum += sir[2];
			}
		}
		stackpointer = radius;
		for (const auto y : ints(0, height)) {
			const auto offset = (y * width + x) * 4;
			pixels[offset] = dv[rsum];
			pixels[offset + 1] = dv[gsum];
			pixels[offset + 2] = dv[bsum];
			rsum -= routsum;
			gsum -= goutsum;
			bsum -= boutsum;

			const auto stackstart = (stackpointer - radius + div) % div;
			const auto sir = &stack[stackstart * 3];

			routsum -= sir[0];
			goutsum -= sir[1];
			boutsum -= sir[2];

			const auto position = (vmin[y] + x) * 3;
			sir[0] = rgb[position];
			sir[1] = rgb[position + 1];
			sir[2] = rgb[position + 2];

			rinsum += sir[0];
			ginsum += sir[1];
			binsum += sir[2];

			rsum += rinsum;
			gsum += ginsum;
			bsum += binsum;
			{
				stackpointer = (stackpointer + 1) % div;
				const auto sir = &stack[stackpointer * 3];

				routsum += sir[0];
				goutsum += sir[1];
				boutsum += sir[2];

				rinsum -= sir[0];
				ginsum -= sir[1];
				binsum -= sir[2];
			}
		}
	}
	return image;
}

void prepareCircle(QImage &img) {
	Assert(!img.isNull());

	img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
	Assert(!img.isNull());

	QPainter p(&img);
	p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
	p.drawImage(
		QRect(QPoint(), img.size() / img.devicePixelRatio()),
		circleMask(img.size()));
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
		return;
	}
	Assert(!image.isNull());

	image.setDevicePixelRatio(style::DevicePixelRatio());
	image = std::move(image).convertToFormat(
		QImage::Format_ARGB32_Premultiplied);
	Assert(!image.isNull());

	auto masks = CornersMask(radius);
	prepareRound(image, masks.data(), corners, target);
}

QImage prepareColored(style::color add, QImage image) {
	return prepareColored(add->c, std::move(image));
}

QImage prepareColored(QColor add, QImage image) {
	const auto format = image.format();
	if (format != QImage::Format_RGB32 && format != QImage::Format_ARGB32_Premultiplied) {
		image = std::move(image).convertToFormat(QImage::Format_ARGB32_Premultiplied);
	}

	if (const auto pix = image.bits()) {
		const auto ca = int(add.alphaF() * 0xFF);
		const auto cr = int(add.redF() * 0xFF);
		const auto cg = int(add.greenF() * 0xFF);
		const auto cb = int(add .blueF() * 0xFF);
		const auto w = image.width();
		const auto h = image.height();
		const auto size = w * h * 4;
		for (auto i = index_type(); i != size; i += 4) {
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
		const auto pixelRatio = style::DevicePixelRatio();
		outerw *= pixelRatio;
		outerh *= pixelRatio;
		if (outerw != w || outerh != h) {
			img.setDevicePixelRatio(pixelRatio);
			auto result = QImage(outerw, outerh, QImage::Format_ARGB32_Premultiplied);
			result.setDevicePixelRatio(pixelRatio);
			if (options & Images::Option::TransparentBackground) {
				result.fill(Qt::transparent);
			}
			{
				QPainter p(&result);
				if (!(options & Images::Option::TransparentBackground)) {
					if (w < outerw || h < outerh) {
						p.fillRect(0, 0, result.width(), result.height(), st::imageBg);
					}
				}
				p.drawImage((result.width() - img.width()) / (2 * pixelRatio), (result.height() - img.height()) / (2 * pixelRatio), img);
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
	img.setDevicePixelRatio(style::DevicePixelRatio());
	return img;
}

} // namespace Images
