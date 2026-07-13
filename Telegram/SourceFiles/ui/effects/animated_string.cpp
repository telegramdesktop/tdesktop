/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/animated_string.h"

#include "ui/painter.h"

namespace Ui {
namespace {

enum class RegionType {
	Equal,
	New,
	Old,
};

struct Region {
	RegionType type = RegionType::Equal;
	QString text;
};

[[nodiscard]] std::vector<QString> Tokenize(
		const QString &text,
		bool splitByWords) {
	auto result = std::vector<QString>();
	const auto length = int(text.size());
	if (!splitByWords) {
		result.reserve(length);
		for (auto i = 0; i != length; ++i) {
			result.push_back(text.mid(i, 1));
		}
		return result;
	}
	auto start = 0;
	for (auto i = 0; i <= length; ++i) {
		if (i == length || text[i] == QChar(' ')) {
			result.push_back(
				text.mid(start, i - start + (i < length ? 1 : 0)));
			start = i + 1;
		}
	}
	return result;
}

[[nodiscard]] QString Join(
		const std::vector<QString> &tokens,
		int from,
		int to) {
	auto result = QString();
	for (auto i = from; i != to; ++i) {
		result += tokens[i];
	}
	return result;
}

void AppendPart(
		std::vector<Region> &regions,
		RegionType type,
		const QString &text,
		bool enforceByLetter) {
	if (enforceByLetter && text.size() > 1) {
		for (auto i = 0; i != int(text.size()); ++i) {
			regions.push_back({ type, text.mid(i, 1) });
		}
	} else {
		regions.push_back({ type, text });
	}
}

[[nodiscard]] std::vector<Region> Diff(
		const std::vector<QString> &oldTokens,
		const std::vector<QString> &newTokens,
		const AnimatedString::Options &options) {
	auto regions = std::vector<Region>();
	const auto oldLength = int(oldTokens.size());
	const auto newLength = int(newTokens.size());
	const auto equals = [&](int newIndex, int oldIndex) {
		return (newIndex >= 0)
			&& (oldIndex >= 0)
			&& (newIndex < newLength)
			&& (oldIndex < oldLength)
			&& (newTokens[newIndex] == oldTokens[oldIndex]);
	};
	const auto enforceByLetter = options.enforceByLetter;

	const auto minLength = std::min(newLength, oldLength);
	if (!options.preserveIndex) {
		auto astart = 0;
		auto bstart = 0;
		auto equal = true;
		auto a = 0;
		auto b = 0;
		for (; a <= minLength; ++a) {
			const auto thisEqual = (a < minLength) && equals(a, b);
			if (equal != thisEqual || a == minLength) {
				if (a == minLength) {
					a = newLength;
					b = oldLength;
				}
				const auto alen = a - astart;
				const auto blen = b - bstart;
				if (alen > 0 || blen > 0) {
					if (alen == blen && equal) {
						regions.push_back({
							RegionType::Equal,
							Join(newTokens, astart, a),
						});
					} else {
						if (alen > 0) {
							AppendPart(
								regions,
								RegionType::New,
								Join(newTokens, astart, a),
								enforceByLetter);
						}
						if (blen > 0) {
							AppendPart(
								regions,
								RegionType::Old,
								Join(oldTokens, bstart, b),
								enforceByLetter);
						}
					}
				}
				equal = thisEqual;
				astart = a;
				bstart = b;
			}
			if (thisEqual) {
				++b;
			}
		}
		return regions;
	}

	if (options.startFromEnd) {
		auto indexes = std::vector<int>();
		auto equal = true;
		auto start = 0;
		auto eq = true;
		for (auto i = 0; i <= minLength; ++i) {
			const auto a = newLength - i - 1;
			const auto b = oldLength - i - 1;
			const auto thisEqual = (a >= 0) && (b >= 0) && equals(a, b);
			if (equal != thisEqual || i == minLength) {
				if (i - start > 0) {
					if (indexes.empty()) {
						eq = equal;
					}
					indexes.push_back(i - start);
				}
				equal = thisEqual;
				start = i;
			}
		}
		auto a = newLength - minLength;
		auto b = oldLength - minLength;
		if (a > 0) {
			AppendPart(
				regions,
				RegionType::New,
				Join(newTokens, 0, a),
				enforceByLetter);
		}
		if (b > 0) {
			AppendPart(
				regions,
				RegionType::Old,
				Join(oldTokens, 0, b),
				enforceByLetter);
		}
		for (auto i = int(indexes.size()) - 1; i >= 0; --i) {
			const auto count = indexes[i];
			if (((i % 2) == 0) == eq) {
				regions.push_back({
					RegionType::Equal,
					(newLength > oldLength)
						? Join(newTokens, a, a + count)
						: Join(oldTokens, b, b + count),
				});
			} else {
				AppendPart(
					regions,
					RegionType::New,
					Join(newTokens, a, a + count),
					enforceByLetter);
				AppendPart(
					regions,
					RegionType::Old,
					Join(oldTokens, b, b + count),
					enforceByLetter);
			}
			a += count;
			b += count;
		}
		return regions;
	}

	auto equal = true;
	auto start = 0;
	for (auto i = 0; i <= minLength; ++i) {
		const auto thisEqual = (i < minLength) && equals(i, i);
		if (equal != thisEqual || i == minLength) {
			if (i - start > 0) {
				if (equal) {
					AppendPart(
						regions,
						RegionType::Equal,
						Join(newTokens, start, i),
						enforceByLetter);
				} else {
					AppendPart(
						regions,
						RegionType::New,
						Join(newTokens, start, i),
						enforceByLetter);
					AppendPart(
						regions,
						RegionType::Old,
						Join(oldTokens, start, i),
						enforceByLetter);
				}
			}
			equal = thisEqual;
			start = i;
		}
	}
	if (newLength - minLength > 0) {
		AppendPart(
			regions,
			RegionType::New,
			Join(newTokens, minLength, newLength),
			enforceByLetter);
	}
	if (oldLength - minLength > 0) {
		AppendPart(
			regions,
			RegionType::Old,
			Join(oldTokens, minLength, oldLength),
			enforceByLetter);
	}
	return regions;
}

} // namespace

AnimatedString::AnimatedString(
	const style::font &font,
	Fn<void()> update)
: AnimatedString(font, std::move(update), Options()) {
}

AnimatedString::AnimatedString(
	const style::font &font,
	Fn<void()> update,
	Options options)
: _font(font)
, _update(std::move(update))
, _options(std::move(options)) {
}

const QString &AnimatedString::text() const {
	return _currentText;
}

void AnimatedString::setText(const QString &text, bool animated) {
	if (!_started) {
		animated = false;
	}
	_started = true;
	if (!animated) {
		_scheduled.reset();
		_animation.stop();
		setInstant(text);
		if (_update) {
			_update();
		}
		return;
	} else if (text == _currentText) {
		return;
	} else if (_animation.animating()) {
		_scheduled = text;
		return;
	}
	realSetText(text);
}

void AnimatedString::setInstant(const QString &text) {
	_currentText = text;
	_oldText = QString();
	_currentParts.clear();
	_oldParts.clear();
	_oldWidth = 0.;
	if (text.isEmpty()) {
		_currentWidth = 0.;
	} else {
		const auto width = float64(_font->width(text));
		_currentParts.push_back({ text, 0., width, -1 });
		_currentWidth = width;
	}
}

void AnimatedString::realSetText(const QString &text) {
	_scheduled.reset();
	_oldText = _currentText;
	_currentText = text;

	const auto regions = Diff(
		Tokenize(_oldText, _options.splitByWords),
		Tokenize(_currentText, _options.splitByWords),
		_options);

	_currentParts.clear();
	_oldParts.clear();
	_currentWidth = _oldWidth = 0.;
	for (const auto &region : regions) {
		const auto width = float64(_font->width(region.text));
		if (region.type == RegionType::Equal) {
			const auto currentIndex = int(_currentParts.size());
			const auto oldIndex = int(_oldParts.size());
			_currentParts.push_back({
				region.text,
				_currentWidth,
				width,
				oldIndex,
			});
			_oldParts.push_back({
				region.text,
				_oldWidth,
				width,
				currentIndex,
			});
			_currentWidth += width;
			_oldWidth += width;
		} else if (region.type == RegionType::New) {
			_currentParts.push_back({
				region.text,
				_currentWidth,
				width,
				-1,
			});
			_currentWidth += width;
		} else {
			_oldParts.push_back({ region.text, _oldWidth, width, -1 });
			_oldWidth += width;
		}
	}

	_animation.start(
		[=] { animationCallback(); },
		0.,
		1.,
		_options.duration,
		_options.transition);
}

void AnimatedString::animationCallback() {
	if (_update) {
		_update();
	}
	if (!_animation.animating()) {
		_oldParts.clear();
		_oldText = QString();
		_oldWidth = 0.;
		if (_scheduled) {
			const auto next = *_scheduled;
			_scheduled.reset();
			if (next != _currentText) {
				realSetText(next);
			}
		}
	}
}

void AnimatedString::drawPart(
		QPainter &p,
		float64 x,
		float64 baseline,
		const QString &text,
		float64 opacity) const {
	if (opacity <= 0. || text.isEmpty()) {
		return;
	}
	p.setOpacity(std::clamp(opacity, 0., 1.));
	p.drawText(QPointF(x, baseline), text);
}

void AnimatedString::draw(
		QPainter &p,
		int x,
		int y,
		const QColor &color,
		float64 opacity) const {
	if (_currentParts.empty() && _oldParts.empty()) {
		return;
	}
	const auto t = _animation.value(1.);
	const auto crossfade = _animation.animating() && !_oldParts.empty();
	const auto initial = p.opacity();
	p.setFont(_font);
	p.setPen(color);
	const auto baseline = y + _font->ascent;
	if (!crossfade) {
		for (const auto &part : _currentParts) {
			drawPart(
				p,
				x + part.offset,
				baseline,
				part.text,
				initial * opacity);
		}
		p.setOpacity(initial);
		return;
	}

	const auto amplitude = _font->height * _options.moveAmplitude;
	const auto direction = _options.moveDown ? 1. : -1.;
	for (const auto &part : _currentParts) {
		const auto opposite = part.opposite;
		if (opposite >= 0) {
			const auto &old = _oldParts[opposite];
			const auto px = old.offset + (part.offset - old.offset) * t;
			drawPart(p, x + px, baseline, part.text, initial * opacity);
		} else {
			const auto py = -amplitude * (1. - t) * direction;
			drawPart(
				p,
				x + part.offset,
				baseline + py,
				part.text,
				initial * opacity * t);
		}
	}
	for (const auto &part : _oldParts) {
		if (part.opposite >= 0) {
			continue;
		}
		const auto py = amplitude * t * direction;
		drawPart(
			p,
			x + part.offset,
			baseline + py,
			part.text,
			initial * opacity * (1. - t));
	}
	p.setOpacity(initial);
}

float64 AnimatedString::currentWidth() const {
	if (!_oldParts.empty() && _animation.animating()) {
		const auto t = _animation.value(1.);
		return _oldWidth + (_currentWidth - _oldWidth) * t;
	}
	return _currentWidth;
}

float64 AnimatedString::width() const {
	return _currentWidth;
}

int AnimatedString::height() const {
	return _font->height;
}

bool AnimatedString::animating() const {
	return _animation.animating();
}

void AnimatedString::finishAnimating() {
	if (_scheduled) {
		const auto next = *_scheduled;
		_scheduled.reset();
		setInstant(next);
	}
	_animation.stop();
	_oldParts.clear();
	_oldText = QString();
	_oldWidth = 0.;
}

} // namespace Ui
