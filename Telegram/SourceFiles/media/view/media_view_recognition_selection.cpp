/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/view/media_view_recognition_selection.h"

#include "ui/rect.h"
#include "ui/style/style_core.h"

namespace Media::View {

void RecognitionSelection::setSources(
		not_null<const Result*> result,
		not_null<const QImage*> image) {
	_result = result;
	_image = image;
}

RecognitionPosition RecognitionSelection::positionAt(
		QPoint position,
		QRect contentRect,
		int rotation,
		bool allowOutside) const {
	if (!_result->success || _result->items.empty()) {
		return {};
	}
	if (rotation) {
		auto transform = QTransform();
		const auto center = rect::center(contentRect);
		transform.translate(center.x(), center.y());
		transform.rotate(-rotation);
		transform.translate(-center.x(), -center.y());
		contentRect = transform.mapRect(contentRect);
		position = transform.map(position);
	}
	const auto imageSize = _image->size() / style::DevicePixelRatio();
	if (imageSize.isEmpty()) {
		return {};
	}
	const auto scale = contentRect.width() / float64(imageSize.width());
	const auto scaled = [&](QRect rect) {
		return QRect(
			contentRect.x() + int(rect.x() * scale),
			contentRect.y() + int(rect.y() * scale),
			int(rect.width() * scale),
			int(rect.height() * scale));
	};
	const auto &items = _result->items;
	auto found = -1;
	auto bestDy = std::numeric_limits<int>::max();
	auto bestDx = std::numeric_limits<int>::max();
	for (auto i = 0, count = int(items.size()); i != count; ++i) {
		const auto box = scaled(items[i].rect);
		if (allowOutside) {
			const auto dy = (position.y() < box.top())
				? (box.top() - position.y())
				: (position.y() > box.bottom())
				? (position.y() - box.bottom())
				: 0;
			const auto dx = (position.x() < box.left())
				? (box.left() - position.x())
				: (position.x() > box.right())
				? (position.x() - box.right())
				: 0;
			if (dy < bestDy || (dy == bestDy && dx < bestDx)) {
				bestDy = dy;
				bestDx = dx;
				found = i;
			}
		} else if (box.contains(position)) {
			found = i;
			break;
		}
	}
	if (found < 0) {
		return {};
	}
	const auto length = int(items[found].text.size());
	const auto &bounds = charBounds(found);
	auto character = length;
	for (auto i = 0; i != length; ++i) {
		const auto center = contentRect.x()
			+ int((bounds[i] + bounds[i + 1]) / 2. * scale);
		if (position.x() < center) {
			character = i;
			break;
		}
	}
	return { found, character };
}

const std::vector<int> &RecognitionSelection::charBounds(int index) const {
	static const auto kEmpty = std::vector<int>();
	const auto &items = _result->items;
	const auto count = int(items.size());
	if (index < 0 || index >= count) {
		return kEmpty;
	}
	const auto key = _image->cacheKey();
	if (_boundsKey != key || int(_boundsCache.size()) != count) {
		_boundsKey = key;
		_boundsCache.assign(count, {});
	}
	auto &cached = _boundsCache[index];
	if (!cached.empty()) {
		return cached;
	}
	const auto &item = items[index];
	const auto length = int(item.text.size());
	const auto line = item.rect;
	auto bounds = std::vector<int>();
	auto usable = (length > 0) && (int(item.glyphs.size()) == length);
	if (usable) {
		for (const auto &glyph : item.glyphs) {
			if (glyph.isNull()
				|| (length > 1 && glyph.width() * 5 > line.width() * 3)) {
				usable = false;
				break;
			}
		}
	}
	if (length <= 0) {
		bounds = { line.x() };
	} else if (usable) {
		bounds.resize(length + 1);
		bounds[0] = item.glyphs.front().left();
		for (auto i = 1; i != length; ++i) {
			bounds[i] = (item.glyphs[i - 1].right()
				+ item.glyphs[i].left()) / 2;
		}
		bounds[length] = item.glyphs.back().right();
	} else if (auto ink = inkBounds(line, length); !ink.empty()) {
		bounds = std::move(ink);
	} else {
		bounds.resize(length + 1);
		for (auto i = 0; i != length + 1; ++i) {
			bounds[i] = line.x() + (line.width() * i) / length;
		}
	}
	cached = std::move(bounds);
	return cached;
}

std::vector<int> RecognitionSelection::inkBounds(
		QRect line,
		int length) const {
	const auto &image = *_image;
	if (image.isNull() || image.depth() != 32 || length <= 0) {
		return {};
	}
	const auto ratio = style::DevicePixelRatio();
	const auto region = QRect(
		line.x() * ratio,
		line.y() * ratio,
		line.width() * ratio,
		line.height() * ratio
	).intersected(QRect(QPoint(), image.size()));
	const auto width = region.width();
	const auto height = region.height();
	if (width < length * 2 || height < 2) {
		return {};
	}
	const auto x0 = region.x();
	const auto y0 = region.y();

	auto histogram = std::array<int, 256>{};
	for (auto y = 0; y != height; ++y) {
		const auto row = reinterpret_cast<const QRgb*>(
			image.constScanLine(y0 + y));
		for (auto x = 0; x != width; ++x) {
			++histogram[qGray(row[x0 + x])];
		}
	}
	auto background = 0;
	for (auto level = 0, best = -1; level != 256; ++level) {
		if (histogram[level] > best) {
			best = histogram[level];
			background = level;
		}
	}
	constexpr auto kInkThreshold = 56;
	const auto columnThreshold = std::max(1, height / 6);

	auto ink = std::vector<int>(width, 0);
	for (auto y = 0; y != height; ++y) {
		const auto row = reinterpret_cast<const QRgb*>(
			image.constScanLine(y0 + y));
		for (auto x = 0; x != width; ++x) {
			if (std::abs(qGray(row[x0 + x]) - background) > kInkThreshold) {
				++ink[x];
			}
		}
	}

	auto segments = std::vector<std::pair<int, int>>();
	auto inSegment = false;
	auto start = 0;
	for (auto x = 0; x != width; ++x) {
		const auto inky = (ink[x] >= columnThreshold);
		if (inky && !inSegment) {
			inSegment = true;
			start = x;
		} else if (!inky && inSegment) {
			inSegment = false;
			segments.push_back({ start, x });
		}
	}
	if (inSegment) {
		segments.push_back({ start, width });
	}
	if (int(segments.size()) != length) {
		return {};
	}

	auto bounds = std::vector<int>(length + 1);
	bounds[0] = line.x() + segments.front().first / ratio;
	for (auto i = 1; i != length; ++i) {
		const auto gap = (segments[i - 1].second + segments[i].first) / 2;
		bounds[i] = line.x() + gap / ratio;
	}
	bounds[length] = line.x() + segments.back().second / ratio;
	return bounds;
}

std::vector<RecognitionSpan> RecognitionSelection::spans() const {
	const auto &items = _result->items;
	const auto count = int(items.size());
	if (_anchor.item < 0
		|| _focus.item < 0
		|| _anchor.item >= count
		|| _focus.item >= count) {
		return {};
	}

	auto order = std::vector<int>(count);
	for (auto i = 0; i != count; ++i) {
		order[i] = i;
	}
	const auto centerY = [&](int i) {
		return items[i].rect.y() + items[i].rect.height() / 2;
	};
	std::sort(order.begin(), order.end(), [&](int a, int b) {
		return centerY(a) < centerY(b);
	});
	auto row = std::vector<int>(count, 0);
	auto rows = 0;
	for (auto k = 1; k != count; ++k) {
		const auto prev = order[k - 1];
		const auto cur = order[k];
		const auto threshold = std::max(
			items[prev].rect.height(),
			items[cur].rect.height()) / 2;
		if (centerY(cur) - centerY(prev) > threshold) {
			++rows;
		}
		row[cur] = rows;
	}
	std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
		return (row[a] != row[b])
			? (row[a] < row[b])
			: (items[a].rect.x() < items[b].rect.x());
	});
	auto rank = std::vector<int>(count);
	for (auto k = 0; k != count; ++k) {
		rank[order[k]] = k;
	}

	auto from = _anchor;
	auto till = _focus;
	auto fromRank = rank[from.item];
	auto tillRank = rank[till.item];
	if (tillRank < fromRank
		|| (tillRank == fromRank && till.character < from.character)) {
		std::swap(from, till);
		std::swap(fromRank, tillRank);
	}
	if (fromRank == tillRank && from.character == till.character) {
		return {};
	}

	auto result = std::vector<RecognitionSpan>();
	auto lastRow = -1;
	for (auto r = fromRank; r <= tillRank; ++r) {
		const auto item = order[r];
		const auto length = int(items[item].text.size());
		const auto c0 = (item == from.item)
			? std::clamp(from.character, 0, length)
			: 0;
		const auto c1 = (item == till.item)
			? std::clamp(till.character, 0, length)
			: length;
		if (c1 <= c0) {
			continue;
		}
		result.push_back({ item, c0, c1, (row[item] != lastRow) });
		lastRow = row[item];
	}
	return result;
}

QRect RecognitionSelection::bandFor(int item, int from, int till) const {
	const auto &items = _result->items;
	if (item < 0 || item >= int(items.size())) {
		return {};
	}
	const auto &entry = items[item];
	const auto length = int(entry.text.size());
	const auto c0 = std::clamp(from, 0, length);
	const auto c1 = std::clamp(till, 0, length);
	if (c1 <= c0) {
		return {};
	}
	const auto &bounds = charBounds(item);
	const auto left = bounds[c0];
	const auto right = bounds[c1];
	return (right > left)
		? QRect(left, entry.rect.y(), right - left, entry.rect.height())
		: entry.rect;
}

QString RecognitionSelection::selectedText() const {
	const auto list = spans();
	const auto &items = _result->items;
	auto result = QString();
	for (const auto &span : list) {
		if (!result.isEmpty()) {
			result += span.rowStart ? '\n' : ' ';
		}
		const auto &text = items[span.item].text;
		result += text.mid(span.from, span.till - span.from);
	}
	return result;
}

void RecognitionSelection::start(RecognitionPosition position) {
	_anchor = position;
	_focus = position;
	_selecting = true;
	_dragged = false;
}

bool RecognitionSelection::updateFocus(RecognitionPosition position) {
	if (position.item >= 0 && _focus != position) {
		_focus = position;
		return true;
	}
	return false;
}

bool RecognitionSelection::clear() {
	_selecting = false;
	_dragged = false;
	if (_anchor.item >= 0 || _focus.item >= 0) {
		_anchor = RecognitionPosition();
		_focus = RecognitionPosition();
		return true;
	}
	return false;
}

bool RecognitionSelection::hasSelection() const {
	return !spans().empty();
}

} // namespace Media::View
