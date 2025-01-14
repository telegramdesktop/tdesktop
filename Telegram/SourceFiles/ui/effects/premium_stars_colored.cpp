/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/premium_stars_colored.h"

#include "base/random.h"
#include "ui/effects/premium_graphics.h" // GiftGradientStops.
#include "ui/text/text_custom_emoji.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"

namespace Ui::Premium {
namespace {

constexpr auto kStarsCount = 16;
constexpr auto kTravelMax = 0.5;
constexpr auto kExcludeRadius = 0.7;
constexpr auto kFading = crl::time(200);
constexpr auto kLifetimeMin = crl::time(1000);
constexpr auto kLifetimeMax = 3 * kLifetimeMin;
constexpr auto kSizeMin = 0.1;
constexpr auto kSizeMax = 0.15;

[[nodiscard]] crl::time ChooseLife(base::BufferedRandom<uint32> &random) {
	return kLifetimeMin
		+ base::RandomIndex(kLifetimeMax - kLifetimeMin, random);
}

class CollectibleEmoji final : public Text::CustomEmoji {
public:
	CollectibleEmoji(
		QStringView entityData,
		QColor centerColor,
		QColor edgeColor,
		std::unique_ptr<Ui::Text::CustomEmoji> inner,
		Fn<void()> update,
		int size);

	int width() override;
	QString entityData() override;
	void paint(QPainter &p, const Context &context) override;
	void unload() override;
	bool ready() override;
	bool readyInDefaultState() override;

private:
	struct Star {
		QPointF start;
		QPointF delta;
		float64 size = 0.;
		crl::time birthTime = 0;
		crl::time deathTime = 0;
	};

	void fill();
	void refill(Star &star, base::BufferedRandom<uint32> &random);
	void prepareFrame();

	QString _entityData;
	QSvgRenderer _svg;
	std::vector<Star> _stars;
	QColor _centerColor;
	QColor _edgeColor;
	std::unique_ptr<Text::CustomEmoji> _inner;
	Ui::Animations::Basic _animation;
	QImage _frame;
	int _size = 0;

};

CollectibleEmoji::CollectibleEmoji(
	QStringView entityData,
	QColor centerColor,
	QColor edgeColor,
	std::unique_ptr<Ui::Text::CustomEmoji> inner,
	Fn<void()> update,
	int size)
: _entityData(entityData.toString())
, _svg(u":/gui/icons/settings/starmini.svg"_q)
, _centerColor(centerColor)
, _edgeColor(edgeColor)
, _inner(std::move(inner))
, _animation(std::move(update))
, _size(size) {
	fill();
}

void CollectibleEmoji::fill() {
	_stars.reserve(kStarsCount);
	const auto now = crl::now();
	auto random = base::BufferedRandom<uint32>(kStarsCount * 12);
	for (auto i = 0; i != kStarsCount; ++i) {
		const auto life = ChooseLife(random);
		const auto shift = base::RandomIndex(life - kFading, random);
		_stars.push_back({
			.birthTime = now - crl::time(shift),
			.deathTime = now - crl::time(shift) + crl::time(life),
		});
		refill(_stars.back(), random);
	}
}

void CollectibleEmoji::refill(
		Star &star,
		base::BufferedRandom<uint32> &random) {
	const auto take = [&] {
		return base::RandomIndex(_size * 16, random) / (_size * 15.);
	};
	const auto stake = [&] {
		return take() * 2. - 1.;
	};
	const auto exclude = kExcludeRadius * kExcludeRadius;
	auto square = 0.;
	while (true) {
		const auto start = QPointF(stake(), stake());
		square = start.x() * start.x() + start.y() * start.y();
		if (square > exclude) {
			star.start = start * _size;
			break;
		}
	}
	square *= _size * _size;
	while (true) {
		const auto delta = QPointF(stake(), stake()) * kTravelMax * _size;
		const auto end = star.start + delta;
		if (end.x() * end.x() + end.y() * end.y() > square) {
			star.delta = delta;
			break;
		}
	}
	star.start = (star.start + QPointF(_size, _size)) / 2.;
	star.size = (kSizeMin + (kSizeMax - kSizeMin) * take()) * _size;
}

int CollectibleEmoji::width() {
	return _inner->width();
}

QString CollectibleEmoji::entityData() {
	return _entityData;
}

void CollectibleEmoji::prepareFrame() {
	const auto clip = QSize(_size, _size);
	if (_frame.isNull()) {
		const auto ratio = style::DevicePixelRatio();
		_frame = QImage(clip * ratio, QImage::Format_ARGB32_Premultiplied);
		_frame.setDevicePixelRatio(ratio);
	}
	_frame.fill(Qt::transparent);
	auto p = QPainter(&_frame);
	auto hq = PainterHighQualityEnabler(p);
	auto random = std::optional<base::BufferedRandom<uint32>>();
	const auto now = crl::now();
	for (auto &star : _stars) {
		if (star.deathTime <= now) {
			if (!random) {
				random.emplace(kStarsCount * 10);
			}
			const auto life = ChooseLife(*random);
			star.birthTime = now;
			star.deathTime = now + crl::time(life);
			refill(star, *random);
		}
		Assert(star.birthTime <= now && now <= star.deathTime);

		const auto time = (now - star.birthTime)
			/ float64(star.deathTime - star.birthTime);
		const auto position = star.start + star.delta * time;
		const auto scale = ((now - star.birthTime) < kFading)
			? ((now - star.birthTime) / float64(kFading))
			: ((star.deathTime - now) < kFading)
			? ((star.deathTime - now) / float64(kFading))
			: 1.;
		if (scale > 0.) {
			const auto size = star.size * scale;
			const auto rect = QRectF(
				position - QPointF(size, size),
				QSizeF(size, size) * 2);
			_svg.render(&p, rect);
		}
	}

	p.setCompositionMode(QPainter::CompositionMode_SourceIn);
	auto gradient = QRadialGradient(
		QRect(QPoint(), clip).center(),
		clip.height() / 2);
	gradient.setStops({
		{ 0., _centerColor },
		{ 1., _edgeColor },
	});
	p.setBrush(gradient);
	p.setPen(Qt::NoPen);
	p.drawRect(QRect(QPoint(), clip));
}

void CollectibleEmoji::paint(QPainter &p, const Context &context) {
	prepareFrame();
	p.drawImage(context.position, _frame);
	if (context.paused) {
		_animation.stop();
	} else if (!_animation.animating()) {
		_animation.start();
	}
	_inner->paint(p, context);
}

void CollectibleEmoji::unload() {
	_inner->unload();
}

bool CollectibleEmoji::ready() {
	return _inner->ready();
}

bool CollectibleEmoji::readyInDefaultState() {
	return _inner->readyInDefaultState();
}

} // namespace

ColoredMiniStars::ColoredMiniStars(
	not_null<Ui::RpWidget*> parent,
	bool optimizeUpdate,
	MiniStars::Type type)
: _ministars(
	optimizeUpdate
		? Fn<void(const QRect &)>([=](const QRect &r) {
			parent->update(r.translated(_position));
		})
		: Fn<void(const QRect &)>([=](const QRect &) { parent->update(); }),
	true,
	type) {
}

ColoredMiniStars::ColoredMiniStars(
	Fn<void(const QRect &)> update,
	MiniStars::Type type)
: _ministars(update, true, type) {
}

void ColoredMiniStars::setSize(const QSize &size) {
	_frame = QImage(
		size * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	_frame.setDevicePixelRatio(style::DevicePixelRatio());

	_mask = _frame;
	_mask.fill(Qt::transparent);
	{
		auto p = QPainter(&_mask);
		if (_stopsOverride && _stopsOverride->size() == 1) {
			const auto &color = _stopsOverride->front().second;
			p.fillRect(0, 0, size.width(), size.height(), color);
		} else {
			auto gradient = QLinearGradient(0, 0, size.width(), 0);
			gradient.setStops((_stopsOverride && _stopsOverride->size() > 1)
				? (*_stopsOverride)
				: Ui::Premium::GiftGradientStops());
			p.setPen(Qt::NoPen);
			p.setBrush(gradient);
			p.drawRect(0, 0, size.width(), size.height());
		}
	}

	_size = size;

	{
		const auto s = _size / Ui::Premium::MiniStars::kSizeFactor;
		const auto margins = QMarginsF(
			s.width() / 2.,
			s.height() / 2.,
			s.width() / 2.,
			s.height() / 2.);
		_ministarsRect = QRectF(QPointF(), _size) - margins;
	}
}

void ColoredMiniStars::setPosition(QPoint position) {
	_position = std::move(position);
}

void ColoredMiniStars::setColorOverride(std::optional<QGradientStops> stops) {
	_stopsOverride = stops;
}

void ColoredMiniStars::paint(QPainter &p) {
	_frame.fill(Qt::transparent);
	{
		auto q = QPainter(&_frame);
		_ministars.paint(q, _ministarsRect);
		q.setCompositionMode(QPainter::CompositionMode_SourceIn);
		q.drawImage(0, 0, _mask);
	}

	p.drawImage(_position, _frame);
}

void ColoredMiniStars::setPaused(bool paused) {
	_ministars.setPaused(paused);
}

void ColoredMiniStars::setCenter(const QRect &rect) {
	const auto center = rect.center();
	const auto size = QSize(
		rect.width() * Ui::Premium::MiniStars::kSizeFactor,
		rect.height());
	const auto ministarsRect = QRect(
		QPoint(center.x() - size.width(), center.y() - size.height()),
		QPoint(center.x() + size.width(), center.y() + size.height()));
	setPosition(ministarsRect.topLeft());
	setSize(ministarsRect.size());
}

std::unique_ptr<Text::CustomEmoji> MakeCollectibleEmoji(
		QStringView entityData,
		QColor centerColor,
		QColor edgeColor,
		std::unique_ptr<Text::CustomEmoji> inner,
		Fn<void()> update,
		int size) {
	return std::make_unique<CollectibleEmoji>(
		entityData,
		centerColor,
		edgeColor,
		std::move(inner),
		std::move(update),
		size);
}

} // namespace Ui::Premium
