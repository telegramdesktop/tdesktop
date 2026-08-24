/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/premium_promo_particles.h"

#include "ui/effects/animation_value.h"
#include "ui/effects/drifting_particles.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "styles/style_premium.h"

#include <QtCore/QtMath>
#include <QtSvg/QSvgRenderer>

namespace Ui::Premium {
namespace {

constexpr auto kBrightAlpha = 90;
constexpr auto kIconAlpha = 30;
constexpr auto kBrightParticleAlpha = 255;
constexpr auto kIconParticleAlpha = 120;

constexpr auto kMinLifeTime = crl::time(2000);
constexpr auto kRandLifeTime = 1000;
constexpr auto kSpeedDivider = 660.;
constexpr auto kSpeedBase = 4.;
constexpr auto kMinDelta = crl::time(4);
constexpr auto kMaxDelta = crl::time(50);
constexpr auto kDistributionTries = 10;

constexpr auto kIconParticlesCount = 40;
constexpr auto kStarParticlesCount = 400;
constexpr auto kSpeedLinesCount = 200;
constexpr auto kHelloWordsCount = 25;

constexpr auto kStarsRectFactor = 0.4;
constexpr auto kStarsCornerFactor = 0.98;
constexpr auto kStarsSpeedScale = 4.;
constexpr auto kStarsBlurRadius = 2;

constexpr auto kMatrixGlyphsCount = 16;
constexpr auto kMatrixStepTime = crl::time(50);
constexpr auto kMatrixMinLength = 4;
constexpr auto kMatrixRandLength = 6;
constexpr auto kMatrixSpawnChance = 4;
constexpr auto kMatrixMinAlpha = 0.2;
constexpr auto kMatrixSwapTime = crl::time(150);
constexpr auto kMatrixRandSwapTime = 300;

constexpr auto kSpeedLineAlpha = 80;
constexpr auto kSpeedLineScale = 150.;
constexpr auto kSpeedLineMinScale = 0.02;

constexpr auto kHelloMinDuration = crl::time(2250);
constexpr auto kHelloRandDuration = 2250;
constexpr auto kHelloMinScale = 0.6;
constexpr auto kHelloRandScale = 0.45;
constexpr auto kHelloLongScale = 0.6;
constexpr auto kHelloMediumScale = 0.75;
constexpr auto kHelloLongLength = 7;
constexpr auto kHelloMediumLength = 5;
constexpr auto kVerticalFactor = 0.1;
constexpr auto kHelloOversample = 2;

constexpr auto kSpeedTimestamps = std::array{
	kSpeedLineMinScale, 1., 1., 1., 1., 1., 1., 1., 1., kSpeedLineMinScale,
};

[[nodiscard]] const std::vector<QString> &HelloWords() {
	static const auto result = std::vector<QString>{
		u"Hello"_q, u"Привіт"_q, u"Привет"_q, u"Bonjour"_q, u"Hola"_q,
		u"Ciao"_q, u"Olá"_q, u"여보세요"_q, u"你好"_q, u"Salve"_q,
		u"Sveiki"_q, u"Halo"_q, u"გამარჯობა"_q, u"Hallå"_q, u"Salam"_q,
		u"Tere"_q, u"Dia dhuit"_q, u"こんにちは"_q, u"Сайн уу"_q,
		u"Bongu"_q, u"Ahoj"_q, u"γεια"_q, u"Zdravo"_q, u"नमस्ते"_q,
		u"Habari"_q, u"Hallo"_q, u"ជំរាបសួរ"_q, u"مرحبًا"_q, u"ನಮಸ್ಕಾರ"_q,
		u"Салам"_q, u"Silav li wir"_q, u"سڵاو"_q, u"Kif inti"_q,
		u"Talofa"_q, u"Thobela"_q, u"हॅलो"_q, u"ሰላም"_q, u"Здраво"_q,
		u"ഹലോ"_q, u"ہیلو"_q, u"ꯍꯦꯜꯂꯣ"_q, u"Alô"_q, u"வணக்கம்"_q,
		u"Mhoro"_q, u"Moni"_q, u"Alo"_q, u"สวัสดี"_q, u"Salom"_q,
		u"Բարեւ"_q,
	};
	return result;
}

[[nodiscard]] QImage CreateFrame(int size) {
	const auto ratio = style::DevicePixelRatio();
	auto result = QImage(
		Size(size) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	result.fill(Qt::transparent);
	return result;
}

[[nodiscard]] QImage RenderIconSprite(
		const QString &path,
		int size,
		QColor color) {
	auto result = CreateFrame(size);
	const auto full = Rect(Size(float64(size)));
	auto renderer = QSvgRenderer(path);
	auto p = QPainter(&result);
	renderer.render(&p, full);
	p.setCompositionMode(QPainter::CompositionMode_SourceIn);
	p.fillRect(full, color);
	p.end();
	return result;
}

struct SpriteConfig {
	QString path;
	int size = 0;
	bool randomRotate = false;
	bool bright = false;
};

[[nodiscard]] DriftingSprite MakeSprite(
		const SpriteConfig &sprite,
		float64 corner,
		bool roundEffect,
		bool blur) {
	const auto color = anim::with_alpha(
		Qt::white,
		(sprite.bright ? kBrightAlpha : kIconAlpha) / 255.);
	return {
		.image = (sprite.path.isEmpty()
			? FourPointStarImage({
				.size = sprite.size,
				.corner = corner,
				.cornerRadius = roundEffect ? (sprite.size / 5.) : 0.,
				.blurRadius = blur ? kStarsBlurRadius : 0,
				.color = color,
			})
			: RenderIconSprite(sprite.path, sprite.size, color)),
		.maxAlpha = (sprite.bright
			? kBrightParticleAlpha
			: kIconParticleAlpha),
		.randomRotate = sprite.randomRotate,
	};
}

class StarParticles final : public PromoParticlesPainter {
public:
	struct Config {
		std::vector<SpriteConfig> sprites;
		int count = kIconParticlesCount;
		float64 corner = kStarsCornerFactor;
		float64 speedScale = kStarsSpeedScale;
		bool orbit = false;
		bool blur = false;
		bool roundEffect = true;
		bool checkBounds = false;
		bool aroundDevice = true;
	};

	explicit StarParticles(Config config);

	void setGeometry(QRect outer, QRect device) override;
	void paint(QPainter &p) override;

private:
	const bool _aroundDevice = false;

	DriftingParticles _particles;

};

StarParticles::StarParticles(Config config)
: _aroundDevice(config.aroundDevice)
, _particles([&] {
	auto sprites = std::vector<DriftingSprite>();
	sprites.reserve(config.sprites.size());
	for (const auto &sprite : config.sprites) {
		sprites.push_back(MakeSprite(
			sprite,
			config.corner,
			config.roundEffect,
			config.blur));
	}
	return DriftingParticles::Config{
		.sprites = std::move(sprites),
		.count = config.count,
		.speed = style::ConvertScaleExact(kSpeedBase) * config.speedScale,
		.orbit = config.orbit,
		.checkBounds = config.checkBounds,
		.spawnInCircle = config.aroundDevice,
		.spread = !config.aroundDevice,
	};
}()) {
}

void StarParticles::setGeometry(QRect outer, QRect device) {
	const auto field = [&] {
		if (!_aroundDevice) {
			const auto inset = float64(st::premiumPromoParticlesInset);
			return QRectF(outer).marginsRemoved(Margins(inset));
		}
		const auto half = base::SafeRound(device.width() * kStarsRectFactor);
		const auto center = rect::center(device);
		return Rect(center.x() - half, center.y() - half, Size(half * 2.));
	}();
	const auto skip = float64(st::premiumPromoParticlesDeviceSkip);
	_particles.setGeometry(
		field,
		outer,
		QRectF(device).marginsRemoved(Margins(skip)));
}

void StarParticles::paint(QPainter &p) {
	_particles.paint(p);
}


class MatrixParticles final : public PromoParticlesPainter {
public:
	MatrixParticles();

	void setGeometry(QRect outer, QRect device) override;
	void paint(QPainter &p) override;

private:
	struct Drop {
		int y = 0;
		int length = 0;
		crl::time time = 0;
	};
	struct Glyph {
		int index = 0;
		int nextIndex = 0;
		crl::time nextTime = 0;
	};

	void resetDrop(Drop &drop, crl::time now);
	void resetGlyph(Glyph &glyph, crl::time now);
	void paintGlyph(
		QPainter &p,
		Glyph &glyph,
		QPointF position,
		crl::time now,
		float64 alpha);

	const int _cell = 0;

	std::vector<QImage> _glyphs;
	std::vector<std::vector<Drop>> _columns;
	std::vector<std::vector<Glyph>> _cells;

	QRect _outer;
	QRectF _exclude;

	ParticlesRandom _random;

};

MatrixParticles::MatrixParticles()
: _cell(st::premiumPromoMatrixCell) {
	const auto font = style::font(
		_cell,
		style::FontFlag::Bold,
		st::normalFont->family());
	_glyphs.reserve(kMatrixGlyphsCount);
	for (auto i = 0; i != kMatrixGlyphsCount; ++i) {
		const auto text = QString(
			QChar((i < 10) ? ('0' + i) : ('A' + (i - 10))));
		auto image = CreateFrame(_cell);
		auto p = QPainter(&image);
		p.setFont(font);
		p.setPen(anim::with_alpha(Qt::white, kIconAlpha / 255.));
		p.drawText(
			QPointF((_cell - font->width(text)) / 2., _cell),
			text);
		p.end();
		_glyphs.push_back(std::move(image));
	}
}

void MatrixParticles::setGeometry(QRect outer, QRect device) {
	_outer = outer;
	const auto skip = float64(st::premiumPromoMatrixCell);
	_exclude = QRectF(device).marginsRemoved(Margins(skip));

	const auto now = crl::now();
	const auto nx = (_cell > 0) ? (outer.width() / _cell) : 0;
	const auto ny = (_cell > 0) ? (outer.height() / _cell) : 0;
	if (nx <= 0 || ny <= 0) {
		_columns.clear();
		_cells.clear();
		return;
	}
	_columns.assign(nx + 1, std::vector<Drop>());
	_cells.assign(nx + 1, std::vector<Glyph>(ny + 1));
	for (auto x = 0; x != nx + 1; ++x) {
		auto drop = Drop();
		resetDrop(drop, now);
		drop.y = _random.index(ny);
		_columns[x].push_back(drop);
		for (auto &glyph : _cells[x]) {
			resetGlyph(glyph, now);
			glyph.index = _random.index(kMatrixGlyphsCount);
		}
	}
}

void MatrixParticles::resetDrop(Drop &drop, crl::time now) {
	drop.y = 0;
	drop.time = now;
	drop.length = kMatrixMinLength + _random.index(kMatrixRandLength);
}

void MatrixParticles::resetGlyph(Glyph &glyph, crl::time now) {
	glyph.index = glyph.nextIndex;
	glyph.nextIndex = _random.index(kMatrixGlyphsCount);
	glyph.nextTime = now + kMatrixSwapTime + _random.index(
		kMatrixRandSwapTime);
}

void MatrixParticles::paintGlyph(
		QPainter &p,
		Glyph &glyph,
		QPointF position,
		crl::time now,
		float64 alpha) {
	const auto left = glyph.nextTime - now;
	if (left >= kMatrixSwapTime) {
		p.setOpacity(alpha);
		p.drawImage(position, _glyphs[glyph.index]);
		return;
	}
	const auto progress = std::clamp(
		1. - (left / float64(kMatrixSwapTime)),
		0.,
		1.);
	p.setOpacity(alpha * (1. - progress));
	p.drawImage(position, _glyphs[glyph.index]);
	p.setOpacity(alpha * progress);
	p.drawImage(position, _glyphs[glyph.nextIndex]);
	if (progress >= 1.) {
		resetGlyph(glyph, now);
	}
}

void MatrixParticles::paint(QPainter &p) {
	if (_columns.empty()) {
		return;
	}
	const auto now = crl::now();
	const auto nx = int(_columns.size()) - 1;
	const auto ny = int(_cells.front().size()) - 1;
	const auto opacity = p.opacity();
	for (auto x = 0; x != nx + 1; ++x) {
		auto &drops = _columns[x];
		for (auto i = 0; i != int(drops.size()); ++i) {
			auto &drop = drops[i];
			if (now - drop.time > kMatrixStepTime) {
				++drop.y;
				drop.time = now;
				if (drop.y - drop.length >= ny) {
					if (drops.size() == 1) {
						resetDrop(drop, now);
					} else {
						drops.erase(drops.begin() + i);
						--i;
						continue;
					}
				}
				if (drop.y > drop.length
					&& i == int(drops.size()) - 1
					&& _random.chance(kMatrixSpawnChance)) {
					auto added = Drop();
					resetDrop(added, now);

					drops.push_back(added);
				}
			}
			const auto bottom = drops[i].y;
			const auto length = drops[i].length;
			const auto till = std::min(bottom, ny + 1);
			for (auto y = std::max(0, bottom - length); y < till; ++y) {
				const auto position = QPointF(_cell * x, _cell * y);
				if (_exclude.contains(position)) {
					continue;
				}
				const auto alpha = std::clamp(
					kMatrixMinAlpha
						+ (1. - kMatrixMinAlpha)
							* (1. - (bottom - y) / float64(length - 1)),
					0.,
					1.);
				paintGlyph(p, _cells[x][y], position, now, opacity * alpha);
			}
		}
	}
	p.setOpacity(opacity);
}

class SpeedLineParticles final : public PromoParticlesPainter {
public:
	SpeedLineParticles() = default;

	void setGeometry(QRect outer, QRect device) override;
	void setVideoProgress(float64 progress) override;
	void paint(QPainter &p) override;

private:
	struct Particle {
		crl::time lifeTime = 0;
		float64 x = 0.;
		float64 y = 0.;
		float64 vecX = 0.;
		float64 vecY = 0.;
	};

	void generate(Particle &particle, crl::time now, bool reset);

	std::vector<Particle> _particles = std::vector<Particle>(
		kSpeedLinesCount);
	std::vector<QLineF> _lines;

	QRectF _rect;
	QRectF _screen;
	crl::time _lastTime = 0;
	float64 _speedScale = kSpeedLineScale;

	ParticlesRandom _random;

};

void SpeedLineParticles::setGeometry(QRect outer, QRect device) {
	const auto inset = float64(st::premiumPromoSpeedInset);
	_screen = outer;
	_rect = QRectF(outer).marginsRemoved(
		Margins(inset)
	).translated(0, outer.height() * kVerticalFactor);

	const auto now = crl::now();
	for (auto &particle : _particles) {
		generate(particle, now, true);
	}
}

void SpeedLineParticles::setVideoProgress(float64 progress) {
	const auto count = int(kSpeedTimestamps.size());
	const auto step = 1. / (count - 1);
	const auto clamped = std::clamp(progress, 0., 1.);
	const auto from = std::min(int(clamped / step), count - 1);
	const auto till = from + 1;
	const auto inner = (clamped - from * step) / step;
	const auto video = (till < count)
		? (kSpeedTimestamps[from] * (1. - inner)
			+ kSpeedTimestamps[till] * inner)
		: kSpeedTimestamps[from];
	_speedScale = kSpeedLineScale * video;
}

void SpeedLineParticles::generate(
		Particle &particle,
		crl::time now,
		bool reset) {
	particle.lifeTime = now + kMinLifeTime + _random.index(kRandLifeTime);
	const auto &from = reset ? _screen : _rect;
	particle.x = _random.value(from.left(), from.right());
	particle.y = _random.value(from.top(), from.bottom());

	const auto center = rect::center(_rect);
	const auto angle = std::atan2(
		particle.x - center.x(),
		particle.y - center.y());
	particle.vecX = std::sin(angle);
	particle.vecY = std::cos(angle);
}

void SpeedLineParticles::paint(QPainter &p) {
	if (_rect.isEmpty()) {
		return;
	}
	const auto now = crl::now();
	const auto delta = float64(
		std::clamp(now - _lastTime, kMinDelta, kMaxDelta));
	_lastTime = now;

	const auto speed = style::ConvertScaleExact(kSpeedBase)
		* (delta / kSpeedDivider)
		* _speedScale;
	const auto length = float64(st::premiumPromoSpeedLine);
	_lines.clear();
	_lines.reserve(_particles.size());
	for (auto &particle : _particles) {
		_lines.push_back(QLineF(
			particle.x,
			particle.y,
			particle.x + length * particle.vecX,
			particle.y + length * particle.vecY));
		particle.x += particle.vecX * speed;
		particle.y += particle.vecY * speed;
		if (now > particle.lifeTime
			|| !_screen.contains(particle.x, particle.y)) {
			generate(particle, now, false);
		}
	}
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(anim::with_alpha(Qt::white, kSpeedLineAlpha / 255.));
	p.drawLines(_lines.data(), int(_lines.size()));
}

class HelloParticles final : public PromoParticlesPainter {
public:
	HelloParticles() = default;

	void setGeometry(QRect outer, QRect device) override;
	void paint(QPainter &p) override;

private:
	struct Particle {
		QString word;
		float64 x = 0.;
		float64 y = 0.;
		float64 scale = 1.;
		crl::time bornTime = 0;
		crl::time duration = 0;
		int alpha = 0;
		bool set = false;
	};

	void generate(Particle &particle, int index, crl::time now, bool reset);
	[[nodiscard]] const QImage &lookupWord(const QString &word);
	[[nodiscard]] QSizeF wordSize(const QString &word);

	std::vector<Particle> _particles = std::vector<Particle>(
		kHelloWordsCount);
	base::flat_map<QString, QImage> _words;

	QRectF _rect;

	ParticlesRandom _random;

};

const QImage &HelloParticles::lookupWord(const QString &word) {
	const auto i = _words.find(word);
	if (i != end(_words)) {
		return i->second;
	}
	const auto &base = st::premiumPromoHelloFont;
	const auto font = style::font(
		base->size() * kHelloOversample,
		base->flags(),
		base->family());
	const auto width = std::max(font->width(word), 1);
	const auto height = std::max(font->height, 1);
	auto image = QImage(
		QSize(width, height) * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(style::DevicePixelRatio() * kHelloOversample);
	image.fill(Qt::transparent);
	auto p = QPainter(&image);
	auto hq = PainterHighQualityEnabler(p);

	p.scale(1. / kHelloOversample, 1. / kHelloOversample);
	p.setFont(font);
	p.setPen(Qt::white);
	p.drawText(QPointF(0, font->ascent), word);
	p.end();
	return _words.emplace(word, std::move(image)).first->second;
}

QSizeF HelloParticles::wordSize(const QString &word) {
	const auto &image = lookupWord(word);
	return QSizeF(image.size()) / image.devicePixelRatio();
}

void HelloParticles::setGeometry(QRect outer, QRect device) {
	const auto inset = outer.height() * kVerticalFactor;
	_rect = QRectF(outer).marginsRemoved(QMarginsF(0, inset, 0, inset));

	const auto now = crl::now();
	auto index = 0;
	for (auto &particle : _particles) {
		generate(particle, index++, now, true);
	}
}

void HelloParticles::generate(
		Particle &particle,
		int index,
		crl::time now,
		bool reset) {
	const auto &words = HelloWords();
	particle.word = words[_random.index(int(words.size()))];
	particle.duration = kHelloMinDuration + _random.index(
		kHelloRandDuration);
	particle.scale = _random.value(
		kHelloMinScale,
		kHelloMinScale + kHelloRandScale);
	if (particle.word.size() > kHelloLongLength) {
		particle.scale *= kHelloLongScale;
	} else if (particle.word.size() > kHelloMediumLength) {
		particle.scale *= kHelloMediumScale;
	}
	const auto size = wordSize(particle.word);

	const auto center = rect::center(_rect);
	auto minX = _rect.left() + size.width() / 4.;
	auto maxX = rect::right(_rect) - size.width() / 4.;
	if (!(index % 2)) {
		maxX = center.x() - size.width() / 2.;
	} else {
		minX = center.x() + size.width() / 2.;
	}
	if (maxX <= minX) {
		maxX = minX + 1.;
	}
	auto bestDistance = 0.;
	auto best = QPointF(
		_random.value(minX, maxX),
		_random.value(_rect.top(), _rect.bottom()));
	for (auto i = 0; i != kDistributionTries; ++i) {
		const auto candidate = QPointF(
			_random.value(minX, maxX),
			_random.value(_rect.top(), _rect.bottom()));
		auto minDistance = std::numeric_limits<float64>::max();
		for (const auto &other : _particles) {
			if (!other.set) {
				continue;
			}
			const auto width = wordSize(other.word).width() * other.scale;
			const auto dx = std::min(
				std::abs(other.x + width * 1.1 - candidate.x()),
				std::abs(other.x - candidate.x()));
			const auto dy = other.y - candidate.y();
			minDistance = std::min(minDistance, dx * dx + dy * dy);
		}
		if (minDistance > bestDistance) {
			bestDistance = minDistance;
			best = candidate;
		}
	}
	particle.x = best.x();
	particle.y = best.y();
	particle.alpha = int(base::SafeRound(
		_random.value(0.5, 1.) * kBrightParticleAlpha));
	particle.bornTime = now - (reset
		? crl::time(_random.value() * 0.9 * particle.duration)
		: 0);
	particle.set = true;
}

void HelloParticles::paint(QPainter &p) {
	if (_rect.isEmpty()) {
		return;
	}
	const auto now = crl::now();
	auto hq = PainterHighQualityEnabler(p);
	const auto opacity = p.opacity();
	auto index = 0;
	for (auto &particle : _particles) {
		const auto &image = lookupWord(particle.word);
		const auto size = QSizeF(image.size()) / image.devicePixelRatio();
		const auto progress = std::clamp(
			(now - particle.bornTime) / float64(particle.duration),
			0.,
			1.);
		const auto shift = progress - 0.5;
		const auto wave = 1. - 4. * shift * shift;
		const auto scale = particle.scale * (0.7 + 0.4 * wave);
		p.save();
		p.translate(particle.x, particle.y);
		p.scale(scale, scale);
		p.setOpacity(opacity * (particle.alpha / 255.) * wave);
		p.drawImage(
			QRectF(
				-size.width() / 2.,
				-size.height() / 2.,
				size.width(),
				size.height()),
			image);
		p.restore();

		if (progress >= 1.) {
			generate(particle, index, now, false);
		}
		++index;
	}
	p.setOpacity(opacity);
}

[[nodiscard]] std::vector<SpriteConfig> IconSprites(
		std::vector<QString> names,
		std::vector<int> sizes,
		bool randomRotate) {
	Expects(names.size() == sizes.size());

	auto result = std::vector<SpriteConfig>();
	result.reserve(names.size());
	for (auto i = 0; i != int(names.size()); ++i) {
		result.push_back({
			.path = u":/gui/icons/premium/%1.svg"_q.arg(names[i]),
			.size = sizes[i],
			.randomRotate = randomRotate,
		});
	}
	return result;
}

[[nodiscard]] std::vector<int> DefaultIconSizes() {
	return {
		st::premiumPromoIconSmall,
		st::premiumPromoIconLarge,
		st::premiumPromoIconMedium,
	};
}

[[nodiscard]] std::vector<int> WideIconSizes() {
	return {
		st::premiumPromoIconSmall,
		st::premiumPromoIconWide,
		st::premiumPromoIconWide,
	};
}

[[nodiscard]] std::unique_ptr<PromoParticlesPainter> MakeIcons(
		std::vector<QString> names,
		std::vector<int> sizes,
		bool randomRotate = true,
		bool orbit = false) {
	return std::make_unique<StarParticles>(StarParticles::Config{
		.sprites = IconSprites(
			std::move(names),
			std::move(sizes),
			randomRotate),
		.orbit = orbit,
		.aroundDevice = false,
	});
}

[[nodiscard]] std::unique_ptr<PromoParticlesPainter> MakeStars() {
	return std::make_unique<StarParticles>(StarParticles::Config{
		.sprites = {
			{ .size = st::premiumPromoStarSmall, .bright = true },
			{ .size = st::premiumPromoStarLarge, .bright = true },
			{ .size = st::premiumPromoStarMedium, .bright = true },
		},
		.count = kStarParticlesCount,
		.orbit = true,
		.blur = true,
		.roundEffect = false,
		.checkBounds = true,
	});
}

[[nodiscard]] std::unique_ptr<PromoParticlesPainter> MakeProfileBadge() {
	const auto star = u":/gui/icons/settings/star.svg"_q;
	return std::make_unique<StarParticles>(StarParticles::Config{
		.sprites = {
			{ .size = st::premiumPromoIconSmall, .bright = true },
			{
				.path = star,
				.size = st::premiumPromoIconLarge,
				.randomRotate = true,
				.bright = true,
			},
			{
				.path = star,
				.size = st::premiumPromoIconMedium,
				.randomRotate = true,
				.bright = true,
			},
		},
		.aroundDevice = false,
	});
}

} // namespace

std::unique_ptr<PromoParticlesPainter> MakePromoParticles(
		PromoParticles type) {
	switch (type) {
	case PromoParticles::Stars:
		return MakeStars();
	case PromoParticles::Matrix:
		return std::make_unique<MatrixParticles>();
	case PromoParticles::SpeedLines:
		return std::make_unique<SpeedLineParticles>();
	case PromoParticles::Hello:
		return std::make_unique<HelloParticles>();
	case PromoParticles::ChatManagement:
		return MakeIcons(
			{ u"folder"_q, u"bubble"_q, u"settings"_q },
			DefaultIconSizes());
	case PromoParticles::Emoji:
		return MakeIcons(
			{ u"smile1"_q, u"smile2"_q, u"like"_q },
			DefaultIconSizes());
	case PromoParticles::Ads:
		return MakeIcons(
			{ u"adsbubble"_q, u"like"_q, u"noads"_q },
			WideIconSizes());
	case PromoParticles::Userpics:
		return MakeIcons(
			{ u"video2"_q, u"video"_q, u"user"_q },
			DefaultIconSizes());
	case PromoParticles::Tags:
		return MakeIcons(
			{ u"tag"_q, u"check"_q, u"star"_q },
			WideIconSizes());
	case PromoParticles::Formatting:
		return MakeIcons({
			u"list"_q,
			u"math"_q,
			u"table"_q,
			u"superscript"_q,
			u"bold"_q,
			u"code"_q,
		}, {
			st::premiumPromoIconSmall,
			st::premiumPromoIconWide,
			st::premiumPromoIconWide,
			st::premiumPromoIconWide,
			st::premiumPromoIconWide,
			st::premiumPromoIconWide,
		}, false, true);
	case PromoParticles::ProfileBadge:
		return MakeProfileBadge();
	}
	Unexpected("Type in Ui::Premium::MakePromoParticles.");
}

} // namespace Ui::Premium
