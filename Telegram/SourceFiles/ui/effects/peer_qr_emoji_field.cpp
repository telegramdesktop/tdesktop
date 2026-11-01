/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/peer_qr_emoji_field.h"

#include "ui/effects/animation_value.h"
#include "ui/text/text_custom_emoji.h"

#include "styles/style_boxes.h"

namespace Ui {
namespace {

constexpr auto kEmptyShare = 0.2;
constexpr auto kBigTier = 1.17;
constexpr auto kSmallTier = 0.9;
constexpr auto kNodeScaleMin = 0.92;
constexpr auto kNodeScaleMax = 1.08;
constexpr auto kTiltMax = 13.;
constexpr auto kSwayAngle = 4.;
constexpr auto kSwaySpeed = 0.3;
constexpr auto kBobSpeed = 0.45;
constexpr auto kScaleCutoff = 0.05;
constexpr auto kCardShrink = 0.03;
constexpr auto kEntryStagger = crl::time(60);
constexpr auto kPopDuration = crl::time(315);
constexpr auto kShrinkDuration = crl::time(180);
constexpr auto kPopDelay = crl::time(135);
constexpr auto kSwapStagger = crl::time(300);
constexpr auto kSwapDuration = kSwapStagger
	+ std::max(kShrinkDuration, kPopDelay + kPopDuration);

enum class Salt : uint32 {
	Empty = 1,
	Scale,
	Tilt,
	Radius,
	Angle,
	Sway,
	Bob,
	Delay,
};

struct Tile {
	QPointF center;
	float64 size = 0.;
	float64 rotation = 0.;
	float64 delay = 0.;
	int row = 0;
	int column = 0;
	int pick = 0;
};

[[nodiscard]] float64 NodeRandom(int row, int column, Salt salt) {
	return PeerQrNodeHash(row, column, uint32(salt)) / 4294967296.;
}

[[nodiscard]] int PickIndex(int row, int column, int count) {
	const auto q = column - (row - (row & 1)) / 2;
	const auto index = (q - row) % count;
	return (index < 0) ? (index + count) : index;
}

template <typename Callback>
void EnumerateTiles(
		const PeerQrLayerFalloff &falloff,
		QPointF origin,
		const PeerQrEmojiMetrics &metrics,
		int count,
		float64 seconds,
		Callback &&callback) {
	if (count <= 0 || metrics.skipX <= 0. || metrics.skipY <= 0.) {
		return;
	}
	const auto reach = falloff.reach(metrics.offsetMax
		+ metrics.bob
		+ metrics.size * kBigTier * kNodeScaleMax / 2.);
	const auto rowFrom = int(std::ceil(
		(reach.top() - origin.y()) / metrics.skipY));
	const auto rowTill = int(std::floor(
		(reach.bottom() - origin.y()) / metrics.skipY));
	for (auto row = rowFrom; row <= rowTill; ++row) {
		const auto odd = (row & 1) != 0;
		const auto shift = odd ? 0.5 : 0.;
		const auto tier = odd ? kSmallTier : kBigTier;
		const auto columnFrom = int(std::ceil(
			(reach.left() - origin.x()) / metrics.skipX - shift));
		const auto columnTill = int(std::floor(
			(reach.right() - origin.x()) / metrics.skipX - shift));
		for (auto column = columnFrom; column <= columnTill; ++column) {
			const auto random = [&](Salt salt) {
				return NodeRandom(row, column, salt);
			};
			if (random(Salt::Empty) < kEmptyShare) {
				continue;
			}
			const auto spread = metrics.offsetMax - metrics.offsetMin;
			const auto radius = metrics.offsetMin
				+ spread * random(Salt::Radius);
			const auto angle = 2. * M_PI * random(Salt::Angle);
			const auto bob = metrics.bob * std::sin(
				kBobSpeed * seconds + 2. * M_PI * random(Salt::Bob));
			const auto flat = origin + QPointF(
				(column + shift) * metrics.skipX + radius * std::cos(angle),
				row * metrics.skipY + radius * std::sin(angle) + bob);
			const auto scale = kNodeScaleMin
				+ (kNodeScaleMax - kNodeScaleMin) * random(Salt::Scale);
			const auto size = metrics.size * tier * scale;
			const auto place = PeerQrPlaceItem(falloff, flat, size);
			if (!place) {
				continue;
			}
			const auto tilt = kTiltMax * (2. * random(Salt::Tilt) - 1.);
			const auto sway = kSwayAngle * std::sin(
				kSwaySpeed * seconds + 2. * M_PI * random(Salt::Sway));
			callback(Tile{
				.center = place->position,
				.size = size * place->scale,
				.rotation = tilt + sway,
				.delay = random(Salt::Delay),
				.row = row,
				.column = column,
				.pick = PickIndex(row, column, count),
			});
		}
	}
}

[[nodiscard]] float64 PopScale(crl::time now, crl::time start) {
	if (now <= start) {
		return 0.;
	} else if (now >= start + kPopDuration) {
		return 1.;
	}
	return anim::easeOutBack(1., float64(now - start) / kPopDuration);
}

[[nodiscard]] float64 ShrinkScale(crl::time now, crl::time start) {
	if (now <= start) {
		return 1.;
	} else if (now >= start + kShrinkDuration) {
		return 0.;
	}
	return 1. - anim::easeInCubic(1., float64(now - start) / kShrinkDuration);
}

} // namespace

uint32 PeerQrHash(uint32 value) {
	value ^= value >> 16;
	value *= 0x7FEB352DU;
	value ^= value >> 15;
	value *= 0x846CA68BU;
	value ^= value >> 16;
	return value;
}

uint32 PeerQrNodeHash(int row, int column, uint32 salt) {
	const auto seeded = PeerQrHash(salt * 0x9E3779B9U + uint32(row));
	return PeerQrHash(seeded ^ (uint32(column) * 0x85EBCA6BU));
}

QImage PeerQrRenderStill(
		not_null<Text::CustomEmoji*> art,
		int frameSize,
		QColor textColor) {
	const auto ratio = style::DevicePixelRatio();
	auto result = QImage(
		QSize(frameSize, frameSize) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	result.fill(Qt::transparent);
	auto p = QPainter(&result);
	art->paint(p, {
		.textColor = textColor,
		.internal = { .forceFirstFrame = true },
	});
	p.end();
	return result;
}

void PeerQrApplyItemTransform(
		QPainter &p,
		const QTransform &base,
		QPointF center,
		float64 rotation,
		float64 size,
		float64 frame) {
	auto transform = base;
	transform.translate(center.x(), center.y());
	transform.rotate(rotation);
	transform.scale(size / frame, size / frame);
	transform.translate(-frame / 2., -frame / 2.);
	p.setTransform(transform);
}

std::optional<PeerQrLayerPlace> PeerQrPlaceItem(
		const PeerQrLayerFalloff &falloff,
		QPointF flat,
		float64 size) {
	const auto place = falloff.place(flat);
	if (place.scale < kScaleCutoff) {
		return std::nullopt;
	}
	const auto extent = size * place.scale / M_SQRT2;
	const auto bounds = QRectF(
		place.position - QPointF(extent, extent),
		QSizeF(2. * extent, 2. * extent));
	if (!bounds.intersects(falloff.area)) {
		return std::nullopt;
	}
	return place;
}

QRectF PeerQrCardCull(QRectF card) {
	const auto x = card.width() * kCardShrink / 2.;
	const auto y = card.height() * kCardShrink / 2.;
	return card.marginsRemoved({ x, y, x, y });
}

// WHY: the card's corner radius is unknown here; a tile inside the ellipse
// inscribed in the dipped card is covered for any reasonable radius.
bool PeerQrCovered(const QRectF &cull, QPointF center, float64 size) {
	if (cull.isEmpty()) {
		return false;
	}
	const auto radius = size / M_SQRT2;
	const auto middle = cull.center();
	const auto x = (std::abs(center.x() - middle.x()) + radius)
		/ (cull.width() / 2.);
	const auto y = (std::abs(center.y() - middle.y()) + radius)
		/ (cull.height() / 2.);
	return (x * x + y * y) <= 1.;
}

PeerQrEmojiMetrics PeerQrEmojiMetrics::Live() {
	return {
		.skipX = float64(st::peerQrEmojiSkipX),
		.skipY = float64(st::peerQrEmojiSkipY),
		.size = float64(st::peerQrEmojiSize),
		.offsetMin = float64(st::peerQrEmojiOffsetMin),
		.offsetMax = float64(st::peerQrEmojiOffsetMax),
		.bob = float64(st::peerQrEmojiBob),
	};
}

void PaintPeerQrEmojiRest(
		QPainter &p,
		const PeerQrLayerFalloff &falloff,
		QPointF origin,
		QRectF card,
		const PeerQrEmojiRest &rest) {
	const auto count = int(rest.stills.size());
	if (!count) {
		return;
	}
	const auto cull = PeerQrCardCull(card);
	p.save();
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	const auto base = p.transform();
	const auto paint = [&](const Tile &tile) {
		const auto &still = rest.stills[tile.pick];
		if (still.isNull() || PeerQrCovered(cull, tile.center, tile.size)) {
			return;
		}
		const auto frame = QRectF(QPointF(), QSizeF(still.size()));
		PeerQrApplyItemTransform(
			p,
			base,
			tile.center,
			tile.rotation,
			tile.size,
			frame.width());
		p.drawImage(frame, still);
	};
	EnumerateTiles(falloff, origin, rest.metrics, count, 0., paint);
	p.restore();
}

PeerQrEmojiField::PeerQrEmojiField(
	PeerQrEmojiSource source,
	Fn<void()> repaint)
: _source(std::move(source))
, _repaint(std::move(repaint))
, _textColor(st::windowFg->c) {
}

PeerQrEmojiField::~PeerQrEmojiField() = default;

void PeerQrEmojiField::setSource(PeerQrEmojiSource source) {
	const auto resized = (_source.frameSize != source.frameSize);
	_source = std::move(source);
	auto created = false;
	for (auto &pick : _picks) {
		if (!pick.emoji) {
			pick.emoji = createEmoji(pick.key);
			created = created || (pick.emoji != nullptr);
		}
	}
	if ((created || resized) && _repaint) {
		_repaint();
	}
}

void PeerQrEmojiField::setPicks(
		std::vector<QString> keys,
		PeerQrEmojiChange change,
		crl::time now) {
	auto unique = std::vector<QString>();
	unique.reserve(keys.size());
	for (auto &key : keys) {
		if (!ranges::contains(unique, key)) {
			unique.push_back(std::move(key));
		}
	}
	const auto same = ranges::equal(
		unique,
		_picks,
		ranges::equal_to(),
		ranges::identity(),
		&Pick::key);
	if (same && change != PeerQrEmojiChange::Entry) {
		return;
	}
	auto previous = _picks
		| ranges::views::transform(&Pick::key)
		| ranges::to_vector;
	auto removed = base::take(_picks);
	const auto take = [&](std::vector<Pick> &from, const QString &key) {
		const auto i = ranges::find(from, key, &Pick::key);
		if (i == end(from)) {
			return false;
		}
		_picks.push_back(std::move(*i));
		from.erase(i);
		return true;
	};
	for (const auto &key : unique) {
		if (!take(removed, key) && !take(_leaving, key)) {
			_picks.push_back({ .key = key, .emoji = createEmoji(key) });
		}
	}
	const auto settled = now - kEntryStagger - kPopDuration;
	const auto instant = (change == PeerQrEmojiChange::Instant);
	const auto readyAt = instant ? settled : now;
	for (auto &pick : _picks) {
		if (pick.readyAt) {
			pick.readyAt = std::min(pick.readyAt, readyAt);
		} else if (pick.emoji && pick.emoji->ready()) {
			pick.readyAt = readyAt;
		}
	}
	if (change == PeerQrEmojiChange::Swap) {
		_previous = std::move(previous);
		_leaving = std::move(removed);
		_changedAt = now;
		return;
	}
	finishSwap();
	_entryAt = std::nullopt;
	if (instant) {
		_entryAt = settled;
	}
}

void PeerQrEmojiField::startEntry(crl::time now) {
	if (!_entryAt) {
		finishSwap();
		_entryAt = now;
	}
}

void PeerQrEmojiField::setTextColor(QColor color) {
	_textColor = color;
}

void PeerQrEmojiField::paint(QPainter &p, const PeerQrEmojiPaint &args) {
	const auto metrics = PeerQrEmojiMetrics::Live();
	const auto now = args.now;
	refreshReady(now);
	if (_changedAt && now >= _changedAt + kSwapDuration) {
		finishSwap();
	}
	if (_picks.empty() || !_entryAt || _source.frameSize <= 0) {
		return;
	}
	const auto previous = _previous
		| ranges::views::transform([&](const QString &key) {
			return findPick(key);
		})
		| ranges::to_vector;
	const auto entryAt = *_entryAt;
	const auto instant = anim::Disabled();
	const auto frame = float64(_source.frameSize);
	const auto cull = PeerQrCardCull(args.card);
	const auto appearAt = [&](const Pick &pick, const Tile &tile) {
		return std::max(entryAt, pick.readyAt)
			+ crl::time(kEntryStagger * tile.delay);
	};
	p.save();
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	const auto base = p.transform();
	const auto draw = [&](const Pick &pick, const Tile &tile, float64 scale) {
		const auto size = tile.size * scale;
		if (!pick.emoji
			|| size <= 0.
			|| PeerQrCovered(cull, tile.center, size)) {
			return;
		}
		PeerQrApplyItemTransform(
			p,
			base,
			tile.center,
			tile.rotation,
			size,
			frame);
		pick.emoji->paint(p, {
			.textColor = _textColor,
			.now = now,
			.paused = args.paused,
		});
	};
	const auto paintTile = [&](const Tile &tile) {
		const auto &pick = _picks[tile.pick];
		const auto old = previous.empty()
			? nullptr
			: previous[PickIndex(tile.row, tile.column, int(previous.size()))];
		const auto changed = _changedAt && (old != &pick);
		const auto delayed = _changedAt
			+ crl::time(kSwapStagger * tile.delay);
		if (changed && old && old->readyAt && !instant) {
			draw(
				*old,
				tile,
				(PopScale(now, appearAt(*old, tile))
					* ShrinkScale(now, delayed)));
		}
		if (pick.readyAt) {
			const auto start = changed
				? std::max(appearAt(pick, tile), delayed + kPopDelay)
				: appearAt(pick, tile);
			draw(pick, tile, instant ? 1. : PopScale(now, start));
		}
	};
	EnumerateTiles(
		args.falloff,
		args.origin,
		metrics,
		int(_picks.size()),
		args.elapsed / 1000.,
		paintTile);
	p.restore();
}

bool PeerQrEmojiField::animating(crl::time now) const {
	if (_picks.empty() || anim::Disabled()) {
		return false;
	} else if (!_entryAt || (_changedAt && now < _changedAt + kSwapDuration)) {
		return true;
	}
	const auto entryAt = *_entryAt;
	return ranges::any_of(_picks, [&](const Pick &pick) {
		return pick.readyAt
			&& (now < std::max(entryAt, pick.readyAt)
				+ kEntryStagger
				+ kPopDuration);
	});
}

PeerQrEmojiRest PeerQrEmojiField::prepareRest(
		PeerQrEmojiMetrics metrics) const {
	auto stills = std::vector<QImage>();
	stills.reserve(_picks.size());
	for (const auto &pick : _picks) {
		const auto shown = pick.emoji
			&& pick.readyAt
			&& (_source.frameSize > 0);
		stills.push_back(shown
			? PeerQrRenderStill(pick.emoji.get(), _source.frameSize, _textColor)
			: QImage());
	}
	return { .stills = std::move(stills), .metrics = metrics };
}

std::unique_ptr<Text::CustomEmoji> PeerQrEmojiField::createEmoji(
		const QString &key) const {
	return _source.factory
		? _source.factory(key, Text::MarkedContext{ .repaint = _repaint })
		: nullptr;
}

const PeerQrEmojiField::Pick *PeerQrEmojiField::findPick(
		const QString &key) const {
	for (const auto list : { &_picks, &_leaving }) {
		const auto i = ranges::find(*list, key, &Pick::key);
		if (i != end(*list)) {
			return &*i;
		}
	}
	return nullptr;
}

void PeerQrEmojiField::refreshReady(crl::time now) {
	if (_source.frameSize <= 0) {
		return;
	}
	for (auto &pick : _picks) {
		if (pick.emoji && !pick.readyAt && pick.emoji->ready()) {
			pick.readyAt = now;
		}
	}
}

void PeerQrEmojiField::finishSwap() {
	_previous.clear();
	_leaving.clear();
	_changedAt = 0;
}

} // namespace Ui
