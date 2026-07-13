/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/sections/settings_local_storage.h"

#include "settings.h"
#include "settings/settings_common.h"
#include "data/data_session.h"
#include "lottie/lottie_icon.h"
#include "base/call_delayed.h"
#include "base/random.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "statistics/view/stack_linear_chart_common.h"
#include "storage/storage_account.h"
#include "storage/cache/storage_cache_database.h"
#include "ui/text/format_values.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/effects/animated_string.h"
#include "ui/effects/animations.h"
#include "ui/effects/ripple_animation.h"
#include "ui/effects/shake_animation.h"
#include "ui/emoji_config.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/text/text_entity.h"
#include "ui/text/text_utilities.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/tooltip.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"

#include <QtCore/QStorageInfo>

namespace Settings {
namespace {

constexpr auto kMegabyte = int64(1024 * 1024);
constexpr auto kTotalSizeLimitsCount = 18;
constexpr auto kMediaSizeLimitsCount = 18;
constexpr auto kMinimalSizeLimit = 100 * kMegabyte;
constexpr auto kTimeLimitsCount = 16;
constexpr auto kMaxTimeLimitValue = std::numeric_limits<size_type>::max();
constexpr auto kFakeMediaCacheTag = uint16(0xFFFF);
constexpr auto kClearingMinDuration = crl::time(1200);
constexpr auto kCompleteParticlesCount = 25;

constexpr auto kChartTags = std::array<uint16, kChartPartsCount>{ {
	Data::kImageCacheTag,
	Data::kStickerCacheTag,
	Data::kVoiceMessageCacheTag,
	Data::kVideoMessageCacheTag,
	Data::kAnimationCacheTag,
	kFakeMediaCacheTag,
} };

int ChartIndexForTag(uint16 tag) {
	for (auto i = 0; i != kChartPartsCount; ++i) {
		if (kChartTags[i] == tag) {
			return i;
		}
	}
	return -1;
}

int64 TotalSizeLimitInMB(int index) {
	if (index < 8) {
		return int64(index + 2) * 100;
	}
	return int64(index - 7) * 1024;
}

int64 TotalSizeLimit(int index) {
	return TotalSizeLimitInMB(index) * kMegabyte;
}

int64 MediaSizeLimitInMB(int index) {
	if (index < 9) {
		return int64(index + 1) * 100;
	}
	return int64(index - 8) * 1024;
}

int64 MediaSizeLimit(int index) {
	return MediaSizeLimitInMB(index) * kMegabyte;
}

QString SizeLimitText(int64 limit) {
	const auto mb = (limit / (1024 * 1024));
	const auto gb = (mb / 1024);
	return (gb > 0)
		? (QString::number(gb) + " GB")
		: (QString::number(mb) + " MB");
}

size_type TimeLimitInDays(int index) {
	if (index < 3) {
		const auto weeks = (index + 1);
		return size_type(weeks) * 7;
	} else if (index < 15) {
		const auto month = (index - 2);
		return (size_type(month) * 30)
			+ ((month >= 12) ? 5 :
				(month >= 10) ? 4 :
				(month >= 8) ? 3 :
				(month >= 7) ? 2 :
				(month >= 5) ? 1 :
				(month >= 3) ? 0 :
				(month >= 2) ? -1 :
				(month >= 1) ? 1 : 0);
	}
	return 0;
}

size_type TimeLimit(int index) {
	const auto days = TimeLimitInDays(index);
	return days
		? (days * 24 * 60 * 60)
		: kMaxTimeLimitValue;
}

QString TimeLimitText(size_type limit) {
	const auto days = (limit / (24 * 60 * 60));
	const auto weeks = (days / 7);
	const auto months = (days / 29);
	return (months > 0)
		? tr::lng_months(tr::now, lt_count, months)
		: (limit > 0)
		? tr::lng_weeks(tr::now, lt_count, weeks)
		: tr::lng_local_storage_limit_never(tr::now);
}

size_type LimitToValue(size_type timeLimit) {
	return timeLimit ? timeLimit : kMaxTimeLimitValue;
}

size_type ValueToLimit(size_type timeLimit) {
	return (timeLimit != kMaxTimeLimitValue) ? timeLimit : 0;
}

constexpr auto kChartSeparator = 2.;
constexpr auto kChartMinFraction = 0.02;
constexpr auto kChartMorphDuration = crl::time(650);
constexpr auto kChartCompleteDuration = crl::time(650);
constexpr auto kChartAppearDuration = crl::time(750);
constexpr auto kChartSelectDuration = crl::time(200);
constexpr auto kChartPercentFadeDuration = crl::time(200);

QColor PartColor(int index) {
	switch (index) {
	case 0: return st::statisticsChartLineLightblue->c;
	case 1: return st::statisticsChartLineOrange->c;
	case 2: return st::statisticsChartLineLightgreen->c;
	case 3: return st::statisticsChartLinePurple->c;
	case 4: return st::statisticsChartLineGreen->c;
	case 5: return st::statisticsChartLineBlue->c;
	}
	return st::statisticsChartLineCyan->c;
}

QString PartTitle(int index, size_type count) {
	switch (index) {
	case 0: return tr::lng_local_storage_image(tr::now, lt_count, count);
	case 1: return tr::lng_local_storage_sticker(tr::now, lt_count, count);
	case 2: return tr::lng_local_storage_voice(tr::now, lt_count, count);
	case 3: return tr::lng_local_storage_round(tr::now, lt_count, count);
	case 4: return tr::lng_local_storage_animation(tr::now, lt_count, count);
	case 5: return tr::lng_local_storage_media_title(tr::now);
	}
	return QString();
}

QString PartName(int index) {
	switch (index) {
	case 0: return tr::lng_local_storage_image_title(tr::now);
	case 1: return tr::lng_local_storage_sticker_title(tr::now);
	case 2: return tr::lng_local_storage_voice_title(tr::now);
	case 3: return tr::lng_local_storage_round_title(tr::now);
	case 4: return tr::lng_local_storage_animation_title(tr::now);
	case 5: return tr::lng_local_storage_media_title(tr::now);
	}
	return QString();
}

const style::icon &ParticleIcon(int index) {
	switch (index) {
	case 0: return st::localStorageParticlePhotos;
	case 1: return st::localStorageParticleStickers;
	case 2: return st::localStorageParticleMusic;
	case 3: return st::localStorageParticleVideos;
	case 4: return st::localStorageParticleVideos;
	case 5: return st::localStorageParticleDocuments;
	}
	return st::localStorageParticleDocuments;
}

const QImage &ParticleImage(int index) {
	static auto images = std::array<QImage, kChartPartsCount>();
	auto &image = images[index];
	if (image.isNull()) {
		image = ParticleIcon(index).instance(Qt::white);
	}
	return image;
}

not_null<Ui::VerticalLayout*> AddIsland(
		not_null<Ui::VerticalLayout*> parent,
		bool topSkip = true) {
	const auto island = parent->add(
		object_ptr<Ui::VerticalLayout>(parent),
		st::localStorageIslandMargin);
	island->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(island);
		auto hq = PainterHighQualityEnabler(p);
		const auto radius = st::localStorageIslandRadius;
		p.setPen(Qt::NoPen);
		p.setBrush(st::boxBg);
		p.drawRoundedRect(island->rect(), radius, radius);
	}, island->lifetime());
	if (topSkip) {
		Ui::AddSkip(island, st::localStorageIslandPadding);
	}
	return island;
}

not_null<Ui::SlideWrap<Ui::VerticalLayout>*> AddIslandWrap(
		not_null<Ui::VerticalLayout*> parent) {
	const auto wrap = parent->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			parent,
			object_ptr<Ui::VerticalLayout>(parent),
			st::localStorageIslandMargin));
	const auto island = wrap->entity();
	island->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(island);
		auto hq = PainterHighQualityEnabler(p);
		const auto radius = st::localStorageIslandRadius;
		p.setPen(Qt::NoPen);
		p.setBrush(st::boxBg);
		p.drawRoundedRect(island->rect(), radius, radius);
	}, island->lifetime());
	return wrap;
}

[[nodiscard]] QString FormatStoragePercent(int64 part, int64 whole) {
	if (whole <= 0) {
		return u"0%"_q;
	}
	const auto ratio = part / float64(whole);
	if (ratio < 0.001) {
		return u"<0.1%"_q;
	}
	const auto percent = int(base::SafeRound(ratio * 100.));
	return (percent <= 0)
		? u"<1%"_q
		: (QString::number(percent) + '%');
}

[[nodiscard]] QString FormatStorageSize(int64 size) {
	constexpr auto kGigabyte = int64(1024) * 1024 * 1024;
	if (size < kGigabyte) {
		return Ui::FormatSizeText(size);
	}
	const auto tenthGb = size * 10 / kGigabyte;
	return QString::number(tenthGb / 10)
		+ '.'
		+ QString::number(tenthGb % 10)
		+ u" GB"_q;
}

[[nodiscard]] QImage GenerateCompleteStar(int size, float64 k) {
	const auto ratio = style::DevicePixelRatio();
	auto image = QImage(
		size * ratio,
		size * ratio,
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(ratio);
	image.fill(Qt::transparent);

	auto p = QPainter(&image);
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(Qt::white);

	const auto half = size / 2.;
	const auto mid = half * k;
	auto path = QPainterPath();
	path.moveTo(0, half);
	path.lineTo(mid, mid);
	path.lineTo(half, 0);
	path.lineTo(size - mid, mid);
	path.lineTo(size, half);
	path.lineTo(size - mid, size - mid);
	path.lineTo(half, size);
	path.lineTo(mid, size - mid);
	path.closeSubpath();
	p.drawPath(path);

	return image;
}

class CompleteParticles final {
public:
	void paint(
		QPainter &p,
		QRect rect,
		QPointF center,
		float64 alpha,
		crl::time now,
		bool paused);

private:
	struct Particle {
		float64 x = 0.;
		float64 y = 0.;
		float64 vecX = 0.;
		float64 vecY = 0.;
		float64 inProgress = 0.;
		float64 baseAlpha = 0.;
		crl::time lifeTime = 0;
		int starIndex = 0;
	};

	void regenerate(Particle &particle, QPointF center, crl::time now);
	[[nodiscard]] const QImage &star(int index);

	std::vector<Particle> _particles;
	std::array<QImage, 3> _stars;
	QImage _layer;
	std::array<float64, 3> _angle = { { 0., 0., 0. } };
	crl::time _prevTime = 0;
	crl::time _pausedAt = 0;
	crl::time _pauseOffset = 0;

};

const QImage &CompleteParticles::star(int index) {
	auto &image = _stars[index];
	if (image.isNull()) {
		const int sizes[] = {
			st::localStorageCompleteStar1,
			st::localStorageCompleteStar2,
			st::localStorageCompleteStar3,
		};
		image = GenerateCompleteStar(sizes[index], 0.85);
	}
	return image;
}

void CompleteParticles::regenerate(
		Particle &particle,
		QPointF center,
		crl::time now) {
	auto bytes = std::array<uchar, 8>();
	base::RandomFill(bytes.data(), bytes.size());
	auto index = 0;
	const auto next = [&] { return bytes[index++] / 255.; };

	particle.starIndex = std::min(2, int(next() * 3.));
	particle.lifeTime = now + 2000 + crl::time(next() * 1000.);

	const auto field = float64(st::localStorageCompleteField);
	const auto exclude = float64(st::localStorageCompleteExclude);
	const auto radius = next() * (field - exclude) + exclude;
	const auto angle = next() * 2. * M_PI;
	particle.x = center.x() + radius * std::sin(angle);
	particle.y = center.y() + radius * std::cos(angle);

	const auto direction = std::atan2(
		particle.y - center.y(),
		particle.x - center.x());
	particle.vecX = std::cos(direction);
	particle.vecY = std::sin(direction);
	particle.baseAlpha = (50 + int(next() * 50.)) / 100.;
	particle.inProgress = 0.;
}

void CompleteParticles::paint(
		QPainter &p,
		QRect rect,
		QPointF center,
		float64 alpha,
		crl::time now,
		bool paused) {
	if (alpha <= 0.) {
		return;
	} else if (_particles.empty()) {
		_particles.resize(kCompleteParticlesCount);
	}

	if (paused) {
		if (!_pausedAt) {
			_pausedAt = now;
		}
		now = _pausedAt - _pauseOffset;
	} else {
		if (_pausedAt) {
			_pauseOffset += now - _pausedAt;
			_pausedAt = 0;
		}
		now -= _pauseOffset;
	}

	if (!_prevTime) {
		_prevTime = now;
		for (auto &particle : _particles) {
			regenerate(particle, center, now);
		}
	}
	const auto diff = std::clamp(
		now - _prevTime,
		crl::time(4),
		crl::time(50));
	_prevTime = now;

	if (!paused) {
		_angle[0] += 360. * (diff / 40000.);
		_angle[1] += 360. * (diff / 50000.);
		_angle[2] += 360. * (diff / 60000.);
	}

	const auto ratio = style::DevicePixelRatio();
	const auto pixelSize = rect.size() * ratio;
	if (_layer.size() != pixelSize) {
		_layer = QImage(pixelSize, QImage::Format_ARGB32_Premultiplied);
		_layer.setDevicePixelRatio(ratio);
	}
	_layer.fill(Qt::transparent);

	auto lp = QPainter(&_layer);
	auto hq = PainterHighQualityEnabler(lp);
	const auto speed = st::localStorageCompleteSpeed * (diff / 660.);
	for (auto &particle : _particles) {
		const auto radians = _angle[particle.starIndex] * M_PI / 180.;
		const auto cosA = std::cos(radians);
		const auto sinA = std::sin(radians);
		const auto dx = particle.x - center.x();
		const auto dy = particle.y - center.y();
		const auto drawX = center.x() + dx * cosA - dy * sinA;
		const auto drawY = center.y() + dx * sinA + dy * cosA;

		auto outProgress = 0.;
		if (particle.lifeTime - now < 200) {
			outProgress = std::clamp(
				1. - (particle.lifeTime - now) / 150.,
				0.,
				1.);
		}
		const auto t = particle.inProgress - 1.;
		const auto scale = t * t * (3. * t + 2.) + 1.;
		const auto &image = star(particle.starIndex);
		const auto side = image.width() / float64(ratio);

		lp.save();
		lp.setOpacity(particle.baseAlpha * (1. - outProgress));
		lp.translate(drawX - rect.x(), drawY - rect.y());
		lp.scale(scale, scale);
		lp.drawImage(QPointF(-side / 2., -side / 2.), image);
		lp.restore();

		if (!paused) {
			particle.x += particle.vecX * speed;
			particle.y += particle.vecY * speed;
			if (particle.inProgress < 1.) {
				particle.inProgress = std::min(
					particle.inProgress + diff / 200.,
					1.);
			}
		}
		if (now > particle.lifeTime
			|| !rect.contains(QPoint(int(drawX), int(drawY)))) {
			regenerate(particle, center, now);
		}
	}
	lp.setOpacity(1.);
	lp.setCompositionMode(QPainter::CompositionMode_SourceIn);
	auto gradient = QLinearGradient(
		QPointF(0, 0),
		QPointF(0, rect.height()));
	gradient.setColorAt(0., QColor(0x6e, 0xd5, 0x56, 0));
	gradient.setColorAt(0.07, QColor(0x6e, 0xd5, 0x56));
	gradient.setColorAt(0.93, QColor(0x41, 0xba, 0x71));
	gradient.setColorAt(1., QColor(0x41, 0xba, 0x71, 0));
	lp.fillRect(QRect(QPoint(), rect.size()), gradient);
	lp.end();

	p.setOpacity(alpha);
	p.drawImage(rect.topLeft(), _layer);
	p.setOpacity(1.);
}

} // namespace

class LocalStorage::Chart final : public Ui::RpWidget {
public:
	Chart(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	void setParts(std::array<int64, kChartPartsCount> sizes, int64 total);
	void setPartInfo(Fn<TextWithEntities(int)> info);
	void setLoaded();
	[[nodiscard]] int partPercent(int index) const {
		return _percent[index];
	}

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	struct Geometry {
		float64 center = 0.;
		float64 half = 0.;
	};

	void computeTargets();
	[[nodiscard]] bool paused() const;
	[[nodiscard]] std::array<Geometry, kChartPartsCount> shown() const;
	void setSelected(int index);
	[[nodiscard]] int segmentAt(QPointF pos) const;
	[[nodiscard]] QPointF partCenter(int index) const;
	void showTooltip(int index);
	void hideTooltip();
	[[nodiscard]] QPointF radialPoint(
		QPointF center,
		float64 radius,
		float64 angle) const;
	void drawParticles(
		QPainter &p,
		QPointF center,
		int index,
		float64 from,
		float64 to,
		float64 innerRadius,
		float64 outerRadius,
		float64 alpha);

	std::array<int64, kChartPartsCount> _sizes = { { 0 } };
	std::array<Geometry, kChartPartsCount> _from;
	std::array<Geometry, kChartPartsCount> _to;
	std::array<int, kChartPartsCount> _percent = { { 0 } };
	std::array<bool, kChartPartsCount> _showPercent = { { false } };
	std::array<float64, kChartPartsCount> _percentTarget = { { 0. } };
	std::array<Ui::Animations::Simple, kChartPartsCount> _percentAlpha;
	int64 _total = 0;

	Ui::Animations::Simple _morph;
	Ui::Animations::Simple _appear;
	bool _appeared = false;
	bool _loaded = false;
	bool _complete = false;
	Ui::Animations::Simple _completeAnimation;
	CompleteParticles _completeParticles;

	int _selected = -1;
	std::array<Ui::Animations::Simple, kChartPartsCount> _selectAnimations;

	const not_null<Window::SessionController*> _controller;
	Ui::Animations::Basic _particles;
	crl::time _particlesStart = 0;
	crl::time _particlesPausedAt = 0;
	bool _shown = false;

	Fn<TextWithEntities(int)> _partInfo;
	std::unique_ptr<Ui::ImportantTooltip> _tooltip;
	int _tooltipPart = -1;

};

LocalStorage::Chart::Chart(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _controller(controller) {
	setMouseTracking(true);

	_particlesStart = crl::now();
	_particles.init([=](crl::time) {
		update();
		return true;
	});
	const auto refresh = [=] {
		if (_shown && !paused()) {
			if (_particlesPausedAt) {
				_particlesStart += crl::now() - _particlesPausedAt;
				_particlesPausedAt = 0;
			}
			if (!_particles.animating()) {
				_particles.start();
			}
		} else {
			if (!_particlesPausedAt) {
				_particlesPausedAt = crl::now();
			}
			_particles.stop();
		}
	};
	shownValue() | rpl::on_next([=](bool shown) {
		_shown = shown;
		refresh();
	}, lifetime());
	_controller->gifPauseLevelChanged(
	) | rpl::on_next(refresh, lifetime());
}

bool LocalStorage::Chart::paused() const {
	return anim::Disabled()
		|| _controller->isGifPausedAtLeastFor(Window::GifPauseReason::Layer);
}

void LocalStorage::Chart::drawParticles(
		QPainter &p,
		QPointF center,
		int index,
		float64 from,
		float64 to,
		float64 innerRadius,
		float64 outerRadius,
		float64 alpha) {
	if (alpha <= 0. || anim::Disabled()) {
		return;
	}
	const auto &image = ParticleImage(index);
	const auto size = float64(st::localStorageChartParticle);
	const auto sqrt2 = std::sqrt(2.);
	const auto reference = _particlesPausedAt ? _particlesPausedAt : crl::now();
	const auto time = (reference - _particlesStart) / 10000.;
	const auto step = 7.;
	const auto fromStep = int(std::floor(from / step));
	const auto toStep = int(std::ceil(to / step));
	const auto rFrom = innerRadius - size * sqrt2;
	const auto rTo = outerRadius + size * sqrt2;
	for (auto k = fromStep; k <= toStep; ++k) {
		const auto angle = k * step;
		const auto t = std::fmod(
			(time + 100.) * (1. + (std::sin(angle * 2000.) + 1.) * 0.25),
			1.);
		const auto radius = rFrom + (rTo - rFrom) * t;
		const auto point = radialPoint(center, radius, angle);
		const auto distance = std::hypot(
			point.x() - center.x(),
			point.y() - center.y());
		const auto centerFade = std::min(distance / 64., 1.);
		auto particleAlpha = 0.45
			* alpha
			* (-1.75 * std::abs(t - 0.5) + 1.)
			* (0.25 * (std::sin(t * M_PI) - 1.) + 1.)
			* centerFade;
		particleAlpha = std::clamp(particleAlpha, 0., 1.);
		if (particleAlpha <= 0.) {
			continue;
		}
		const auto scale = 0.75
			* (0.25 * (std::sin(t * M_PI) - 1.) + 1.)
			* (0.8 + (std::sin(angle) + 1.) * 0.25);
		const auto side = size * scale;
		p.setOpacity(particleAlpha);
		p.drawImage(
			Rect(point.x() - side / 2., point.y() - side / 2., Size(side)),
			image);
	}
}

void LocalStorage::Chart::setLoaded() {
	_loaded = true;
}

void LocalStorage::Chart::setParts(
		std::array<int64, kChartPartsCount> sizes,
		int64 total) {
	const auto complete = _loaded && (total <= 0);
	if (_complete != complete) {
		_complete = complete;
		if (_appeared) {
			_completeAnimation.start(
				[=] { update(); },
				complete ? 0. : 1.,
				complete ? 1. : 0.,
				kChartCompleteDuration,
				anim::easeOutQuint);
		} else {
			_completeAnimation.stop();
		}
	}
	if (_sizes == sizes && _total == total) {
		update();
		return;
	}
	hideTooltip();
	_from = shown();
	_sizes = sizes;
	_total = total;
	computeTargets();

	for (auto i = 0; i != kChartPartsCount; ++i) {
		const auto target = _showPercent[i] ? 1. : 0.;
		if (_percentTarget[i] != target) {
			const auto from = _percentAlpha[i].value(_percentTarget[i]);
			_percentTarget[i] = target;
			_percentAlpha[i].start(
				[=] { update(); },
				from,
				target,
				kChartPercentFadeDuration);
		}
	}

	_morph.stop();
	_morph.start(
		[=] { update(); },
		0.,
		1.,
		kChartMorphDuration,
		anim::easeOutQuint);
	if (!_appeared && total > 0) {
		_appeared = true;
		_appear.start(
			[=] { update(); },
			0.,
			1.,
			kChartAppearDuration,
			anim::easeOutQuint);
	}
	update();
}

void LocalStorage::Chart::computeTargets() {
	auto sum = int64();
	auto count = 0;
	for (auto i = 0; i != kChartPartsCount; ++i) {
		if (_sizes[i] > 0) {
			sum += _sizes[i];
			++count;
		}
	}
	_to = std::array<Geometry, kChartPartsCount>();
	_percent = { { 0 } };
	_showPercent = { { false } };
	if (sum <= 0) {
		for (auto i = 0; i != kChartPartsCount; ++i) {
			_to[i].center = _from[i].center;
		}
		return;
	}

	auto under = 0;
	auto minus = 0.;
	for (auto i = 0; i != kChartPartsCount; ++i) {
		const auto fraction = _sizes[i] / float64(sum);
		if (fraction > 0. && fraction < kChartMinFraction) {
			++under;
			minus += fraction;
		}
	}

	auto values = std::vector<float64>(kChartPartsCount);
	for (auto i = 0; i != kChartPartsCount; ++i) {
		values[i] = float64(_sizes[i]);
	}
	const auto percentage = Statistic::PiePartsPercentage(
		values,
		float64(sum),
		true);

	const auto separators = (count >= 2) ? count : 0;
	const auto sweep = 360. - kChartSeparator * separators;
	auto start = 0.;
	auto drawn = 0;
	for (auto i = 0; i != kChartPartsCount; ++i) {
		const auto from = start + drawn * kChartSeparator;
		const auto fraction = _sizes[i] / float64(sum);
		if (fraction <= 0.) {
			_to[i].center = from;
			_to[i].half = 0.;
			continue;
		}
		_percent[i] = int(base::SafeRound(
			percentage.parts[i].roundedPercentage * 100.));
		_showPercent[i] = (fraction > 0.05) && (fraction < 1.);
		auto adjusted = fraction;
		if (adjusted < kChartMinFraction) {
			adjusted = kChartMinFraction;
		} else {
			adjusted *= 1. - (kChartMinFraction * under - minus);
		}
		const auto to = from + adjusted * sweep;
		_to[i].center = (from + to) / 2.;
		_to[i].half = (to - from) / 2.;
		start += adjusted * sweep;
		++drawn;
	}
}

auto LocalStorage::Chart::shown() const
-> std::array<Geometry, kChartPartsCount> {
	const auto progress = _morph.value(1.);
	auto result = std::array<Geometry, kChartPartsCount>();
	for (auto i = 0; i != kChartPartsCount; ++i) {
		result[i].center = _from[i].center
			+ (_to[i].center - _from[i].center) * progress;
		result[i].half = _from[i].half
			+ (_to[i].half - _from[i].half) * progress;
	}
	return result;
}

QPointF LocalStorage::Chart::radialPoint(
		QPointF center,
		float64 radius,
		float64 angle) const {
	const auto radians = angle * M_PI / 180.;
	return QPointF(
		center.x() + radius * std::cos(radians),
		center.y() + radius * std::sin(radians));
}

void LocalStorage::Chart::setSelected(int index) {
	if (_selected == index) {
		return;
	}
	const auto previous = _selected;
	_selected = index;
	const auto animate = [&](int i, float64 to) {
		_selectAnimations[i].start(
			[=] { update(); },
			1. - to,
			to,
			kChartSelectDuration,
			anim::easeOutQuint);
	};
	if (previous >= 0) {
		animate(previous, 0.);
	}
	if (index >= 0) {
		animate(index, 1.);
	}
	setCursor(index >= 0 ? style::cur_pointer : style::cur_default);
	update();
}

int LocalStorage::Chart::resizeGetHeight(int newWidth) {
	return st::localStorageChartHeight;
}

int LocalStorage::Chart::segmentAt(QPointF pos) const {
	const auto center = QPointF(width() / 2., height() / 2.);
	const auto outer = st::localStorageChartDiameter / 2.;
	const auto thickness = st::localStorageChartThickness;
	const auto inner = outer - thickness;
	const auto dx = pos.x() - center.x();
	const auto dy = pos.y() - center.y();
	const auto distance = std::hypot(dx, dy);
	if (distance < inner - st::localStorageChartSelectGrow
		|| distance > outer + st::localStorageChartSelectGrow) {
		return -1;
	}
	auto angle = std::atan2(dy, dx) * 180. / M_PI;
	if (angle < 0.) {
		angle += 360.;
	}
	const auto geometry = shown();
	for (auto i = 0; i != kChartPartsCount; ++i) {
		if (geometry[i].half <= 0.) {
			continue;
		}
		auto delta = std::fmod(std::abs(angle - geometry[i].center), 360.);
		if (delta > 180.) {
			delta = 360. - delta;
		}
		if (delta <= geometry[i].half) {
			return i;
		}
	}
	return -1;
}

void LocalStorage::Chart::mouseMoveEvent(QMouseEvent *e) {
	setSelected(segmentAt(e->pos()));
}

void LocalStorage::Chart::mousePressEvent(QMouseEvent *e) {
	const auto index = segmentAt(e->pos());
	if (index < 0 || index == _tooltipPart) {
		hideTooltip();
	} else {
		showTooltip(index);
	}
}

QPointF LocalStorage::Chart::partCenter(int index) const {
	const auto center = QPointF(width() / 2., height() / 2.);
	const auto outer = st::localStorageChartDiameter / 2.;
	const auto thickness = float64(st::localStorageChartThickness);
	const auto midRadius = outer - thickness / 2.;
	const auto geometry = shown();
	return radialPoint(center, midRadius, geometry[index].center);
}

void LocalStorage::Chart::setPartInfo(Fn<TextWithEntities(int)> info) {
	_partInfo = std::move(info);
}

void LocalStorage::Chart::showTooltip(int index) {
	hideTooltip();
	const auto parent = window();
	if (!parent || !_partInfo || index < 0) {
		return;
	}
	_tooltipPart = index;
	_tooltip = std::make_unique<Ui::ImportantTooltip>(
		parent,
		Ui::MakeNiceTooltipLabel(
			parent,
			rpl::single(_partInfo(index)),
			st::localStorageUsageTooltipMaxWidth,
			st::defaultImportantTooltipLabel),
		st::defaultImportantTooltip);
	const auto tooltip = _tooltip.get();
	const auto weak = base::make_weak(tooltip);
	tooltip->setAttribute(Qt::WA_TransparentForMouseEvents);
	tooltip->setHiddenCallback([=] { delete weak.get(); });
	const auto local = partCenter(index);
	const auto upper = (local.y() < height() / 2.);
	auto area = QRect(local.toPoint(), QSize());
	area.translate(0, upper
		? -st::localStorageUsageTooltipSkip
		: st::localStorageUsageTooltipSkip);
	area = Ui::MapFrom(parent, this, area);
	tooltip->pointAt(area, upper ? RectPart::Top : RectPart::Bottom);
	tooltip->toggleAnimated(true);
}

void LocalStorage::Chart::hideTooltip() {
	_tooltipPart = -1;
	if (const auto tooltip = _tooltip.release()) {
		tooltip->toggleAnimated(false);
	}
}

void LocalStorage::Chart::leaveEventHook(QEvent *e) {
	setSelected(-1);
}

void LocalStorage::Chart::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);

	const auto appear = (_total <= 0)
		? 1.
		: (_appeared ? _appear.value(1.) : 0.);
	const auto complete = _completeAnimation.value(_complete ? 1. : 0.);
	const auto segmentAppear = appear * (1. - complete);
	const auto center = QPointF(width() / 2., height() / 2.);
	const auto outer = st::localStorageChartDiameter / 2.;
	const auto thin = float64(st::localStorageChartThicknessThin);
	const auto full = float64(st::localStorageChartThickness);
	const auto thickness = thin + (full - thin) * segmentAppear;
	const auto rotation = (1. - appear) * -120.;
	const auto midRadius = outer - thickness / 2.;

	p.setOpacity(appear);

	auto background = QPen(st::windowBgOver->c);
	background.setWidthF(thickness);
	background.setCapStyle(Qt::FlatCap);
	p.setPen(background);
	p.setBrush(Qt::NoBrush);
	p.drawEllipse(center, midRadius, midRadius);

	p.setOpacity(segmentAppear);
	const auto geometry = shown();
	for (auto i = 0; i != kChartPartsCount; ++i) {
		if (geometry[i].half <= 0.1) {
			continue;
		}
		const auto selected = _selectAnimations[i].value(
			(_selected == i) ? 1. : 0.);
		const auto grow = selected * st::localStorageChartSelectGrow;
		const auto segmentThickness = thickness + grow;
		const auto segmentMid = midRadius + grow / 2.;
		const auto segmentOuter = outer + grow;
		const auto base = PartColor(i);

		auto gradient = QRadialGradient(center, segmentOuter);
		gradient.setColorAt(
			0.3,
			anim::color(base, QColor(255, 255, 255), 0.30));
		gradient.setColorAt(1., base);

		auto pen = QPen(QBrush(gradient), segmentThickness);
		pen.setCapStyle(Qt::FlatCap);
		p.setPen(pen);

		const auto from = geometry[i].center - geometry[i].half + rotation;
		const auto to = geometry[i].center + geometry[i].half + rotation;
		const auto rect = Rect(
			center.x() - segmentMid,
			center.y() - segmentMid,
			Size(segmentMid * 2.));
		const auto startAngle = qRound(-from * 16.);
		const auto spanAngle = -qRound((to - from) * 16.);
		p.drawArc(rect, startAngle, spanAngle);

		const auto innerRadius = segmentOuter - segmentThickness;
		const auto outerRect = Rect(
			center.x() - segmentOuter,
			center.y() - segmentOuter,
			Size(segmentOuter * 2.));
		const auto innerRect = Rect(
			center.x() - innerRadius,
			center.y() - innerRadius,
			Size(innerRadius * 2.));
		auto sector = QPainterPath();
		sector.arcMoveTo(outerRect, startAngle / 16.);
		sector.arcTo(outerRect, startAngle / 16., spanAngle / 16.);
		sector.arcTo(
			innerRect,
			(startAngle + spanAngle) / 16.,
			-spanAngle / 16.);
		sector.closeSubpath();
		p.save();
		p.setClipPath(sector);
		drawParticles(
			p,
			center,
			i,
			from,
			to,
			innerRadius,
			segmentOuter,
			segmentAppear);
		p.restore();
		p.setOpacity(segmentAppear);
	}

	p.setFont(st::localStorageChartPercentFont);
	const auto percentFont = st::localStorageChartPercentFont;
	for (auto i = 0; i != kChartPartsCount; ++i) {
		const auto labelAlpha = _percentAlpha[i].value(_percentTarget[i]);
		if (labelAlpha <= 0. || geometry[i].half <= 0.1) {
			continue;
		}
		const auto selected = _selectAnimations[i].value(
			(_selected == i) ? 1. : 0.);
		const auto point = radialPoint(
			center,
			midRadius + selected * st::localStorageChartSelectGrow / 2.,
			geometry[i].center + rotation);
		const auto text = QString::number(_percent[i]) + '%';
		const auto twidth = percentFont->width(text);
		p.setOpacity(appear * labelAlpha);
		p.setPen(QColor(255, 255, 255));
		p.save();
		p.translate(point);
		const auto scale = 1. + selected * 0.1;
		p.scale(scale, scale);
		p.drawText(
			QRectF(
				-twidth,
				-percentFont->height,
				twidth * 2.,
				percentFont->height * 2.),
			text,
			QTextOption(Qt::AlignCenter));
		p.restore();
	}
	p.setOpacity(1.);

	const auto number = QString::number(
		(_total + kMegabyte - 1) / kMegabyte);
	const auto unit = u"MB"_q;
	const auto sizeFont = st::localStorageChartSizeFont;
	const auto unitFont = st::localStorageChartUnitFont;
	const auto blockHeight = sizeFont->height + unitFont->height;
	const auto top = (st::localStorageChartHeight - blockHeight) / 2;

	p.setOpacity(segmentAppear);
	p.save();
	const auto scale = 0.6 + 0.4 * appear;
	p.translate(center);
	p.scale(scale, scale);
	p.translate(-center);
	p.setFont(sizeFont);
	p.setPen(st::windowBoldFg);
	p.drawText(
		QRect(0, top, width(), sizeFont->height),
		number,
		style::al_top);
	p.setFont(unitFont);
	p.setPen(st::windowSubTextFg);
	p.drawText(
		QRect(0, top + sizeFont->height, width(), unitFont->height),
		unit,
		style::al_top);
	p.restore();

	if (complete > 0.) {
		_completeParticles.paint(
			p,
			rect(),
			center,
			complete,
			crl::now(),
			paused());

		auto gradient = QLinearGradient(
			QPointF(center.x(), center.y() - outer),
			QPointF(center.x(), center.y() + outer));
		gradient.setColorAt(0., QColor(0x6e, 0xd5, 0x56));
		gradient.setColorAt(1., QColor(0x41, 0xba, 0x71));
		const auto brush = QBrush(gradient);
		p.setOpacity(complete);
		p.setBrush(Qt::NoBrush);

		auto ring = QPen(brush, thickness);
		ring.setCapStyle(Qt::FlatCap);
		p.setPen(ring);
		p.drawEllipse(center, midRadius, midRadius);

		const auto boxLeft = center.x() - outer;
		const auto boxTop = center.y() - outer;
		const auto diameter = outer * 2.;
		auto check = QPainterPath();
		check.moveTo(boxLeft + diameter * 0.348, boxTop + diameter * 0.538);
		check.lineTo(boxLeft + diameter * 0.447, boxTop + diameter * 0.636);
		check.lineTo(boxLeft + diameter * 0.678, boxTop + diameter * 0.402);
		auto checkPen = QPen(brush, thin);
		checkPen.setCapStyle(Qt::RoundCap);
		checkPen.setJoinStyle(Qt::RoundJoin);
		p.setPen(checkPen);
		p.drawPath(check);
	}
	p.setOpacity(1.);
}

class LocalStorage::DeviceBar final : public Ui::RpWidget {
public:
	explicit DeviceBar(QWidget *parent);

	void setCache(int64 cache);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	void refresh();
	void apply();
	[[nodiscard]] QRect barRect() const;
	[[nodiscard]] TextWithEntities tooltipText() const;
	void showTooltip();
	void hideTooltip();

	object_ptr<Ui::FlatLabel> _subtitle;
	std::unique_ptr<Ui::ImportantTooltip> _tooltip;
	int64 _reported = 0;
	int64 _cache = 0;
	int64 _total = 0;
	int64 _free = 0;

};

LocalStorage::DeviceBar::DeviceBar(QWidget *parent)
: RpWidget(parent)
, _subtitle(this, QString(), st::localStorageUsageSubtitle) {
	_subtitle->setAttribute(Qt::WA_TransparentForMouseEvents);
	setMouseTracking(true);
	refresh();
}

void LocalStorage::DeviceBar::refresh() {
	const auto weak = base::make_weak(this);
	const auto dir = cWorkingDir();
	crl::async([=] {
		const auto info = QStorageInfo(dir);
		const auto total = info.isValid() ? info.bytesTotal() : int64();
		const auto free = info.isValid() ? info.bytesAvailable() : int64();
		crl::on_main(weak, [=] {
			_total = total;
			_free = (free > total) ? total : free;
			apply();
		});
	});
}

void LocalStorage::DeviceBar::apply() {
	const auto used = std::max(_total - _free, int64());
	_cache = std::min(_reported, used);
	_subtitle->setText(tr::lng_local_storage_device_usage(
		tr::now,
		lt_percent,
		FormatStoragePercent(_cache, _total)));
	resizeToWidth(width());
	update();
}

void LocalStorage::DeviceBar::setCache(int64 cache) {
	_reported = cache;
	apply();
	refresh();
}

QRect LocalStorage::DeviceBar::barRect() const {
	const auto width = std::min(st::localStorageUsageBarWidth, QWidget::width());
	const auto top = st::localStorageUsageTopSkip
		+ _subtitle->height()
		+ st::localStorageUsageBarSkip;
	return QRect(
		(QWidget::width() - width) / 2,
		top,
		width,
		st::localStorageUsageBarHeight);
}

int LocalStorage::DeviceBar::resizeGetHeight(int newWidth) {
	_subtitle->resizeToWidth(newWidth);
	_subtitle->moveToLeft(0, st::localStorageUsageTopSkip, newWidth);
	return st::localStorageUsageTopSkip
		+ _subtitle->height()
		+ st::localStorageUsageBarSkip
		+ st::localStorageUsageBarHeight
		+ st::localStorageUsageBottomSkip;
}

void LocalStorage::DeviceBar::paintEvent(QPaintEvent *e) {
	if (_total <= 0) {
		return;
	}
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);

	const auto bar = QRectF(barRect());
	const auto radius = bar.height() / 2.;
	const auto used = _total - _free;
	const auto cacheColor = st::activeButtonBg->c;
	const auto usedColor = st::scrollBg->c;
	const auto freeColor = st::windowBgRipple->c;

	auto path = QPainterPath();
	path.addRoundedRect(bar, radius, radius);
	p.setClipPath(path);
	p.fillRect(bar, freeColor);

	const auto fill = [&](int64 from, int64 size, QColor color) {
		if (size <= 0) {
			return;
		}
		const auto x1 = bar.x() + bar.width() * (from / float64(_total));
		const auto x2 = bar.x()
			+ bar.width() * ((from + size) / float64(_total));
		p.fillRect(QRectF(x1, bar.y(), x2 - x1, bar.height()), color);
	};
	fill(0, used, usedColor);
	fill(0, _cache, cacheColor);
}

void LocalStorage::DeviceBar::mouseMoveEvent(QMouseEvent *e) {
	if (_total > 0 && barRect().marginsAdded(st::localStorageUsageBarMargin)
			.contains(e->pos())) {
		showTooltip();
	} else {
		hideTooltip();
	}
}

void LocalStorage::DeviceBar::leaveEventHook(QEvent *e) {
	hideTooltip();
}

TextWithEntities LocalStorage::DeviceBar::tooltipText() const {
	const auto used = _total - _free;
	const auto other = std::max(used - _cache, int64());
	auto result = TextWithEntities();
	const auto line = [&](const QString &label, int64 size) {
		if (!result.text.isEmpty()) {
			result.append('\n');
		}
		result.append(label).append(u": "_q).append(
			Ui::Text::Bold(FormatStorageSize(size)));
	};
	line(tr::lng_local_storage_device_telegram(tr::now), _cache);
	line(tr::lng_local_storage_device_other(tr::now), other);
	line(tr::lng_local_storage_device_free(tr::now), _free);
	line(tr::lng_local_storage_device_total(tr::now), _total);
	return result;
}

void LocalStorage::DeviceBar::showTooltip() {
	if (_tooltip || _total <= 0) {
		return;
	}
	const auto parent = window();
	if (!parent) {
		return;
	}
	_tooltip = std::make_unique<Ui::ImportantTooltip>(
		parent,
		Ui::MakeNiceTooltipLabel(
			parent,
			rpl::single(tooltipText()),
			st::localStorageUsageTooltipMaxWidth,
			st::defaultImportantTooltipLabel),
		st::defaultImportantTooltip);
	const auto tooltip = _tooltip.get();
	const auto weak = base::make_weak(tooltip);
	tooltip->setAttribute(Qt::WA_TransparentForMouseEvents);
	tooltip->setHiddenCallback([=] { delete weak.get(); });
	auto area = Ui::MapFrom(parent, this, barRect());
	area.translate(0, -st::localStorageUsageTooltipSkip);
	tooltip->pointAt(area, RectPart::Top);
	tooltip->toggleAnimated(true);
}

void LocalStorage::DeviceBar::hideTooltip() {
	if (const auto tooltip = _tooltip.release()) {
		tooltip->toggleAnimated(false);
	}
}

class LocalStorage::ClearButton final : public Ui::RippleButton {
public:
	explicit ClearButton(QWidget *parent);

	void setText(const QString &label, const QString &amount);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;
	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	Ui::AnimatedString _label;
	Ui::AnimatedString _amount;

};

LocalStorage::ClearButton::ClearButton(QWidget *parent)
: RippleButton(parent, st::defaultRippleAnimation)
, _label(st::localStorageClearFont, [=] { update(); }, {
	.splitByWords = true,
	.preserveIndex = true,
	.startFromEnd = true,
	.moveAmplitude = 0.25,
	.duration = crl::time(300),
})
, _amount(st::localStorageClearFont, [=] { update(); }, {
	.splitByWords = true,
	.preserveIndex = true,
	.startFromEnd = true,
	.moveAmplitude = 0.25,
	.duration = crl::time(300),
}) {
}

void LocalStorage::ClearButton::setText(
		const QString &label,
		const QString &amount) {
	_label.setText(label);
	_amount.setText(amount);
}

int LocalStorage::ClearButton::resizeGetHeight(int newWidth) {
	return st::localStorageClearHeight;
}

void LocalStorage::ClearButton::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);

	const auto disabled = isDisabled();
	const auto radius = height() / 2.;
	p.setPen(Qt::NoPen);
	p.setBrush(disabled ? st::windowBgRipple : st::activeButtonBg);
	p.drawRoundedRect(rect(), radius, radius);

	if (!disabled) {
		const auto ripple = st::activeButtonBgRipple->c;
		paintRipple(p, 0, 0, &ripple);
	}

	const auto font = st::localStorageClearFont;
	const auto labelWidth = _label.currentWidth();
	const auto amountWidth = _amount.currentWidth();
	const auto skip = (amountWidth > 0.) ? st::localStorageClearSkip : 0;
	const auto left = (width() - labelWidth - skip - amountWidth) / 2.;
	const auto top = (height() - font->height) / 2;
	const auto fg = disabled ? st::windowSubTextFg->c : st::activeButtonFg->c;
	_label.draw(p, int(base::SafeRound(left)), top, fg);
	_amount.draw(p, int(base::SafeRound(left + labelWidth + skip)), top, fg, 0.6);
}

QImage LocalStorage::ClearButton::prepareRippleMask() const {
	return Ui::RippleAnimation::RoundRectMask(size(), height() / 2);
}

QPoint LocalStorage::ClearButton::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos());
}

class LocalStorage::Row final : public Ui::RippleButton {
public:
	Row(
		QWidget *parent,
		const QString &title,
		int chartIndex,
		bool checked,
		const Database::TaggedSummary &data);

	void update(const Database::TaggedSummary &data);
	void setPercent(int percent);
	void setRoundedTop(bool rounded);

	[[nodiscard]] bool checked() const;
	[[nodiscard]] rpl::producer<bool> checkedChanges() const;
	void setChecked(bool checked);
	void shake();

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;
	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	QString sizeText(const Database::TaggedSummary &data) const;
	void toggle();

	style::complex_color _checkColor;
	style::Check _checkStyle;
	Ui::RoundCheckView _check;
	object_ptr<Ui::FlatLabel> _percent;
	object_ptr<Ui::FlatLabel> _name;
	object_ptr<Ui::FlatLabel> _size;
	rpl::event_stream<bool> _checkedChanges;
	Ui::Animations::Simple _shake;
	bool _roundedTop = false;
	bool _hasPercent = false;

};

LocalStorage::Row::Row(
	QWidget *parent,
	const QString &title,
	int chartIndex,
	bool checked,
	const Database::TaggedSummary &data)
: RippleButton(parent, st::defaultRippleAnimation)
, _checkColor([=] { return PartColor(chartIndex); })
, _checkStyle(st::localStorageCategoryCheck)
, _check(_checkStyle, checked, [=] { Ui::RpWidget::update(); })
, _percent(this, QString(), st::localStorageCategoryPercent)
, _name(this, title, st::localStorageCategoryName)
, _size(this, sizeText(data), st::localStorageCategorySize) {
	_checkStyle.toggledFg = _checkColor.color();
	_percent->setAttribute(Qt::WA_TransparentForMouseEvents);
	_name->setAttribute(Qt::WA_TransparentForMouseEvents);
	_size->setAttribute(Qt::WA_TransparentForMouseEvents);
	setClickedCallback([=] { toggle(); });
}

void LocalStorage::Row::toggle() {
	const auto value = !_check.checked();
	_check.setChecked(value, anim::type::normal);
	_checkedChanges.fire_copy(value);
}

bool LocalStorage::Row::checked() const {
	return _check.checked();
}

rpl::producer<bool> LocalStorage::Row::checkedChanges() const {
	return _checkedChanges.events();
}

void LocalStorage::Row::setChecked(bool checked) {
	_check.setChecked(checked, anim::type::normal);
}

void LocalStorage::Row::shake() {
	_shake.stop();
	_shake.start(
		Ui::DefaultShakeCallback([=](int shift) { move(shift, y()); }),
		0.,
		1.,
		st::shakeDuration);
}

void LocalStorage::Row::setRoundedTop(bool rounded) {
	_roundedTop = rounded;
}

void LocalStorage::Row::update(const Database::TaggedSummary &data) {
	_size->setText(sizeText(data));
	resizeToWidth(width());
}

void LocalStorage::Row::setPercent(int percent) {
	_hasPercent = (percent > 0);
	_percent->setText(_hasPercent
		? (QString::number(percent) + '%')
		: QString());
	resizeToWidth(width());
}

int LocalStorage::Row::resizeGetHeight(int newWidth) {
	const auto height = st::localStorageCategoryHeight;
	const auto pad = st::localStorageCategoryPadding;
	const auto checkSize = _check.getSize();
	const auto left = pad.left()
		+ checkSize.width()
		+ st::localStorageCategoryCheckSkip;

	_size->resizeToWidth(std::min(_size->textMaxWidth(), newWidth));
	_size->moveToRight(pad.right(), (height - _size->height()) / 2, newWidth);

	_percent->resizeToWidth(_percent->textMaxWidth());
	_percent->setVisible(_hasPercent);
	const auto percentWidth = _hasPercent
		? (st::localStorageCategoryPercentSkip + _percent->width())
		: 0;
	const auto nameAvailable = std::max(
		_size->x() - st::localStorageCategoryPercentSkip - percentWidth - left,
		0);
	_name->resizeToWidth(std::min(_name->textMaxWidth(), nameAvailable));
	_name->moveToLeft(left, (height - _name->height()) / 2, newWidth);
	if (_hasPercent) {
		_percent->moveToLeft(
			left + _name->width() + st::localStorageCategoryPercentSkip,
			(height - _percent->height()) / 2,
			newWidth);
	}
	return height;
}

void LocalStorage::Row::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	paintRipple(p, 0, 0);
	const auto pad = st::localStorageCategoryPadding;
	const auto checkSize = _check.getSize();
	_check.paint(
		p,
		pad.left(),
		(height() - checkSize.height()) / 2,
		width());
}

QImage LocalStorage::Row::prepareRippleMask() const {
	if (!_roundedTop) {
		return Ui::RippleAnimation::RectMask(size());
	}
	const auto radius = st::localStorageIslandRadius;
	return Ui::RippleAnimation::MaskByDrawer(size(), false, [&](QPainter &p) {
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(Qt::white);
		p.drawRoundedRect(
			Rect(QSize(width(), height() + radius)),
			radius,
			radius);
	});
}

QPoint LocalStorage::Row::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos());
}

QString LocalStorage::Row::sizeText(
		const Database::TaggedSummary &data) const {
	return data.totalSize
		? Ui::FormatSizeText(data.totalSize)
		: tr::lng_local_storage_empty(tr::now);
}

LocalStorage::LocalStorage(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller)
, _session(&controller->session())
, _db(&_session->data().cache())
, _dbBig(&_session->data().cacheBigFile())
, _bottomSkipRounding(st::boxRadius, st::windowBgOver) {
	const auto &settings = _session->local().cacheSettings();
	const auto &settingsBig = _session->local().cacheBigFileSettings();
	_totalSizeLimit = settings.totalSizeLimit + settingsBig.totalSizeLimit;
	_mediaSizeLimit = settingsBig.totalSizeLimit;
	_timeLimit = settings.totalTimeLimit;

	setupContent();
}

rpl::producer<QString> LocalStorage::title() {
	return tr::lng_local_storage_title();
}

bool LocalStorage::paintOuter(
		not_null<QWidget*> outer,
		int maxVisibleHeight,
		QRect clip) {
	QPainter(outer.get()).fillRect(clip, st::windowBgOver);
	return true;
}

void LocalStorage::showFinished() {
	Section::showFinished();

	if (_clearButton) {
		controller()->checkHighlightControl(
			u"storage/clear-cache"_q,
			_clearButton,
			{ .rippleShape = true });
	}
	if (_totalSlider) {
		const auto add = st::roundRadiusSmall;
		controller()->checkHighlightControl(
			u"storage/max-cache"_q,
			_totalSlider,
			{ .margin = Margins(-add), .radius = add });
	}
}

void LocalStorage::updateRow(
		not_null<Ui::SlideWrap<Row>*> row,
		const Database::TaggedSummary *data) {
	const auto shown = (data && data->count && data->totalSize);
	if (shown) {
		row->entity()->update(*data);
	}
	row->toggle(shown, anim::type::normal);
}

void LocalStorage::update(
		Database::Stats &&stats,
		Database::Stats &&statsBig) {
	_stats = std::move(stats);
	_statsBig = std::move(statsBig);
	for (const auto &entry : _rows) {
		if (entry.first == kFakeMediaCacheTag) {
			updateRow(entry.second, &_statsBig.full);
		} else if (entry.first) {
			const auto i = _stats.tagged.find(entry.first);
			updateRow(
				entry.second,
				(i != end(_stats.tagged)) ? &i->second : nullptr);
		}
	}
	updateRowCorners();
	if (_chart) {
		_chart->setLoaded();
	}
	updateChart();
	updateDeviceBar();
	updateClearButton();
	updateCategoriesWrap();
	finishClearing();
}

void LocalStorage::updateCategoriesWrap() {
	if (!_categoriesWrap) {
		return;
	}
	_categoriesWrap->toggle(
		summary().totalSize > 0,
		_categoriesInited ? anim::type::normal : anim::type::instant);
	_categoriesInited = true;
}

void LocalStorage::toggleSelected(
		int chartIndex,
		bool selected,
		not_null<Row*> row) {
	if (!selected) {
		const auto sizes = chartedSizes();
		auto count = 0;
		for (auto i = 0; i != kChartPartsCount; ++i) {
			if (_selected[i] && sizes[i] > 0) {
				++count;
			}
		}
		if (count <= 1) {
			row->setChecked(true);
			row->shake();
			return;
		}
	}
	_selected[chartIndex] = selected;
	auto all = true;
	for (const auto value : _selected) {
		if (!value) {
			all = false;
			break;
		}
	}
	_allSelected = all;
	updateChart();
	updateClearButton();
}

std::array<int64, kChartPartsCount> LocalStorage::chartedSizes() const {
	auto result = std::array<int64, kChartPartsCount>();
	for (auto i = 0; i != kChartPartsCount; ++i) {
		const auto tag = kChartTags[i];
		if (tag == kFakeMediaCacheTag) {
			result[i] = _statsBig.full.totalSize;
		} else {
			const auto j = _stats.tagged.find(uint8(tag));
			result[i] = (j != end(_stats.tagged))
				? j->second.totalSize
				: int64();
		}
	}
	return result;
}

void LocalStorage::updateChart() {
	if (!_chart) {
		return;
	}
	auto sizes = chartedSizes();
	auto total = int64();
	for (auto i = 0; i != kChartPartsCount; ++i) {
		if (_selected[i]) {
			total += sizes[i];
		} else {
			sizes[i] = 0;
		}
	}
	_chart->setParts(sizes, total);
	updateCategoryPercents();
}

void LocalStorage::updateCategoryPercents() {
	for (const auto &[tag, row] : _rows) {
		const auto index = ChartIndexForTag(tag);
		if (index < 0) {
			continue;
		}
		row->entity()->setPercent(_chart->partPercent(index));
	}
}

void LocalStorage::updateRowCorners() {
	auto first = true;
	for (const auto &[tag, wrap] : _rows) {
		const auto shown = wrap->toggled();
		wrap->entity()->setRoundedTop(shown && first);
		if (shown) {
			first = false;
		}
	}
}

void LocalStorage::updateDeviceBar() {
	if (_deviceBar) {
		_deviceBar->setCache(summary().totalSize);
	}
}

void LocalStorage::updateClearButton() {
	if (!_clearButton) {
		return;
	}
	const auto all = _allSelected.current();
	auto freed = int64();
	if (all) {
		freed = summary().totalSize;
	} else {
		const auto sizes = chartedSizes();
		for (auto i = 0; i != kChartPartsCount; ++i) {
			if (_selected[i]) {
				freed += sizes[i];
			}
		}
	}
	_clearButton->setText(
		(all
			? tr::lng_local_storage_clear(tr::now)
			: tr::lng_local_storage_clear_selected(tr::now)),
		(freed > 0) ? Ui::FormatSizeText(freed) : QString());
	_clearButton->setDisabled(freed <= 0);
}

void LocalStorage::showClearingBox() {
	if (_clearingBox) {
		return;
	}
	auto box = Box([=](not_null<Ui::GenericBox*> box) {
		box->setWidth(st::boxWidth);
		box->setCloseByOutsideClick(false);
		box->boxClosing(
		) | rpl::on_next(crl::guard(this, [=] {
			if (!_clearingStarted) {
				_clearRequested = false;
				_minDurationPassed = false;
			}
		}), box->lifetime());
		auto icon = CreateLottieIcon(
			box->verticalLayout(),
			{
				.name = u"cleaning_cache"_q,
				.sizeOverride = st::localStorageClearingLottieSize,
			},
			st::localStorageClearingSpinnerPadding);
		const auto animate = std::move(icon.animate);
		box->addRow(std::move(icon.widget));
		box->setShowFinishedCallback(crl::guard(this, [=] {
			animate(anim::repeat::loop);
			if (_clearingStarted) {
				return;
			}
			_clearingStarted = true;
			startClearing();
			base::call_delayed(kClearingMinDuration, this, [=] {
				_minDurationPassed = true;
				finishClearing();
			});
		}));
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_local_storage_clearing(),
				st::changePhoneTitle),
			st::changePhoneTitlePadding,
			style::al_top);
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_local_storage_clearing_about(),
				st::changePhoneDescription),
			st::changePhoneDescriptionPadding,
			style::al_top);
	});
	const auto raw = box.data();
	_clearingBox = base::make_weak(raw);
	controller()->show(std::move(box));
}

auto LocalStorage::summary() const -> Database::TaggedSummary {
	auto result = _stats.full;
	result.count += _statsBig.full.count;
	result.totalSize += _statsBig.full.totalSize;
	return result;
}

void LocalStorage::clearSelected() {
	if (_clearRequested) {
		return;
	}
	_clearRequested = true;
	_clearingStarted = false;
	_minDurationPassed = false;
	if (_allSelected.current()) {
		_clearFreedBase = summary().totalSize;
	} else {
		const auto sizes = chartedSizes();
		auto freed = int64();
		for (auto i = 0; i != kChartPartsCount; ++i) {
			if (_selected[i]) {
				freed += sizes[i];
			}
		}
		_clearFreedBase = freed;
	}
	showClearingBox();
}

void LocalStorage::startClearing() {
	if (_allSelected.current()) {
		_db->clear();
		_dbBig->clear();
		Ui::Emoji::ClearIrrelevantCache();
		return;
	}
	for (auto i = 0; i != kChartPartsCount; ++i) {
		if (!_selected[i]) {
			continue;
		}
		const auto tag = kChartTags[i];
		if (tag == kFakeMediaCacheTag) {
			_dbBig->clear();
		} else {
			_db->clearByTag(uint8(tag));
		}
	}
}

void LocalStorage::finishClearing() {
	if (!_clearRequested
		|| !_clearingStarted
		|| !_minDurationPassed
		|| _stats.clearing
		|| _statsBig.clearing) {
		return;
	}
	_clearRequested = false;
	const auto freed = std::max(
		_clearFreedBase - summary().totalSize,
		int64());
	_selected.fill(true);
	_allSelected = true;
	for (const auto &[tag, row] : _rows) {
		row->entity()->setChecked(true);
	}
	if (const auto strong = _clearingBox.get()) {
		strong->closeBox();
	}
	if (freed > 0) {
		controller()->showToast(tr::lng_local_storage_cleared(
			tr::now,
			lt_size,
			Ui::FormatSizeText(freed)));
	}
	updateChart();
	updateClearButton();
	updateCategoriesWrap();
}

void LocalStorage::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	content->paintRequest(
	) | rpl::on_next([=](QRect clip) {
		QPainter(content).fillRect(clip, st::windowBgOver);
	}, content->lifetime());

	setupControls(content);

	rpl::combine(
		_db->statsOnMain(),
		_dbBig->statsOnMain()
	) | rpl::on_next([=](
			Database::Stats &&stats,
			Database::Stats &&statsBig) {
		update(std::move(stats), std::move(statsBig));
	}, content->lifetime());

	Ui::ResizeFitChild(this, content);
}

void LocalStorage::setupControls(not_null<Ui::VerticalLayout*> container) {
	const auto createRow = [&](
			not_null<Ui::VerticalLayout*> into,
			uint16 tag,
			const Database::TaggedSummary &data) {
		const auto chartIndex = ChartIndexForTag(tag);
		const auto checked = _selected[chartIndex];
		auto result = into->add(object_ptr<Ui::SlideWrap<Row>>(
			into,
			object_ptr<Row>(
				into,
				PartName(chartIndex),
				chartIndex,
				checked,
				data)));
		const auto shown = (data.count && data.totalSize);
		result->toggle(shown, anim::type::instant);
		const auto entity = result->entity();
		entity->checkedChanges(
		) | rpl::on_next([=](bool checked) {
			toggleSelected(chartIndex, checked, entity);
		}, result->lifetime());
		_rows.emplace(tag, result);
		return result;
	};
	const auto createTagRow = [&](
			not_null<Ui::VerticalLayout*> into,
			uint8 tag) {
		static const auto empty = Database::TaggedSummary();
		const auto i = _stats.tagged.find(tag);
		createRow(into, tag, (i != end(_stats.tagged)) ? i->second : empty);
	};

	_chart = container->add(object_ptr<Chart>(container, controller()));
	_chart->setPartInfo([this](int index) -> TextWithEntities {
		const auto sizes = chartedSizes();
		auto total = int64();
		for (auto i = 0; i != kChartPartsCount; ++i) {
			if (_selected[i]) {
				total += sizes[i];
			}
		}
		const auto count = [&] {
			const auto tagged = [&](uint8 tag) {
				const auto i = _stats.tagged.find(tag);
				return (i != end(_stats.tagged)) ? i->second.count : size_type();
			};
			switch (index) {
			case 0: return tagged(Data::kImageCacheTag);
			case 1: return tagged(Data::kStickerCacheTag);
			case 2: return tagged(Data::kVoiceMessageCacheTag);
			case 3: return tagged(Data::kVideoMessageCacheTag);
			case 4: return tagged(Data::kAnimationCacheTag);
			case 5: return _statsBig.full.count;
			}
			return size_type();
		}();
		auto result = TextWithEntities();
		result.append(Ui::Text::Bold(PartTitle(index, count)));
		result.append('\n');
		result.append(FormatStorageSize(sizes[index]));
		if (total > 0) {
			const auto percent = int(base::SafeRound(
				sizes[index] / float64(total) * 100.));
			if (percent > 0) {
				result.append(u" · "_q).append(QString::number(percent) + '%');
			}
		}
		return result;
	});

	_deviceBar = container->add(object_ptr<DeviceBar>(container));

	_categoriesWrap = AddIslandWrap(container);
	const auto categoriesIsland = _categoriesWrap->entity();
	createTagRow(categoriesIsland, Data::kImageCacheTag);
	createTagRow(categoriesIsland, Data::kStickerCacheTag);
	createTagRow(categoriesIsland, Data::kVoiceMessageCacheTag);
	createTagRow(categoriesIsland, Data::kVideoMessageCacheTag);
	createTagRow(categoriesIsland, Data::kAnimationCacheTag);
	createRow(categoriesIsland, kFakeMediaCacheTag, _statsBig.full);

	_clearButton = categoriesIsland->add(
		object_ptr<ClearButton>(categoriesIsland),
		st::localStorageClearMargin);
	_clearButton->setClickedCallback([=] { clearSelected(); });
	Ui::AddSkip(categoriesIsland, st::localStorageIslandPadding);

	const auto addAbout = [&](rpl::producer<QString> text) {
		const auto label = container->add(
			object_ptr<Ui::FlatLabel>(
				container,
				std::move(text),
				st::localStorageAbout),
			st::localStorageAboutMargin);
		label->setAttribute(Qt::WA_TransparentForMouseEvents);
	};

	addAbout(tr::lng_local_storage_about());

	const auto settingsIsland = AddIsland(container);
	setupLimits(settingsIsland);
	Ui::AddSkip(settingsIsland, st::localStorageIslandPadding);

	addAbout(tr::lng_local_storage_max_size_about());

	updateRowCorners();
	updateChart();
	updateDeviceBar();
	updateClearButton();

	Ui::AddSkip(container, st::localStorageBottomSkip);
}

template <
	typename Value,
	typename Convert,
	typename Callback,
	typename>
not_null<Ui::MediaSlider*> LocalStorage::createLimitsSlider(
		not_null<Ui::VerticalLayout*> container,
		int valuesCount,
		const QString &name,
		Convert &&convert,
		Value currentValue,
		Callback &&callback) {
	const auto row = container->add(
		object_ptr<Ui::FixedHeightWidget>(
			container,
			st::localStorageLimitLabel.font->height),
		st::localStorageLimitLabelMargin);
	const auto title = Ui::CreateChild<Ui::LabelSimple>(
		row,
		st::localStorageLimitLabel,
		name);
	const auto label = Ui::CreateChild<Ui::LabelSimple>(
		row,
		st::localStorageLimitValue);
	rpl::combine(
		row->widthValue(),
		label->widthValue()
	) | rpl::on_next([=](int width, int) {
		title->moveToLeft(0, 0, width);
		label->moveToRight(0, 0, width);
	}, row->lifetime());
	callback(label, currentValue);
	const auto slider = container->add(
		object_ptr<Ui::MediaSlider>(container, st::localStorageLimitSlider),
		st::localStorageLimitMargin);
	slider->resize(st::localStorageLimitSlider.seekSize);
	slider->setPseudoDiscrete(
		valuesCount,
		std::forward<Convert>(convert),
		currentValue,
		[=, callback = std::forward<Callback>(callback)](Value value) {
			callback(label, value);
		});
	return slider;
}

void LocalStorage::updateMediaLimit() {
	const auto good = [&](int64 mediaLimit) {
		return (_totalSizeLimit - mediaLimit >= kMinimalSizeLimit);
	};
	if (good(_mediaSizeLimit) || !_mediaSlider || !_mediaLabel) {
		return;
	}
	auto index = 1;
	while ((index < kMediaSizeLimitsCount)
		&& (MediaSizeLimit(index) * 2 <= _totalSizeLimit)) {
		++index;
	}
	--index;
	_mediaSizeLimit = MediaSizeLimit(index);
	_mediaSlider->setValue(index / float64(kMediaSizeLimitsCount - 1));
	updateMediaLabel();

	Ensures(good(_mediaSizeLimit));
}

void LocalStorage::updateTotalLimit() {
	const auto good = [&](int64 totalLimit) {
		return (totalLimit - _mediaSizeLimit >= kMinimalSizeLimit);
	};
	if (good(_totalSizeLimit) || !_totalSlider || !_totalLabel) {
		return;
	}
	auto index = kTotalSizeLimitsCount - 1;
	while ((index > 0)
		&& (TotalSizeLimit(index - 1) >= 2 * _mediaSizeLimit)) {
		--index;
	}
	_totalSizeLimit = TotalSizeLimit(index);
	_totalSlider->setValue(index / float64(kTotalSizeLimitsCount - 1));
	updateTotalLabel();

	Ensures(good(_totalSizeLimit));
}

void LocalStorage::updateTotalLabel() {
	Expects(_totalLabel != nullptr);

	_totalLabel->setText(SizeLimitText(_totalSizeLimit));
}

void LocalStorage::updateMediaLabel() {
	Expects(_mediaLabel != nullptr);

	_mediaLabel->setText(SizeLimitText(_mediaSizeLimit));
}

void LocalStorage::setupLimits(not_null<Ui::VerticalLayout*> container) {
	Ui::AddSubsectionTitle(
		container,
		tr::lng_local_storage_manage_title());

	_totalSlider = createLimitsSlider(
		container,
		kTotalSizeLimitsCount,
		tr::lng_local_storage_size_limit_title(tr::now),
		TotalSizeLimit,
		_totalSizeLimit,
		[=](not_null<Ui::LabelSimple*> label, int64 limit) {
			_totalSizeLimit = limit;
			_totalLabel = label;
			updateTotalLabel();
			updateMediaLimit();
			applyLimits();
		});

	_mediaSlider = createLimitsSlider(
		container,
		kMediaSizeLimitsCount,
		tr::lng_local_storage_media_limit_title(tr::now),
		MediaSizeLimit,
		_mediaSizeLimit,
		[=](not_null<Ui::LabelSimple*> label, int64 limit) {
			_mediaSizeLimit = limit;
			_mediaLabel = label;
			updateMediaLabel();
			updateTotalLimit();
			applyLimits();
		});

	createLimitsSlider(
		container,
		kTimeLimitsCount,
		tr::lng_local_storage_time_limit_title(tr::now),
		TimeLimit,
		LimitToValue(_timeLimit),
		[=](not_null<Ui::LabelSimple*> label, size_type limit) {
			_timeLimit = ValueToLimit(limit);
			label->setText(TimeLimitText(_timeLimit));
			applyLimits();
		});
}

void LocalStorage::applyLimits() {
	const auto &settings = _session->local().cacheSettings();
	const auto &settingsBig = _session->local().cacheBigFileSettings();
	const auto sizeLimit = _totalSizeLimit - _mediaSizeLimit;
	const auto changed = (settings.totalSizeLimit != sizeLimit)
		|| (settingsBig.totalSizeLimit != _mediaSizeLimit)
		|| (settings.totalTimeLimit != _timeLimit)
		|| (settingsBig.totalTimeLimit != _timeLimit);
	if (!changed) {
		return;
	}
	auto update = Storage::Cache::Database::SettingsUpdate();
	update.totalSizeLimit = sizeLimit;
	update.totalTimeLimit = _timeLimit;
	auto updateBig = Storage::Cache::Database::SettingsUpdate();
	updateBig.totalSizeLimit = _mediaSizeLimit;
	updateBig.totalTimeLimit = _timeLimit;
	_session->local().updateCacheSettings(update, updateBig);
	_session->data().cache().updateSettings(update);
}

Type LocalStorageId() {
	return LocalStorage::Id();
}

} // namespace Settings
