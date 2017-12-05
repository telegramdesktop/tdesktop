/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "ui/empty_userpic.h"

#include "data/data_peer.h"
#include "styles/style_history.h"

namespace Ui {

EmptyUserpic::EmptyUserpic(const style::color &color, const QString &name)
: _color(color) {
	fillString(name);
}

template <typename Callback>
void EmptyUserpic::paint(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		int size,
		Callback paintBackground) const {
	x = rtl() ? (outerWidth - x - size) : x;

	const auto fontsize = (size * 13) / 33;
	auto font = st::historyPeerUserpicFont->f;
	font.setPixelSize(fontsize);

	PainterHighQualityEnabler hq(p);
	p.setBrush(_color);
	p.setPen(Qt::NoPen);
	paintBackground();

	p.setFont(font);
	p.setBrush(Qt::NoBrush);
	p.setPen(st::historyPeerUserpicFg);
	p.drawText(QRect(x, y, size, size), _string, QTextOption(style::al_center));
}

void EmptyUserpic::paint(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		int size) const {
	paint(p, x, y, outerWidth, size, [&p, x, y, size] {
		p.drawEllipse(x, y, size, size);
	});
}

void EmptyUserpic::paintRounded(Painter &p, int x, int y, int outerWidth, int size) const {
	paint(p, x, y, outerWidth, size, [&p, x, y, size] {
		p.drawRoundedRect(x, y, size, size, st::buttonRadius, st::buttonRadius);
	});
}

void EmptyUserpic::paintSquare(Painter &p, int x, int y, int outerWidth, int size) const {
	paint(p, x, y, outerWidth, size, [&p, x, y, size] {
		p.fillRect(x, y, size, size, p.brush());
	});
}

void EmptyUserpic::PaintSavedMessages(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		int size) {
	const auto &bg = st::historyPeer4UserpicBg;
	const auto &fg = st::historyPeerUserpicFg;
	PaintSavedMessages(p, x, y, outerWidth, size, bg, fg);
}

void EmptyUserpic::PaintSavedMessages(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		int size,
		const style::color &bg,
		const style::color &fg) {
	x = rtl() ? (outerWidth - x - size) : x;

	PainterHighQualityEnabler hq(p);
	p.setBrush(bg);
	p.setPen(Qt::NoPen);
	p.drawEllipse(x, y, size, size);

	// |<----width----->|
	//
	// XXXXXXXXXXXXXXXXXX  ---
	// X                X   |
	// X                X   |
	// X                X   |
	// X                X height
	// X       XX       X   |     ---
	// X     XX  XX     X   |      |
	// X   XX      XX   X   |     add
	// X XX          XX X   |      |
	// XX              XX  ---    ---

	const auto thinkness = std::round(size * 0.055);
	const auto increment = int(thinkness) % 2 + (size % 2);
	const auto width = std::round(size * 0.15) * 2 + increment;
	const auto height = std::round(size * 0.19) * 2 + increment;
	const auto add = std::round(size * 0.064);

	const auto left = x + (size - width) / 2;
	const auto top = y + (size - height) / 2;
	const auto right = left + width;
	const auto bottom = top + height;
	const auto middle = (left + right) / 2;
	const auto half = (top + bottom) / 2;

	p.setBrush(Qt::NoBrush);
	auto pen = fg->p;
	pen.setWidthF(thinkness);
	pen.setCapStyle(Qt::FlatCap);

	{
		// XXXXXXXXXXXXXXXXXX
		// X                X
		// X                X
		// X                X
		// X                X
		// X                X

		pen.setJoinStyle(Qt::RoundJoin);
		p.setPen(pen);
		QPainterPath path;
		path.moveTo(left, half);
		path.lineTo(left, top);
		path.lineTo(right, top);
		path.lineTo(right, half);
		p.drawPath(path);
	}
	{
		// X                X
		// X       XX       X
		// X     XX  XX     X
		// X   XX      XX   X
		// X XX          XX X
		// XX              XX

		pen.setJoinStyle(Qt::MiterJoin);
		p.setPen(pen);
		QPainterPath path;
		path.moveTo(left, half);
		path.lineTo(left, bottom);
		path.lineTo(middle, bottom - add);
		path.lineTo(right, bottom);
		path.lineTo(right, half);
		p.drawPath(path);
	}
}

StorageKey EmptyUserpic::uniqueKey() const {
	auto first = 0xFFFFFFFF00000000ULL | anim::getPremultiplied(_color->c);
	auto second = uint64(0);
	memcpy(&second, _string.constData(), qMin(sizeof(second), _string.size() * sizeof(QChar)));
	return StorageKey(first, second);
}

QPixmap EmptyUserpic::generate(int size) {
	auto result = QImage(QSize(size, size) * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(cRetinaFactor());
	result.fill(Qt::transparent);
	{
		Painter p(&result);
		paint(p, 0, 0, size, size);
	}
	return App::pixmapFromImageInPlace(std::move(result));
}

void EmptyUserpic::fillString(const QString &name) {
	QList<QString> letters;
	QList<int> levels;

	auto level = 0;
	auto letterFound = false;
	auto ch = name.constData(), end = ch + name.size();
	while (ch != end) {
		auto emojiLength = 0;
		if (auto emoji = Ui::Emoji::Find(ch, end, &emojiLength)) {
			ch += emojiLength;
		} else if (ch->isHighSurrogate()) {
			++ch;
			if (ch != end && ch->isLowSurrogate()) {
				++ch;
			}
		} else if (!letterFound && ch->isLetterOrNumber()) {
			letterFound = true;
			if (ch + 1 != end && chIsDiac(*(ch + 1))) {
				letters.push_back(QString(ch, 2));
				levels.push_back(level);
				++ch;
			} else {
				letters.push_back(QString(ch, 1));
				levels.push_back(level);
			}
			++ch;
		} else {
			if (*ch == ' ') {
				level = 0;
				letterFound = false;
			} else if (letterFound && *ch == '-') {
				level = 1;
				letterFound = true;
			}
			++ch;
		}
	}

	// We prefer the second letter to be after ' ', but it can also be after '-'.
	_string = QString();
	if (!letters.isEmpty()) {
		_string += letters.front();
		auto bestIndex = 0;
		auto bestLevel = 2;
		for (auto i = letters.size(); i != 1;) {
			if (levels[--i] < bestLevel) {
				bestIndex = i;
				bestLevel = levels[i];
			}
		}
		if (bestIndex > 0) {
			_string += letters[bestIndex];
		}
	}
	_string = _string.toUpper();
}

EmptyUserpic::~EmptyUserpic() = default;

} // namespace Ui
