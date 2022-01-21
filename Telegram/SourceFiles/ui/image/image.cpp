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

QByteArray ExpandInlineBytes(const QByteArray &bytes) {
	if (bytes.size() < 3 || bytes[0] != '\x01') {
		return QByteArray();
	}
	const char header[] = "\xff\xd8\xff\xe0\x00\x10\x4a\x46\x49"
		"\x46\x00\x01\x01\x00\x00\x01\x00\x01\x00\x00\xff\xdb\x00\x43\x00\x28\x1c"
		"\x1e\x23\x1e\x19\x28\x23\x21\x23\x2d\x2b\x28\x30\x3c\x64\x41\x3c\x37\x37"
		"\x3c\x7b\x58\x5d\x49\x64\x91\x80\x99\x96\x8f\x80\x8c\x8a\xa0\xb4\xe6\xc3"
		"\xa0\xaa\xda\xad\x8a\x8c\xc8\xff\xcb\xda\xee\xf5\xff\xff\xff\x9b\xc1\xff"
		"\xff\xff\xfa\xff\xe6\xfd\xff\xf8\xff\xdb\x00\x43\x01\x2b\x2d\x2d\x3c\x35"
		"\x3c\x76\x41\x41\x76\xf8\xa5\x8c\xa5\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8"
		"\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8"
		"\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8"
		"\xf8\xf8\xf8\xf8\xf8\xff\xc0\x00\x11\x08\x00\x00\x00\x00\x03\x01\x22\x00"
		"\x02\x11\x01\x03\x11\x01\xff\xc4\x00\x1f\x00\x00\x01\x05\x01\x01\x01\x01"
		"\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x01\x02\x03\x04\x05\x06\x07\x08"
		"\x09\x0a\x0b\xff\xc4\x00\xb5\x10\x00\x02\x01\x03\x03\x02\x04\x03\x05\x05"
		"\x04\x04\x00\x00\x01\x7d\x01\x02\x03\x00\x04\x11\x05\x12\x21\x31\x41\x06"
		"\x13\x51\x61\x07\x22\x71\x14\x32\x81\x91\xa1\x08\x23\x42\xb1\xc1\x15\x52"
		"\xd1\xf0\x24\x33\x62\x72\x82\x09\x0a\x16\x17\x18\x19\x1a\x25\x26\x27\x28"
		"\x29\x2a\x34\x35\x36\x37\x38\x39\x3a\x43\x44\x45\x46\x47\x48\x49\x4a\x53"
		"\x54\x55\x56\x57\x58\x59\x5a\x63\x64\x65\x66\x67\x68\x69\x6a\x73\x74\x75"
		"\x76\x77\x78\x79\x7a\x83\x84\x85\x86\x87\x88\x89\x8a\x92\x93\x94\x95\x96"
		"\x97\x98\x99\x9a\xa2\xa3\xa4\xa5\xa6\xa7\xa8\xa9\xaa\xb2\xb3\xb4\xb5\xb6"
		"\xb7\xb8\xb9\xba\xc2\xc3\xc4\xc5\xc6\xc7\xc8\xc9\xca\xd2\xd3\xd4\xd5\xd6"
		"\xd7\xd8\xd9\xda\xe1\xe2\xe3\xe4\xe5\xe6\xe7\xe8\xe9\xea\xf1\xf2\xf3\xf4"
		"\xf5\xf6\xf7\xf8\xf9\xfa\xff\xc4\x00\x1f\x01\x00\x03\x01\x01\x01\x01\x01"
		"\x01\x01\x01\x01\x00\x00\x00\x00\x00\x00\x01\x02\x03\x04\x05\x06\x07\x08"
		"\x09\x0a\x0b\xff\xc4\x00\xb5\x11\x00\x02\x01\x02\x04\x04\x03\x04\x07\x05"
		"\x04\x04\x00\x01\x02\x77\x00\x01\x02\x03\x11\x04\x05\x21\x31\x06\x12\x41"
		"\x51\x07\x61\x71\x13\x22\x32\x81\x08\x14\x42\x91\xa1\xb1\xc1\x09\x23\x33"
		"\x52\xf0\x15\x62\x72\xd1\x0a\x16\x24\x34\xe1\x25\xf1\x17\x18\x19\x1a\x26"
		"\x27\x28\x29\x2a\x35\x36\x37\x38\x39\x3a\x43\x44\x45\x46\x47\x48\x49\x4a"
		"\x53\x54\x55\x56\x57\x58\x59\x5a\x63\x64\x65\x66\x67\x68\x69\x6a\x73\x74"
		"\x75\x76\x77\x78\x79\x7a\x82\x83\x84\x85\x86\x87\x88\x89\x8a\x92\x93\x94"
		"\x95\x96\x97\x98\x99\x9a\xa2\xa3\xa4\xa5\xa6\xa7\xa8\xa9\xaa\xb2\xb3\xb4"
		"\xb5\xb6\xb7\xb8\xb9\xba\xc2\xc3\xc4\xc5\xc6\xc7\xc8\xc9\xca\xd2\xd3\xd4"
		"\xd5\xd6\xd7\xd8\xd9\xda\xe2\xe3\xe4\xe5\xe6\xe7\xe8\xe9\xea\xf2\xf3\xf4"
		"\xf5\xf6\xf7\xf8\xf9\xfa\xff\xda\x00\x0c\x03\x01\x00\x02\x11\x03\x11\x00"
		"\x3f\x00";
	const char footer[] = "\xff\xd9";
	auto real = QByteArray(header, sizeof(header) - 1);
	real[164] = bytes[1];
	real[166] = bytes[2];
	return real
		+ bytes.mid(3)
		+ QByteArray::fromRawData(footer, sizeof(footer) - 1);
}

QImage FromInlineBytes(const QByteArray &bytes) {
	return Read({ .content = ExpandInlineBytes(bytes) }).image;
}

// Thanks TDLib for code.
QByteArray ExpandPathInlineBytes(const QByteArray &bytes) {
	auto result = QByteArray();
	result.reserve(3 * (bytes.size() + 1));
	result.append('M');
	for (unsigned char c : bytes) {
		if (c >= 128 + 64) {
			result.append("AACAAAAHAAALMAAAQASTAVAAAZ"
				"aacaaaahaaalmaaaqastava.az0123456789-,"[c - 128 - 64]);
		} else {
			if (c >= 128) {
				result.append(',');
			} else if (c >= 64) {
				result.append('-');
			}
			//char buffer[3] = { 0 }; // Unavailable on macOS < 10.15.
			//std::to_chars(buffer, buffer + 3, (c & 63));
			//result.append(buffer);
			result.append(QByteArray::number(c & 63));
		}
	}
	result.append('z');
	return result;
}

QPainterPath PathFromInlineBytes(const QByteArray &bytes) {
	if (bytes.isEmpty()) {
		return QPainterPath();
	}
	const auto expanded = ExpandPathInlineBytes(bytes);
	const auto path = expanded.data(); // Allows checking for '\0' by index.
	auto position = 0;

	const auto isAlpha = [](char c) {
		c |= 0x20;
		return 'a' <= c && c <= 'z';
	};
	const auto isDigit = [](char c) {
		return '0' <= c && c <= '9';
	};
	const auto skipCommas = [&] {
		while (path[position] == ',') {
			++position;
		}
	};
	const auto getNumber = [&] {
		skipCommas();
		auto sign = 1;
		if (path[position] == '-') {
			sign = -1;
			++position;
		}
		double res = 0;
		while (isDigit(path[position])) {
			res = res * 10 + path[position++] - '0';
		}
		if (path[position] == '.') {
			++position;
			double mul = 0.1;
			while (isDigit(path[position])) {
				res += (path[position] - '0') * mul;
				mul *= 0.1;
				++position;
			}
		}
		return sign * res;
	};

	auto result = QPainterPath();
	auto x = 0.;
	auto y = 0.;
	while (path[position] != '\0') {
		skipCommas();
		if (path[position] == '\0') {
			break;
		}

		while (path[position] == 'm' || path[position] == 'M') {
			auto command = path[position++];
			do {
				if (command == 'm') {
					x += getNumber();
					y += getNumber();
				} else {
					x = getNumber();
					y = getNumber();
				}
				skipCommas();
			} while (path[position] != '\0' && !isAlpha(path[position]));
		}

		auto xStart = x;
		auto yStart = y;
		result.moveTo(xStart, yStart);
		auto haveLastEndControlPoint = false;
		auto xLastEndControlPoint = 0.;
		auto yLastEndControlPoint = 0.;
		auto isClosed = false;
		auto command = '-';
		while (!isClosed) {
			skipCommas();
			if (path[position] == '\0') {
				LOG(("SVG Error: Receive unclosed path: %1"
					).arg(QString::fromLatin1(path)));
				return QPainterPath();
			}
			if (isAlpha(path[position])) {
				command = path[position++];
			}
			switch (command) {
			case 'l':
			case 'L':
			case 'h':
			case 'H':
			case 'v':
			case 'V':
				if (command == 'l' || command == 'h') {
					x += getNumber();
				} else if (command == 'L' || command == 'H') {
					x = getNumber();
				}
				if (command == 'l' || command == 'v') {
					y += getNumber();
				} else if (command == 'L' || command == 'V') {
					y = getNumber();
				}
				result.lineTo(x, y);
				haveLastEndControlPoint = false;
				break;
			case 'C':
			case 'c':
			case 'S':
			case 's': {
				auto xStartControlPoint = 0.;
				auto yStartControlPoint = 0.;
				if (command == 'S' || command == 's') {
					if (haveLastEndControlPoint) {
						xStartControlPoint = 2 * x - xLastEndControlPoint;
						yStartControlPoint = 2 * y - yLastEndControlPoint;
					} else {
						xStartControlPoint = x;
						yStartControlPoint = y;
					}
				} else {
					xStartControlPoint = getNumber();
					yStartControlPoint = getNumber();
					if (command == 'c') {
						xStartControlPoint += x;
						yStartControlPoint += y;
					}
				}

				xLastEndControlPoint = getNumber();
				yLastEndControlPoint = getNumber();
				if (command == 'c' || command == 's') {
					xLastEndControlPoint += x;
					yLastEndControlPoint += y;
				}
				haveLastEndControlPoint = true;

				if (command == 'c' || command == 's') {
					x += getNumber();
					y += getNumber();
				} else {
					x = getNumber();
					y = getNumber();
				}
				result.cubicTo(
					xStartControlPoint,
					yStartControlPoint,
					xLastEndControlPoint,
					yLastEndControlPoint,
					x,
					y);
				break;
			}
			case 'm':
			case 'M':
				--position;
				[[fallthrough]];
			case 'z':
			case 'Z':
				if (x != xStart || y != yStart) {
					x = xStart;
					y = yStart;
					result.lineTo(x, y);
				}
				isClosed = true;
				break;
			default:
				LOG(("SVG Error: Receive invalid command %1 at pos %2: %3"
					).arg(command
					).arg(position
					).arg(QString::fromLatin1(path)));
				return QPainterPath();
			}
		}
	}
	return result;
}

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
		const auto factor = cIntRetinaFactor();
		auto data = QImage(
			factor,
			factor,
			QImage::Format_ARGB32_Premultiplied);
		data.fill(Qt::transparent);
		data.setDevicePixelRatio(cRetinaFactor());
		return data;
	}());
	return &result;
}

not_null<Image*> Image::BlankMedia() {
	static auto result = Image([] {
		const auto factor = cIntRetinaFactor();
		auto data = QImage(
			factor,
			factor,
			QImage::Format_ARGB32_Premultiplied);
		data.fill(Qt::black);
		data.setDevicePixelRatio(cRetinaFactor());
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
		w = width() * ratio;
	} else if (h <= 0) {
		h = std::max(int(int64(height()) * w / width()), 1) * ratio;
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
