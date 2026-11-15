/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_card_stickers.h"

#include "apiwrap.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_star_gift.h"
#include "data/data_user.h"
#include "data/data_user_wallet_card.h"
#include "history/view/media/history_view_sticker_player.h"
#include "lottie/lottie_common.h"
#include "lottie/lottie_single_player.h"
#include "main/main_session.h"
#include "ui/effects/star_burst.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "wallet/wallet_address.h"
#include "wallet/wallet_card_space.h"
#include "wallet/wallet_own_card.h"
#include "wallet/wallet_session.h"

#include "styles/style_wallet.h"

namespace Wallet {
namespace {

// WHY: the outline is computed on a grid of at most this many cells per
// outline radius, so the work per frame pixel stays bounded at any size.
constexpr auto kOutlineSamples = 8;
constexpr auto kNoSite = std::numeric_limits<float>::max();
constexpr auto kSolidSpan = -2;
constexpr auto kBlendSpan = -3;
constexpr auto kSettleDuration = crl::time(70);
constexpr auto kRingDelay = crl::time(50);
constexpr auto kRingDuration = crl::time(300);
constexpr auto kLandingDuration = kRingDelay + kRingDuration;
constexpr auto kSparkleDelay = crl::time(150);
constexpr auto kSparkleSpread = crl::time(50);
constexpr auto kSparkleLifeMin = crl::time(100);
constexpr auto kSparkleLifeMax = crl::time(150);
constexpr auto kRingStartScale = 1.2;
constexpr auto kRingGrowth = 0.8;
constexpr auto kRingOpacity = 0.6;
constexpr auto kSinkDuration = crl::time(300);

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

[[nodiscard]] QSize FrameSize(not_null<DocumentData*> document, int side) {
	const auto box = QSize(side, side);
	const auto &dimensions = document->dimensions;
	return dimensions.isEmpty()
		? box
		: dimensions.scaled(box, Qt::KeepAspectRatio).expandedTo({ 1, 1 });
}

[[nodiscard]] std::unique_ptr<HistoryView::StickerPlayer> MakePlayer(
		not_null<DocumentData*> document,
		const QByteArray &bytes,
		int side,
		int ratio) {
	const auto sticker = document->sticker();
	const auto size = FrameSize(document, side);
	if (sticker && sticker->isLottie()) {
		return std::make_unique<HistoryView::LottiePlayer>(
			std::make_unique<Lottie::SinglePlayer>(
				Lottie::ReadContent(bytes, document->filepath()),
				Lottie::FrameRequest{ size * ratio },
				Lottie::Quality::High));
	} else if (sticker && sticker->isWebm()) {
		return std::make_unique<HistoryView::WebmPlayer>(
			document->location(),
			bytes,
			size);
	}
	return std::make_unique<HistoryView::StaticStickerPlayer>(
		document->location(),
		bytes,
		size);
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

[[nodiscard]] QSizeF LogicalSize(const QImage &image) {
	return QSizeF(image.size()) / image.devicePixelRatio();
}

[[nodiscard]] float64 EaseOut(float64 progress) {
	const auto left = 1. - std::clamp(progress, 0., 1.);
	return 1. - left * left;
}

[[nodiscard]] Ui::StarBurstDescriptor LandingBurstDescriptor() {
	const auto side = [](float64 sign) {
		return Ui::StarBurstSide{
			.sign = sign,
			.count = 5,
			.angle = { -90., 90. },
			.reach = { 0.55, 0.85 },
		};
	};
	return {
		.sides = { side(-1.), side(1.) },
		.delay = kSparkleDelay,
		.spread = kSparkleSpread,
		.lifeMin = kSparkleLifeMin,
		.lifeMax = kSparkleLifeMax,
		.size = { 0.05, 0.09 },
		.alpha = { 0.6, 1. },
		.twinkle = { 0.1, 1.9 },
		.appearTill = 0.2,
		.fadeAfter = 0.5,
		.deformation = 0.1,
	};
}

void PaintRing(
		QPainter &p,
		QPointF center,
		float64 side,
		crl::time elapsed) {
	if (elapsed < kRingDelay || elapsed >= kLandingDuration) {
		return;
	}
	const auto progress = (elapsed - kRingDelay) / float64(kRingDuration);
	const auto radius = side * (kRingStartScale + kRingGrowth * progress) / 2.;
	const auto opacity = p.opacity();
	p.setOpacity(opacity * kRingOpacity * (1. - progress));
	p.setPen(QPen(QColor(255, 255, 255), st::walletCardStickerRingWidth));
	p.setBrush(Qt::NoBrush);
	p.drawEllipse(center, radius, radius);
	p.setOpacity(opacity);
}

void PaintSticker(
		QPainter &p,
		const Data::StoryArea &area,
		const QImage &image,
		QSize card,
		QSizeF size) {
	p.save();
	p.translate(CardStickerCenter(area, card));
	p.rotate(CardStickerRotation(area));
	p.drawImage(
		QRectF(QPointF(-size.width() / 2., -size.height() / 2.), size),
		image);
	p.restore();
}

void ClipToCard(QPainter &p, QSize card) {
	auto clip = QPainterPath();
	clip.addRoundedRect(
		QRectF(QPointF(), QSizeF(card)),
		st::walletCardRadius,
		st::walletCardRadius);
	p.setClipPath(clip);
}

void PrepareLayer(QImage &layer, QSize size, int ratio) {
	const auto pixels = size * ratio;
	if (layer.size() != pixels || layer.devicePixelRatio() != ratio) {
		layer = QImage(pixels, QImage::Format_ARGB32_Premultiplied);
		layer.setDevicePixelRatio(ratio);
	}
	layer.fill(Qt::transparent);
}

void PaintLayer(QPainter &p, const CardFold &fold, const QImage &layer) {
	if (layer.isNull()) {
		return;
	}
	p.save();
	p.setOpacity(fold.opacity);
	p.setTransform(fold.transform, true);
	p.drawImage(QRectF(fold.rest), layer);
	p.restore();
}

} // namespace

struct CardStickers::Entry {
	not_null<DocumentData*> document;
	Data::WalletCardSticker data;
	QString canonical;
	std::shared_ptr<Data::DocumentMedia> media;
	std::unique_ptr<HistoryView::StickerPlayer> player;
	std::optional<StickerPlayback> playback;
	QImage image;
	int imageSide = 0;
	int side = 0;
	int ratio = 0;
	bool handed = false;
	rpl::lifetime loading;
};

struct CardStickers::Landing {
	QString address;
	QImage frame;
	std::unique_ptr<Ui::StarBurst> burst;
	crl::time started = 0;
	float64 fromScale = 1.;
};

struct CardStickers::Sinking {
	QString address;
	crl::time started = 0;
};

struct CardStickers::Preview {
	QString address;
	Data::StoryArea area;
};

void RefreshOwnCard(not_null<Main::Session*> session) {
	session->api().requestFullPeer(session->user());
}

auto OwnCardStickersValue(not_null<Main::Session*> session)
-> rpl::producer<std::vector<Data::WalletCardSticker>> {
	return session->wallet().ownCard().shownValue(
	) | rpl::map([](const OwnCardState &state) {
		return state.stickers;
	}) | rpl::distinct_until_changed();
}

QImage CardStickerOutlined(QImage frame, int ratio) {
	frame = std::move(frame).convertToFormat(
		QImage::Format_ARGB32_Premultiplied);
	const auto outline = CardStickerOutline(
		std::max(frame.width(), frame.height()));
	auto grid = OutlineSites(frame, outline);
	OutlineDistances(grid, outline);
	OutlineAlphas(grid, outline);
	auto result = OutlineStroke(grid, frame, outline);
	result.setDevicePixelRatio(ratio);
	return result;
}

bool TravelArtworkPaused() {
	return anim::Disabled() || PowerSaving::On(PowerSaving::kStickersChat);
}

void BindStickerPlayback(StickerPlayback &playback, Fn<void()> repaint) {
	const auto raw = &playback;
	playback.player->setRepaintCallback([=] {
		raw->awaitingFrame = false;
		repaint();
	});
}

StickerPlaybackStep AdvanceStickerPlayback(
		StickerPlayback &playback,
		crl::time now) {
	const auto player = playback.player.get();
	if (!player || playback.awaitingFrame || !player->ready()) {
		return {};
	}
	auto info = player->frame(
		playback.frameSize,
		QColor(0, 0, 0, 0),
		false,
		now,
		false);
	if (info.image.isNull()) {
		return {};
	} else if (playback.advanced
		&& (info.index == 0 || info.index < playback.index)) {
		return { .finished = true };
	}
	auto result = StickerPlaybackStep{ .show = true };
	if (playback.frame.isNull() || info.index != playback.index) {
		result.fresh = std::move(info.image);
		playback.index = info.index;
	}
	return result;
}

bool PresentStickerPlayback(StickerPlayback &playback) {
	const auto player = playback.player.get();
	if (player->framesCount() == 1) {
		return false;
	} else if (player->markFrameShown()) {
		playback.advanced = true;
		playback.awaitingFrame = true;
	}
	return true;
}

void StackCardStickers(
		std::vector<Data::WalletCardSticker> &stickers,
		const std::vector<QString> &stack) {
	const auto position = [&](const Data::WalletCardSticker &sticker) {
		return ranges::find(stack, CanonicalAddress(sticker.address))
			- begin(stack);
	};
	ranges::stable_sort(stickers, ranges::less(), position);
}

CardStickers::CardStickers(
	not_null<Main::Session*> session,
	rpl::producer<std::vector<Data::WalletCardSticker>> stickers,
	Fn<void()> repaint)
: _session(session)
, _repaint(std::move(repaint))
, _landingAnimation([=](crl::time now) { return landingStep(now); })
, _sinkAnimation([=](crl::time now) { return sinkStep(now); }) {
	std::move(
		stickers
	) | rpl::on_next([=](std::vector<Data::WalletCardSticker> &&list) {
		apply(std::move(list));
	}, _lifetime);

	session->wallet().ownCard().failures(
	) | rpl::on_next([=] {
		lowerRejected();
	}, _lifetime);
}

CardStickers::~CardStickers() = default;

void CardStickers::paint(QPainter &p, const CardFold &fold) {
	const auto ratio = style::DevicePixelRatio();
	const auto size = fold.rest.size();
	if (_layerSize != size || _layerRatio != ratio) {
		_layerSize = size;
		_layerRatio = ratio;
		_layerValid = _aboveValid = false;
	}
	for (const auto &entry : _entries) {
		if (validate(entry.get())) {
			_layerValid = _aboveValid = false;
		}
	}
	const auto now = crl::now();
	if (!_landings.empty()) {
		pruneLandings(now);
	}
	if (!_landings.empty() || !_sinkings.empty()) {
		_aboveValid = false;
	}
	if (!_layerValid || !_aboveValid) {
		compose(now);
	}
	PaintLayer(p, fold, _layer);
}

void CardStickers::paintAbove(QPainter &p, const CardFold &fold) const {
	PaintLayer(p, fold, _above);
}

void CardStickers::hold(const QString &address) {
	const auto canonical = CanonicalAddress(address);
	if (canonical.isEmpty()) {
		return;
	}
	_holds.emplace(canonical);
	dropArrival(canonical);
	if (const auto entry = lookup(canonical)) {
		entry->playback.reset();
	}
	invalidate();
	refreshArriving();
}

void CardStickers::release(const QString &address) {
	const auto canonical = CanonicalAddress(address);
	if (_holds.remove(canonical)) {
		sink(canonical, crl::now());
		invalidate();
		refreshArriving();
		_releases.fire_copy(canonical);
	}
}

void CardStickers::grip(const QString &address) {
	const auto canonical = CanonicalAddress(address);
	if (canonical.isEmpty() || !_grips.emplace(canonical).second) {
		return;
	}
	raise(canonical);
}

void CardStickers::ungrip(const QString &address) {
	const auto canonical = CanonicalAddress(address);
	if (_grips.remove(canonical) && _rejectedGrips.remove(canonical)) {
		_raised.erase(ranges::remove(_raised, canonical), end(_raised));
		invalidate();
	}
}

void CardStickers::raise(const QString &address) {
	const auto canonical = CanonicalAddress(address);
	if (canonical.isEmpty()
		|| (!_raised.empty() && _raised.back() == canonical)) {
		return;
	}
	_raised.erase(ranges::remove(_raised, canonical), end(_raised));
	_raised.push_back(canonical);
	invalidate();
}

void CardStickers::land(
		const QString &address,
		float64 fromScale,
		QImage frame,
		TravelArtwork artwork) {
	const auto canonical = CanonicalAddress(address);
	if (canonical.isEmpty()) {
		return;
	}
	const auto ended = _holds.remove(canonical);
	dropArrival(canonical);
	const auto entry = lookup(canonical);
	if (!anim::Disabled() && entry) {
		_landings.push_back({
			.address = canonical,
			.frame = (frame.isNull()
				? artwork.playback.frame
				: std::move(frame)),
			.burst = Ui::StarBurst::Make(LandingBurstDescriptor()),
			.started = crl::now(),
			.fromScale = (std::isfinite(fromScale) && fromScale > 0.)
				? fromScale
				: 1.,
		});
		if (!_landingAnimation.animating()) {
			_landingAnimation.start();
		}
	}
	if (entry) {
		adoptArtwork(entry, std::move(artwork));
	}
	invalidate();
	refreshArriving();
	if (ended) {
		_releases.fire_copy(canonical);
	}
}

void CardStickers::preview(
		const QString &address,
		const Data::StoryArea &area) {
	const auto entry = find(address);
	const auto &geometry = area.geometry;
	const auto finite = std::isfinite(geometry.x()
		+ geometry.y()
		+ geometry.width()
		+ geometry.height()
		+ area.rotation);
	if (!entry
		|| !finite
		|| geometry.width() <= 0.
		|| geometry.height() <= 0.) {
		clearPreview();
		return;
	}
	dropArrival(entry->canonical);
	const auto entering = !_preview
		|| (_preview->address != entry->canonical);
	if (!_preview) {
		_preview = std::make_unique<Preview>();
	}
	_preview->address = entry->canonical;
	_preview->area = area;
	if (entering) {
		invalidate();
	} else {
		invalidateAbove();
	}
	refreshArriving();
}

void CardStickers::clearPreview() {
	if (const auto preview = base::take(_preview)) {
		if (!ranges::contains(_landings, preview->address, &Landing::address)) {
			sink(preview->address, crl::now());
		}
		invalidate();
		refreshArriving();
	}
}

void CardStickers::play(const QString &address) {
	const auto entry = find(address);
	if (!entry
		|| entry->playback
		|| entry->image.isNull()
		|| entry->side <= 0
		|| !entry->media
		|| !entry->media->loaded()
		|| _holds.contains(entry->canonical)
		|| anim::Disabled()) {
		return;
	}
	const auto sticker = entry->document->sticker();
	if (!sticker || !sticker->isAnimated()) {
		return;
	}
	entry->playback = StickerPlayback{
		.player = MakePlayer(
			entry->document,
			entry->media->bytes(),
			entry->side,
			entry->ratio),
		.frameSize = FrameSize(entry->document, entry->side),
		.side = entry->side,
	};
	entry->handed = false;
	BindStickerPlayback(*entry->playback, [=] { invalidate(); });
}

void CardStickers::stopPlayback() {
	auto stopped = false;
	for (const auto &entry : _entries) {
		if (entry->playback) {
			entry->playback.reset();
			stopped = true;
		}
	}
	if (stopped) {
		invalidate();
	}
}

bool CardStickers::shown(const QString &address) const {
	const auto entry = find(address);
	return entry
		&& (entry->side > 0)
		&& !entry->image.isNull()
		&& !_holds.contains(entry->canonical);
}

bool CardStickers::held(const QString &address) const {
	return _holds.contains(CanonicalAddress(address));
}

QImage CardStickers::image(const QString &address) const {
	const auto entry = find(address);
	if (!entry) {
		return QImage();
	} else if (entry->playback && !entry->playback->frame.isNull()) {
		return entry->playback->frame;
	}
	return entry->image;
}

std::vector<QString> CardStickers::stack() const {
	auto result = std::vector<QString>();
	for (const auto &entry : ordered()) {
		result.push_back(entry->canonical);
	}
	return result;
}

const QImage &CardStickers::settledLayer() const {
	return _layer;
}

rpl::producer<bool> CardStickers::arrivingValue() const {
	return _arriving.value();
}

rpl::producer<QString> CardStickers::releases() const {
	return _releases.events();
}

void CardStickers::apply(std::vector<Data::WalletCardSticker> &&stickers) {
	auto was = base::take(_entries);
	for (auto &sticker : stickers) {
		if (!sticker.gift) {
			continue;
		}
		const auto document = sticker.gift->document;
		const auto canonical = CanonicalAddress(sticker.address);
		const auto i = ranges::find_if(was, [&](const auto &entry) {
			return entry
				&& (entry->document == document)
				&& (entry->canonical == canonical);
		});
		auto entry = (i != end(was))
			? std::move(*i)
			: std::make_unique<Entry>(Entry{ .document = document });
		entry->data = std::move(sticker);
		entry->canonical = canonical;
		_entries.push_back(std::move(entry));
	}
	_landings.erase(ranges::remove_if(_landings, [&](const Landing &landing) {
		return !lookup(landing.address);
	}), end(_landings));
	_sinkings.erase(ranges::remove_if(_sinkings, [&](const Sinking &sinking) {
		return !lookup(sinking.address);
	}), end(_sinkings));
	_raised.erase(ranges::remove_if(_raised, [&](const QString &canonical) {
		return !lookup(canonical) && !_holds.contains(canonical);
	}), end(_raised));
	if (_preview && !lookup(_preview->address)) {
		_preview = nullptr;
		refreshArriving();
	}
	invalidate();
}

std::vector<not_null<CardStickers::Entry*>> CardStickers::ordered() const {
	auto result = std::vector<not_null<Entry*>>();
	result.reserve(_entries.size());
	for (const auto &entry : _entries) {
		if (!ranges::contains(_raised, entry->canonical)) {
			result.push_back(entry.get());
		}
	}
	for (const auto &canonical : _raised) {
		if (const auto entry = lookup(canonical)) {
			result.push_back(entry);
		}
	}
	return result;
}

void CardStickers::lowerRejected() {
	_raised.erase(ranges::remove_if(_raised, [&](const QString &canonical) {
		return !_grips.contains(canonical);
	}), end(_raised));
	_rejectedGrips = _grips;
	invalidate();
}

bool CardStickers::validate(not_null<Entry*> entry) {
	auto changed = false;
	const auto side = CardStickerSide(entry->data.area, _layerSize);
	if (entry->side != side || entry->ratio != _layerRatio) {
		changed = !entry->image.isNull();
		prepare(entry, side);
	}
	if (entry->player && entry->player->ready()) {
		capture(entry);
		changed = changed || !entry->image.isNull();
	}
	return changed;
}

void CardStickers::prepare(not_null<Entry*> entry, int side) {
	entry->loading.destroy();
	entry->player = nullptr;
	if (!entry->handed) {
		entry->playback.reset();
	}
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
	entry->player = MakePlayer(
		entry->document,
		entry->media->bytes(),
		entry->side,
		entry->ratio);
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
		entry->image = CardStickerOutlined(std::move(frame), entry->ratio);
		entry->imageSide = entry->side;
	}
}

void CardStickers::adoptArtwork(
		not_null<Entry*> entry,
		TravelArtwork &&artwork) {
	auto &playback = artwork.playback;
	if (artwork.document != entry->document || playback.side <= 0) {
		return;
	} else if (validate(entry)) {
		_layerValid = _aboveValid = false;
	}
	if (entry->image.isNull() && !artwork.rest.isNull()) {
		entry->image = std::move(artwork.rest);
		entry->imageSide = playback.side;
	}
	if (!playback.player
		|| playback.frameSize.isEmpty()
		|| TravelArtworkPaused()) {
		return;
	}
	entry->playback = std::move(playback);
	entry->handed = true;
	BindStickerPlayback(*entry->playback, [=] { invalidate(); });
}

void CardStickers::compose(crl::time now) {
	const auto below = !_layerValid;
	const auto above = !_aboveValid;
	_layerValid = _aboveValid = true;
	struct Lifted {
		not_null<Entry*> entry;
		const Landing *landing = nullptr;
		float64 cover = 0.;
	};
	auto lower = std::vector<not_null<Entry*>>();
	auto upper = std::vector<Lifted>();
	auto shown = false;
	if (!_layerSize.isEmpty()) {
		for (const auto &entry : ordered()) {
			if (_holds.contains(entry->canonical)) {
				continue;
			}
			const auto landing = ranges::find(
				_landings,
				entry->canonical,
				&Landing::address);
			const auto previewed = _preview
				&& (_preview->address == entry->canonical);
			if (previewed || landing != end(_landings)) {
				upper.push_back({
					.entry = entry,
					.landing = previewed ? nullptr : &*landing,
				});
				continue;
			}
			lower.push_back(entry);
			shown = shown || !entry->image.isNull();
			const auto cover = sinkCover(entry->canonical, now);
			if (cover > 0.) {
				upper.push_back({ .entry = entry, .cover = cover });
			}
		}
	}
	if (below && !shown) {
		_layer = QImage();
	} else if (below) {
		PrepareLayer(_layer, _layerSize, _layerRatio);
		auto p = QPainter(&_layer);
		auto hq = PainterHighQualityEnabler(p);
		ClipToCard(p, _layerSize);
		for (const auto &entry : lower) {
			const auto playing = advance(entry, now);
			paintArt(p, entry, entry->data.area, 1.);
			if (playing) {
				presented(entry);
			}
		}
	}
	if (above && upper.empty()) {
		_above = QImage();
	} else if (above) {
		PrepareLayer(_above, _layerSize, _layerRatio);
		auto p = QPainter(&_above);
		auto hq = PainterHighQualityEnabler(p);
		ClipToCard(p, _layerSize);
		for (const auto &lifted : upper) {
			const auto entry = lifted.entry;
			if (lifted.cover > 0.) {
				// WHY: a sinking gift is also in the lower layer, so this
				// copy fades out to fade the card data in over it, with the
				// frame that layer drew.
				p.setOpacity(lifted.cover);
				paintArt(p, entry, entry->data.area, 1.);
				p.setOpacity(1.);
				continue;
			}
			const auto playing = advance(entry, now);
			if (lifted.landing) {
				paintLanding(p, entry, *lifted.landing, now);
			} else {
				paintArt(p, entry, _preview->area, 1.);
			}
			if (playing) {
				presented(entry);
			}
		}
	}
}

bool CardStickers::advance(not_null<Entry*> entry, crl::time now) {
	if (!entry->playback) {
		return false;
	} else if (entry->handed ? TravelArtworkPaused() : anim::Disabled()) {
		entry->playback.reset();
		return false;
	}
	auto &playback = *entry->playback;
	auto step = AdvanceStickerPlayback(playback, now);
	if (step.finished) {
		entry->playback.reset();
		return false;
	} else if (!step.fresh.isNull()) {
		playback.frame = CardStickerOutlined(
			std::move(step.fresh),
			entry->ratio);
	}
	return step.show;
}

void CardStickers::presented(not_null<Entry*> entry) {
	if (!PresentStickerPlayback(*entry->playback)) {
		entry->playback.reset();
	}
}

bool CardStickers::paintArt(
		QPainter &p,
		not_null<const Entry*> entry,
		const Data::StoryArea &area,
		float64 scale) const {
	const auto playing = entry->playback
		&& !entry->playback->frame.isNull();
	const auto &image = playing ? entry->playback->frame : entry->image;
	const auto from = playing ? entry->playback->side : entry->imageSide;
	const auto side = CardStickerSide(area, _layerSize);
	if (image.isNull() || from <= 0 || side <= 0) {
		return false;
	}
	const auto size = LogicalSize(image) * (scale * side / float64(from));
	PaintSticker(p, area, image, _layerSize, size);
	return true;
}

void CardStickers::paintLanding(
		QPainter &p,
		not_null<const Entry*> entry,
		const Landing &landing,
		crl::time now) const {
	const auto side = float64(entry->side);
	if (side <= 0.) {
		return;
	}
	const auto &area = entry->data.area;
	const auto center = CardStickerCenter(area, _layerSize);
	const auto elapsed = now - landing.started;
	PaintRing(p, center, side, elapsed);
	if (landing.burst) {
		landing.burst->paint(p, {
			.origin = center,
			.extent = side,
			.elapsed = elapsed,
		});
	}
	const auto settle = EaseOut(elapsed / float64(kSettleDuration));
	const auto scale = 1. + (landing.fromScale - 1.) * (1. - settle);
	if (!paintArt(p, entry, area, scale) && !landing.frame.isNull()) {
		const auto size = QSizeF(landing.frame.size()).scaled(
			QSizeF(side, side),
			Qt::KeepAspectRatio) * scale;
		PaintSticker(p, area, landing.frame, _layerSize, size);
	}
}

CardStickers::Entry *CardStickers::find(const QString &address) const {
	const auto canonical = CanonicalAddress(address);
	return canonical.isEmpty() ? nullptr : lookup(canonical);
}

CardStickers::Entry *CardStickers::lookup(const QString &canonical) const {
	const auto i = ranges::find(
		_entries,
		canonical,
		&Entry::canonical);
	return (i != end(_entries)) ? i->get() : nullptr;
}

void CardStickers::dropArrival(const QString &canonical) {
	_landings.erase(
		ranges::remove(_landings, canonical, &Landing::address),
		end(_landings));
	_sinkings.erase(
		ranges::remove(_sinkings, canonical, &Sinking::address),
		end(_sinkings));
}

void CardStickers::pruneLandings(crl::time now) {
	const auto disabled = anim::Disabled();
	const auto was = _landings.size();
	auto finished = std::vector<QString>();
	_landings.erase(ranges::remove_if(_landings, [&](const Landing &landing) {
		if (disabled) {
			return true;
		} else if (now - landing.started < kLandingDuration) {
			return false;
		}
		const auto entry = lookup(landing.address);
		const auto ended = !entry
			|| !entry->image.isNull()
			|| landing.frame.isNull();
		if (ended) {
			finished.push_back(landing.address);
		}
		return ended;
	}), end(_landings));
	if (_landings.size() != was) {
		_layerValid = _aboveValid = false;
	}
	for (const auto &address : finished) {
		sink(address, now);
	}
}

bool CardStickers::landingStep(crl::time now) {
	pruneLandings(now);
	invalidateAbove();
	const auto active = ranges::any_of(_landings, [&](const Landing &landing) {
		return now - landing.started < kLandingDuration;
	});
	if (!active) {
		_landingAnimation.stop();
	}
	refreshArriving();
	return active;
}

void CardStickers::sink(const QString &canonical, crl::time now) {
	const auto entry = lookup(canonical);
	if (!entry || entry->image.isNull() || anim::Disabled()) {
		return;
	}
	_sinkings.erase(
		ranges::remove(_sinkings, canonical, &Sinking::address),
		end(_sinkings));
	_sinkings.push_back({ .address = canonical, .started = now });
	_aboveValid = false;
	if (!_sinkAnimation.animating()) {
		_sinkAnimation.start();
	}
}

float64 CardStickers::sinkCover(
		const QString &canonical,
		crl::time now) const {
	const auto i = ranges::find(_sinkings, canonical, &Sinking::address);
	if (i == end(_sinkings)) {
		return 0.;
	}
	const auto progress = (now - i->started) / float64(kSinkDuration);
	return 1. - anim::sineInOut(1., std::clamp(progress, 0., 1.));
}

void CardStickers::pruneSinkings(crl::time now) {
	const auto disabled = anim::Disabled();
	const auto was = _sinkings.size();
	const auto finished = [&](const Sinking &sinking) {
		return disabled
			|| (now - sinking.started >= kSinkDuration)
			|| !lookup(sinking.address);
	};
	_sinkings.erase(
		ranges::remove_if(_sinkings, finished),
		end(_sinkings));
	if (_sinkings.size() != was) {
		_aboveValid = false;
	}
}

bool CardStickers::sinkStep(crl::time now) {
	pruneSinkings(now);
	invalidateAbove();
	if (_sinkings.empty()) {
		_sinkAnimation.stop();
		return false;
	}
	return true;
}

void CardStickers::refreshArriving() {
	_arriving = _landingAnimation.animating()
		|| !_holds.empty()
		|| (_preview != nullptr);
}

void CardStickers::invalidate() {
	_layerValid = _aboveValid = false;
	_repaint();
}

void CardStickers::invalidateAbove() {
	_aboveValid = false;
	_repaint();
}

} // namespace Wallet
