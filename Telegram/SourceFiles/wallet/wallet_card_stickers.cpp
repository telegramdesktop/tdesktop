/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_card_stickers.h"

#include "apiwrap.h"
#include "data/data_changes.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_star_gift.h"
#include "data/data_user.h"
#include "data/data_user_wallet_card.h"
#include "history/view/media/history_view_sticker_player.h"
#include "lottie/lottie_common.h"
#include "lottie/lottie_single_player.h"
#include "main/main_session.h"
#include "ui/painter.h"

#include "styles/style_wallet.h"

namespace Wallet {
namespace {

constexpr auto kOutlineRatio = 0.03;
// WHY: the outline is computed on a grid of at most this many cells per
// outline radius, so the work per frame pixel stays bounded at any size.
constexpr auto kOutlineSamples = 8;
constexpr auto kNoSite = std::numeric_limits<float>::max();
constexpr auto kSolidSpan = -2;
constexpr auto kBlendSpan = -3;

struct OutlineGrid {
	int step = 0;
	int columns = 0;
	int rows = 0;
	std::vector<float> values;
	std::vector<float> horizontal;
	std::vector<float> vertical;
	std::vector<float> tones;
	std::vector<uchar> alphas;
};

struct Envelope {
	std::vector<float> positions;
	std::vector<float> heights;
	std::vector<float> carries;
	std::vector<float> shades;
	std::vector<float> bounds;
};

struct OutlineSpan {
	int from = 0;
	int till = 0;
	int column = 0;
	QRgb value = 0;
};

[[nodiscard]] int StickerSide(const Data::StoryArea &area, QSize card) {
	const auto limit = float64(std::min(card.width(), card.height()));
	const auto side = std::min(
		area.geometry.width() * card.width(),
		area.geometry.height() * card.height());
	return (side > 0.)
		? int(base::SafeRound(std::min(side, limit)))
		: 0;
}

[[nodiscard]] QSize FrameSize(not_null<DocumentData*> document, int side) {
	const auto box = QSize(side, side);
	const auto &dimensions = document->dimensions;
	return dimensions.isEmpty()
		? box
		: dimensions.scaled(box, Qt::KeepAspectRatio).expandedTo({ 1, 1 });
}

void LowerEnvelope(
		float *values,
		float *along,
		float *across,
		float *tones,
		int count,
		int stride,
		float cutoff,
		Envelope &buffer) {
	const auto positions = buffer.positions.data();
	const auto heights = buffer.heights.data();
	const auto carries = buffer.carries.data();
	const auto shades = buffer.shades.data();
	const auto bounds = buffer.bounds.data();
	auto top = -1;
	for (auto q = 0; q != count; ++q) {
		const auto height = values[q * stride];
		if (height >= cutoff) {
			continue;
		}
		const auto position = q + along[q * stride];
		auto bound = -kNoSite;
		while (top >= 0) {
			const auto site = positions[top];
			bound = (height - heights[top] + position * position - site * site)
				/ (2.f * (position - site));
			if (bound > bounds[top]) {
				break;
			}
			--top;
		}
		++top;
		positions[top] = position;
		heights[top] = height;
		carries[top] = across[q * stride];
		shades[top] = tones[q * stride];
		bounds[top] = bound;
	}
	if (top < 0) {
		return;
	}
	auto index = 0;
	for (auto q = 0; q != count; ++q) {
		while (index < top && bounds[index + 1] <= q) {
			++index;
		}
		const auto delta = q - positions[index];
		values[q * stride] = delta * delta + heights[index];
		across[q * stride] = carries[index];
		tones[q * stride] = shades[index];
	}
}

void SpreadRows(uchar *to, const uchar *from, int columns, int count) {
	for (auto row = 0; row != count; row += columns) {
		const auto line = from + row;
		const auto result = to + row;
		result[0] = std::max(line[0], line[1]);
		for (auto x = 1; x != columns - 1; ++x) {
			result[x] = std::max(line[x], std::max(line[x - 1], line[x + 1]));
		}
		result[columns - 1] = std::max(line[columns - 2], line[columns - 1]);
	}
}

void MaxShifted(uchar *to, const uchar *from, int count, int shift) {
	const auto till = count + std::min(shift, 0);
	for (auto i = std::max(shift, 0); i < till; ++i) {
		to[i] = std::max(to[i], from[i - shift]);
	}
}

[[nodiscard]] OutlineGrid OutlineSites(const QImage &frame, int outline) {
	const auto step = (outline + kOutlineSamples - 1) / kOutlineSamples;
	const auto width = frame.width();
	const auto columns = (width + 2 * outline + step - 1) / step;
	const auto rows = (frame.height() + 2 * outline + step - 1) / step;
	const auto cells = columns * rows;
	const auto middle = (step - 1) / 2.f;
	auto result = OutlineGrid{
		.step = step,
		.columns = columns,
		.rows = rows,
		.values = std::vector<float>(cells, kNoSite),
		.horizontal = std::vector<float>(cells),
		.vertical = std::vector<float>(cells),
		.tones = std::vector<float>(cells),
		.alphas = std::vector<uchar>(cells),
	};
	auto sums = std::vector<float>(cells);
	auto buffer = std::vector<uchar>(2 * cells);
	auto lineSums = std::vector<float>(width);
	auto lineShifts = std::vector<float>(width);
	auto linePeaks = std::vector<uchar>(width);
	for (auto row = 0; row != rows; ++row) {
		const auto top = std::max(row * step - outline, 0);
		const auto bottom = std::min(
			(row + 1) * step - outline,
			frame.height());
		if (top >= bottom) {
			continue;
		}
		ranges::fill(lineSums, 0.f);
		ranges::fill(lineShifts, 0.f);
		ranges::fill(linePeaks, uchar());
		for (auto y = top; y != bottom; ++y) {
			const auto pixels = reinterpret_cast<const QRgb*>(
				frame.constScanLine(y));
			const auto shift = y + outline - row * step - middle;
			for (auto x = 0; x != width; ++x) {
				const auto alpha = uchar(qAlpha(pixels[x]));
				lineSums[x] += alpha;
				lineShifts[x] += alpha * shift;
				linePeaks[x] = std::max(linePeaks[x], alpha);
			}
		}
		auto x = 0;
		for (auto column = outline / step; x != width; ++column) {
			const auto till = std::min((column + 1) * step - outline, width);
			auto sum = 0.f;
			auto horizontal = 0.f;
			auto vertical = 0.f;
			auto peak = uchar();
			for (; x != till; ++x) {
				const auto shift = x + outline - column * step - middle;
				sum += lineSums[x];
				horizontal += lineSums[x] * shift;
				vertical += lineShifts[x];
				peak = std::max(peak, linePeaks[x]);
			}
			const auto index = row * columns + column;
			sums[index] = sum;
			result.horizontal[index] = horizontal;
			result.vertical[index] = vertical;
			result.alphas[index] = peak;
		}
	}
	const auto tones = buffer.data();
	const auto spread = tones + cells;
	SpreadRows(spread, result.alphas.data(), columns, cells);
	std::copy(spread, spread + cells, tones);
	MaxShifted(tones, spread, cells, columns);
	MaxShifted(tones, spread, cells, -columns);
	for (auto index = 0; index != cells; ++index) {
		const auto sum = sums[index];
		if (!sum) {
			continue;
		}
		// antialiased rims count as partial cover of their brighter neighbour
		const auto coverage = sum / (float(tones[index]) * step * step);
		const auto reach = (outline
			+ coverage * step / 2.f
			- (1.f - coverage) / (2.f * step)) / step;
		result.values[index] = -reach * reach;
		result.horizontal[index] /= sum * step;
		result.vertical[index] /= sum * step;
		result.tones[index] = tones[index];
	}
	return result;
}

void OutlineDistances(OutlineGrid &grid, int outline) {
	const auto step = grid.step;
	const auto columns = grid.columns;
	const auto rows = grid.rows;
	const auto values = grid.values.data();
	const auto horizontal = grid.horizontal.data();
	const auto vertical = grid.vertical.data();
	const auto tones = grid.tones.data();
	const auto relative = outline / float(step);
	const auto limit = float(outline + 2 * step);
	const auto cutoff = (limit * limit) / (step * step) - relative * relative;
	const auto longest = std::max(columns, rows);
	auto buffer = Envelope{
		.positions = std::vector<float>(longest),
		.heights = std::vector<float>(longest),
		.carries = std::vector<float>(longest),
		.shades = std::vector<float>(longest),
		.bounds = std::vector<float>(longest),
	};
	for (auto column = 0; column != columns; ++column) {
		LowerEnvelope(
			values + column,
			vertical + column,
			horizontal + column,
			tones + column,
			rows,
			columns,
			cutoff,
			buffer);
	}
	for (auto row = 0; row != rows; ++row) {
		const auto shift = row * columns;
		LowerEnvelope(
			values + shift,
			horizontal + shift,
			vertical + shift,
			tones + shift,
			columns,
			1,
			cutoff,
			buffer);
	}
	for (auto &value : grid.values) {
		value = (value == kNoSite)
			? limit
			: std::min(
				limit,
				step * std::sqrt(std::max(value + relative * relative, 0.f)));
	}
}

void OutlineAlphas(OutlineGrid &grid, int outline) {
	const auto columns = grid.columns;
	const auto count = int(grid.alphas.size());
	const auto radius = outline / float(grid.step);
	const auto reaches = int(radius);
	const auto result = grid.alphas.data();
	auto buffer = std::vector<uchar>(2 * count);
	auto rows = buffer.data();
	auto spread = rows + count;
	std::copy(result, result + count, rows);
	for (auto reach = 0; reach <= reaches; ++reach) {
		if (reach) {
			SpreadRows(spread, rows, columns, count);
			std::swap(rows, spread);
		}
		for (auto dy = -reaches; dy <= reaches; ++dy) {
			const auto chord = std::max(radius * radius - dy * dy, 0.f);
			if (int(std::sqrt(chord)) == reach) {
				MaxShifted(result, rows, count, dy * columns);
			}
		}
	}
	// keep the nearest shape's alpha where the cell disc stops short of it
	for (auto i = 0; i != count; ++i) {
		result[i] = std::max(result[i], uchar(grid.tones[i]));
	}
}

void OutlineSpans(
		std::vector<OutlineSpan> &spans,
		float *above,
		float *below,
		const OutlineGrid &grid,
		const int *starts,
		int upper,
		int lower,
		int outline) {
	const auto step = grid.step;
	const auto columns = grid.columns;
	const auto middle = (step - 1) / 2.f;
	const auto outer = outline + 0.5f;
	const auto &values = grid.values;
	const auto &alphas = grid.alphas;
	const auto run = [&](int from, int till, int kind, QRgb value) {
		if (!spans.empty()
			&& spans.back().column == kind
			&& spans.back().value == value) {
			spans.back().till = till;
		} else {
			spans.push_back({ from, till, kind, value });
		}
	};
	spans.clear();
	for (auto column = -1; column < columns; ++column) {
		const auto from = starts[column + 1];
		const auto till = starts[column + 2];
		if (from == till) {
			continue;
		}
		const auto first = std::max(column, 0);
		const auto second = std::min(column + 1, columns - 1);
		const auto [nearest, farthest] = std::minmax({
			values[upper + first],
			values[upper + second],
			values[lower + first],
			values[lower + second],
		});
		const auto alpha = alphas[upper + first];
		if (nearest >= outer) {
			run(from, till, kSolidSpan, QRgb());
			continue;
		} else if (farthest > outer - 1.f) {
			spans.push_back({ from, till, column });
			continue;
		} else if (alphas[upper + second] == alpha
			&& alphas[lower + first] == alpha
			&& alphas[lower + second] == alpha) {
			run(from, till, kSolidSpan, QRgb(alpha * 0x01010101U));
			continue;
		}
		const auto start = middle + column * step;
		const auto top = float(alpha);
		const auto bottom = float(alphas[lower + first]);
		const auto rise = (alphas[upper + second] - top) / step;
		const auto fall = (alphas[lower + second] - bottom) / step;
		for (auto x = from; x != till; ++x) {
			above[x] = top + (x - start) * rise;
			below[x] = bottom + (x - start) * fall;
		}
		run(from, till, kBlendSpan, QRgb());
	}
}

void FillStrokeRow(
		QRgb *to,
		const std::vector<OutlineSpan> &spans,
		const float *above,
		const float *below,
		const OutlineGrid &grid,
		int upper,
		int lower,
		float weight,
		int outline) {
	const auto step = grid.step;
	const auto columns = grid.columns;
	const auto middle = (step - 1) / 2.f;
	const auto outer = outline + 0.5f;
	const auto at = [&](const auto &field, int column) {
		const auto top = float(field[upper + column]);
		return top + (float(field[lower + column]) - top) * weight;
	};
	for (const auto [from, till, column, value] : spans) {
		if (column == kSolidSpan) {
			std::fill(to + from, to + till, value);
			continue;
		} else if (column == kBlendSpan) {
			for (auto x = from; x != till; ++x) {
				const auto alpha = above[x] + (below[x] - above[x]) * weight;
				to[x] = uint(alpha + 0.5f) * 0x01010101U;
			}
			continue;
		}
		const auto first = std::max(column, 0);
		const auto second = std::min(column + 1, columns - 1);
		const auto left = at(grid.values, first);
		const auto slope = (at(grid.values, second) - left) / step;
		const auto alpha = at(grid.alphas, first);
		const auto change = (at(grid.alphas, second) - alpha) / step;
		const auto start = middle + column * step;
		for (auto x = from; x != till; ++x) {
			const auto distance = left + (x - start) * slope;
			const auto coverage = std::clamp(outer - distance, 0.f, 1.f);
			const auto mask = (alpha + (x - start) * change) * coverage;
			to[x] = uint(mask + 0.5f) * 0x01010101U;
		}
	}
}

void DrawFrameRow(QRgb *to, const QRgb *from, int width) {
	for (auto x = 0; x != width; ++x) {
		const auto pixel = from[x];
		const auto under = qAlpha(to[x]) * (255 - qAlpha(pixel));
		to[x] = pixel + ((under + 127) / 255) * 0x01010101U;
	}
}

[[nodiscard]] QImage OutlineStroke(
		const OutlineGrid &grid,
		const QImage &frame,
		int outline) {
	const auto step = grid.step;
	const auto columns = grid.columns;
	const auto rows = grid.rows;
	const auto middle = (step - 1) / 2.f;
	const auto size = frame.size() + QSize(outline, outline) * 2;
	auto starts = std::vector<int>(columns + 2);
	for (auto column = 0; column != columns; ++column) {
		starts[column + 1] = std::min(
			int(std::ceil(middle + column * step)),
			size.width());
	}
	starts[columns + 1] = size.width();
	auto spans = std::vector<OutlineSpan>();
	auto blend = std::vector<float>(2 * size.width());
	const auto above = blend.data();
	const auto below = above + size.width();
	auto band = -1;
	auto result = QImage(size, QImage::Format_ARGB32_Premultiplied);
	for (auto y = 0; y != size.height(); ++y) {
		const auto position = std::clamp(
			(y - middle) / step,
			0.f,
			float(rows - 1));
		const auto top = int(position);
		const auto upper = top * columns;
		const auto lower = std::min(top + 1, rows - 1) * columns;
		if (band != top) {
			band = top;
			OutlineSpans(
				spans,
				above,
				below,
				grid,
				starts.data(),
				upper,
				lower,
				outline);
		}
		const auto to = reinterpret_cast<QRgb*>(result.scanLine(y));
		FillStrokeRow(
			to,
			spans,
			above,
			below,
			grid,
			upper,
			lower,
			position - top,
			outline);
		if (y >= outline && y < outline + frame.height()) {
			DrawFrameRow(
				to + outline,
				reinterpret_cast<const QRgb*>(
					frame.constScanLine(y - outline)),
				frame.width());
		}
	}
	return result;
}

[[nodiscard]] QImage Outlined(QImage frame, int ratio) {
	frame = std::move(frame).convertToFormat(
		QImage::Format_ARGB32_Premultiplied);
	const auto side = std::max(frame.width(), frame.height());
	const auto outline = std::max(
		1,
		int(base::SafeRound(side * kOutlineRatio)));
	auto grid = OutlineSites(frame, outline);
	OutlineDistances(grid, outline);
	OutlineAlphas(grid, outline);
	auto result = OutlineStroke(grid, frame, outline);
	result.setDevicePixelRatio(ratio);
	return result;
}

void PaintSticker(
		QPainter &p,
		const Data::StoryArea &area,
		const QImage &image,
		QSize card) {
	const auto center = area.geometry.center();
	const auto rotation = std::isfinite(area.rotation)
		? std::remainder(area.rotation, 360.)
		: 0.;
	const auto size = QSizeF(image.size()) / image.devicePixelRatio();
	p.save();
	p.translate(center.x() * card.width(), center.y() * card.height());
	p.rotate(rotation);
	p.drawImage(
		QRectF(QPointF(-size.width() / 2., -size.height() / 2.), size),
		image);
	p.restore();
}

} // namespace

struct CardStickers::Entry {
	not_null<DocumentData*> document;
	Data::WalletCardSticker data;
	std::shared_ptr<Data::DocumentMedia> media;
	std::unique_ptr<HistoryView::StickerPlayer> player;
	QImage image;
	int side = 0;
	int ratio = 0;
	rpl::lifetime loading;
};

void RefreshOwnCard(not_null<Main::Session*> session) {
	session->api().requestFullPeer(session->user());
}

auto OwnCardStickersValue(not_null<Main::Session*> session)
-> rpl::producer<std::vector<Data::WalletCardSticker>> {
	const auto self = session->user();
	return session->changes().peerFlagsValue(
		self,
		Data::PeerUpdate::Flag::WalletCard
	) | rpl::map([=] {
		const auto card = self->walletCard();
		return card
			? card->stickers
			: std::vector<Data::WalletCardSticker>();
	}) | rpl::distinct_until_changed();
}

CardStickers::CardStickers(
	not_null<Main::Session*> session,
	rpl::producer<std::vector<Data::WalletCardSticker>> stickers,
	Fn<void()> repaint)
: _session(session)
, _repaint(std::move(repaint)) {
	std::move(
		stickers
	) | rpl::on_next([=](std::vector<Data::WalletCardSticker> &&list) {
		apply(std::move(list));
	}, _lifetime);
}

CardStickers::~CardStickers() = default;

void CardStickers::paint(QPainter &p, QRect rest) {
	const auto ratio = style::DevicePixelRatio();
	if (_layerSize != rest.size() || _layerRatio != ratio) {
		_layerSize = rest.size();
		_layerRatio = ratio;
		_layerValid = false;
	}
	for (const auto &entry : _entries) {
		if (validate(entry.get())) {
			_layerValid = false;
		}
	}
	if (!_layerValid) {
		compose();
	}
	if (!_layer.isNull()) {
		p.drawImage(QRectF(rest), _layer);
	}
}

void CardStickers::apply(std::vector<Data::WalletCardSticker> &&stickers) {
	auto was = base::take(_entries);
	for (auto &sticker : stickers) {
		if (!sticker.gift) {
			continue;
		}
		const auto document = sticker.gift->document;
		const auto i = ranges::find_if(was, [&](const auto &entry) {
			return entry && (entry->document == document);
		});
		auto entry = (i != end(was))
			? std::move(*i)
			: std::make_unique<Entry>(Entry{ .document = document });
		entry->data = std::move(sticker);
		_entries.push_back(std::move(entry));
	}
	_layerValid = false;
	_repaint();
}

bool CardStickers::validate(not_null<Entry*> entry) {
	auto changed = false;
	const auto side = StickerSide(entry->data.area, _layerSize);
	if (entry->side != side || entry->ratio != _layerRatio) {
		changed = !entry->image.isNull();
		prepare(entry, side);
	}
	if (entry->image.isNull() && entry->player && entry->player->ready()) {
		capture(entry);
		changed = changed || !entry->image.isNull();
	}
	return changed;
}

void CardStickers::prepare(not_null<Entry*> entry, int side) {
	entry->loading.destroy();
	entry->player = nullptr;
	entry->image = QImage();
	entry->side = side;
	entry->ratio = _layerRatio;
	if (!side) {
		return;
	} else if (!entry->media) {
		entry->media = entry->document->createMediaView();
	}
	entry->media->checkStickerLarge();
	if (entry->media->loaded()) {
		createPlayer(entry);
		return;
	}
	_session->downloaderTaskFinished(
	) | rpl::filter([=] {
		return entry->media->loaded();
	}) | rpl::take(1) | rpl::on_next([=] {
		createPlayer(entry);
	}, entry->loading);
}

void CardStickers::createPlayer(not_null<Entry*> entry) {
	const auto document = entry->document;
	const auto bytes = entry->media->bytes();
	const auto sticker = document->sticker();
	const auto size = FrameSize(document, entry->side);
	if (sticker && sticker->isLottie()) {
		entry->player = std::make_unique<HistoryView::LottiePlayer>(
			std::make_unique<Lottie::SinglePlayer>(
				Lottie::ReadContent(bytes, document->filepath()),
				Lottie::FrameRequest{ size * entry->ratio },
				Lottie::Quality::High));
	} else if (sticker && sticker->isWebm()) {
		entry->player = std::make_unique<HistoryView::WebmPlayer>(
			document->location(),
			bytes,
			size);
	} else {
		entry->player = std::make_unique<HistoryView::StaticStickerPlayer>(
			document->location(),
			bytes,
			size);
	}
	entry->player->setRepaintCallback(_repaint);
}

void CardStickers::capture(not_null<Entry*> entry) {
	auto frame = entry->player->frame(
		FrameSize(entry->document, entry->side),
		QColor(0, 0, 0, 0),
		false,
		crl::now(),
		true).image;
	entry->player = nullptr;
	if (!frame.isNull()) {
		entry->image = Outlined(std::move(frame), entry->ratio);
	}
}

void CardStickers::compose() {
	_layerValid = true;
	_layer = QImage();
	const auto ready = ranges::any_of(_entries, [](const auto &entry) {
		return !entry->image.isNull();
	});
	if (!ready || _layerSize.isEmpty()) {
		return;
	}
	_layer = QImage(
		_layerSize * _layerRatio,
		QImage::Format_ARGB32_Premultiplied);
	_layer.setDevicePixelRatio(_layerRatio);
	_layer.fill(Qt::transparent);
	auto p = QPainter(&_layer);
	auto hq = PainterHighQualityEnabler(p);
	auto clip = QPainterPath();
	clip.addRoundedRect(
		QRectF(QPointF(), QSizeF(_layerSize)),
		st::walletCardRadius,
		st::walletCardRadius);
	p.setClipPath(clip);
	for (const auto &entry : _entries) {
		if (!entry->image.isNull()) {
			PaintSticker(p, entry->data.area, entry->image, _layerSize);
		}
	}
}

} // namespace Wallet
