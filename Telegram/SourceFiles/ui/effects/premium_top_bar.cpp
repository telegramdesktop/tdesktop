/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/premium_top_bar.h"

#include "lottie/lottie_icon.h"
#include "ui/color_contrast.h"
#include "ui/painter.h"
#include "ui/effects/premium_graphics.h"
#include "ui/effects/premium_star.h"
#include "ui/effects/premium_star_particles.h"
#include "ui/effects/premium_diamond.h"
#include "ui/effects/premium_coin.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/rect.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_premium.h"
#include "styles/style_boxes.h"

namespace Ui::Premium {
namespace {

constexpr auto kBodyAnimationPart = 0.90;
constexpr auto kTitleAdditionalScale = 0.15;
constexpr auto kMinAcceptableContrast = 4.5; // 1.14;

constexpr auto kStar3dScale = 2.;
constexpr auto kDiamond3dScale = 1.58;
constexpr auto kCoin3dScale = 1.85;

constexpr auto kStarParticlesFieldScale = 3.;

[[nodiscard]] QImage ScaleTo(QImage image) {
	using namespace style;
	const auto size = image.size();
	const auto scale = DevicePixelRatio() * Scale() / 300.;
	const auto scaled = QSize(
		int(base::SafeRound(size.width() * scale)),
		int(base::SafeRound(size.height() * scale)));
	image = image.scaled(
		scaled,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
	image.setDevicePixelRatio(DevicePixelRatio());
	return image;
}

} // namespace

TopBarAbstract::TopBarAbstract(
	QWidget *parent,
	const style::PremiumCover &st)
: RpWidget(parent)
, _st(st) {
}

void TopBarAbstract::setRoundEdges(bool value) {
	_roundEdges = value;
	update();
}

void TopBarAbstract::paintEdges(QPainter &p, const QBrush &brush) const {
	const auto r = rect();
	if (_roundEdges) {
		PainterHighQualityEnabler hq(p);
		const auto radius = st::boxRadius;
		p.setPen(Qt::NoPen);
		p.setBrush(brush);
		p.drawRoundedRect(
			r + QMargins{ 0, 0, 0, radius + 1 },
			radius,
			radius);
	} else {
		p.fillRect(r, brush);
	}
}

void TopBarAbstract::paintEdges(QPainter &p) const {
	paintEdges(p, st().bg);
	if (isDark() && st().additionalShadowForDarkThemes) {
		paintEdges(p, st::shadowFg);
		paintEdges(p, st::shadowFg);
	}
}

QRectF TopBarAbstract::starRect(
		float64 topProgress,
		float64 sizeProgress) const {
	const auto starSize = _st.starSize * sizeProgress;
	return QRectF(
		QPointF(
			(width() - starSize.width()) / 2,
			_st.starTopSkip * topProgress),
		starSize);
};

bool TopBarAbstract::isDark() const {
	return _isDark;
}

void TopBarAbstract::computeIsDark() {
	const auto contrast = CountContrast(
		st().bg->c,
		st::premiumButtonFg->c);
	_isDark = (contrast > kMinAcceptableContrast);
}

TopBar::TopBar(
	not_null<QWidget*> parent,
	const style::PremiumCover &st,
	TopBarDescriptor &&descriptor)
: TopBarAbstract(parent, st)
, _light(descriptor.light)
, _logo(descriptor.logo)
, _titleFont(st.titleFont)
, _titlePadding(st.titlePadding)
, _aboutMaxWidth(st.aboutMaxWidth)
, _about(this, std::move(descriptor.about), st.about)
, _ministars(
		this,
		descriptor.optimizeMinistars,
		(_logo == u"diamond"_q)
			? MiniStarsType::DiamondStars
			: MiniStarsType::BiStars) {
	if (descriptor.use3dStar && Star::Supported()) {
		_star3d = CreateChild<Star>(this);
		_star3dGolden = descriptor.star3dGolden;
		if (_star3dGolden) {
			_star3d->setGolden(true);
		}
		_particles3d = std::make_unique<StarParticles>([=](
				const QRect &area) {
			update(area);
		});
		_star3d->flungStrength() | rpl::on_next([=](float64 strength) {
			_particles3d->fling(strength);
		}, lifetime());
		if (descriptor.showFinished) {
			std::move(
				descriptor.showFinished
			) | rpl::on_next([=] {
				if (_star3d) {
					_star3d->startEnter();
				}
			}, lifetime());
		}
	} else if (descriptor.use3dDiamond && Diamond::Supported()) {
		_diamond3d = CreateChild<Diamond>(this);
		_particles3d = std::make_unique<StarParticles>([=](
				const QRect &area) {
			update(area);
		});
		_diamond3d->flungStrength() | rpl::on_next([=](float64 strength) {
			_particles3d->fling(strength);
		}, lifetime());
	} else if (descriptor.use3dCoin && Coin::Supported()) {
		_coin3d = CreateChild<Coin>(this);
		_particles3d = std::make_unique<StarParticles>([=](
				const QRect &area) {
			update(area);
		});
		_coin3d->flungStrength() | rpl::on_next([=](float64 strength) {
			_particles3d->fling(strength);
		}, lifetime());
	}

	std::move(
		descriptor.title
	) | rpl::on_next([=](QString text) {
		_titlePath = QPainterPath();
		_titlePath.addText(0, _titleFont->ascent, _titleFont, text);
		update();
	}, lifetime());

	if (const auto other = descriptor.clickContextOther) {
		_about->setClickHandlerFilter([=](
				const ClickHandlerPtr &handler,
				Qt::MouseButton button) {
			ActivateClickHandler(_about, handler, {
				button,
				other()
			});
			return false;
		});
	}

	rpl::single() | rpl::then(
		style::PaletteChanged()
	) | rpl::on_next([=, starSize = st.starSize] {
		TopBarAbstract::computeIsDark();

		if (_logo == u"dollar"_q) {
			_dollar = ScaleTo(QImage(u":/gui/art/business_logo.png"_q));
			_ministars.setColorOverride(
				QGradientStops{{ 0, st::premiumButtonFg->c }});
		} else if (_logo == u"affiliate"_q) {
			_dollar = ScaleTo(QImage(u":/gui/art/affiliate_logo.png"_q));
			_ministars.setColorOverride(descriptor.gradientStops);
		} else if (_logo == u"diamond"_q) {
			if (!_diamond3d) {
				_lottie = Lottie::MakeIcon({
					.name = u"diamond"_q,
					.sizeOverride = starSize,
				});
				_lottie->animate(
					[=] {
						update(_starRect.toRect() + Margins(st::lineWidth));
					},
					0,
					_lottie->framesCount() - 1);
			}
			_ministars.setColorOverride(
				QGradientStops{{ 0, st::windowActiveTextFg->c }});
		} else if (!_light && !TopBarAbstract::isDark()) {
			_star.load(Svg());
			_ministars.setColorOverride(
				QGradientStops{{ 0, st::premiumButtonFg->c }});
		} else {
			_star.load(ColorizedSvg(descriptor.gradientStops
				? (*descriptor.gradientStops)
				: Ui::Premium::ButtonGradientStops()));
			_ministars.setColorOverride(descriptor.gradientStops);
		}
		if (_star3d) {
			if (_star3dGolden) {
				_star3d->setColors(
					QColor(0xFE, 0xC8, 0x46),
					QColor(0xEC, 0x92, 0x0A));
				_particles3d->setColors(
					QColor(0xFA, 0x54, 0x16),
					QColor(0xFF, 0xC8, 0x37));
				_particles3d->setGlyph(StarParticles::Glyph::Star);
			} else if (!_light && !TopBarAbstract::isDark()) {
				_star3d->setColors(
					QColor(255, 255, 255),
					QColor(0xE3, 0xEC, 0xFA));
				_particles3d->setColor(QColor(255, 255, 255));
			} else {
				const auto stops = descriptor.gradientStops
					? (*descriptor.gradientStops)
					: Ui::Premium::ButtonGradientStops();
				const auto middle = stops[stops.size() / 2].second;
				_star3d->setColors(stops.front().second, middle);
				_particles3d->setColor(middle);
			}
		}
		if (_diamond3d) {
			_diamond3d->setNight(TopBarAbstract::isDark());
			_particles3d->setColor(st::windowActiveTextFg->c);
		}
		if (_coin3d) {
			_coin3d->setNight(TopBarAbstract::isDark());
			if (TopBarAbstract::isDark()) {
				const auto stops = descriptor.gradientStops
					? (*descriptor.gradientStops)
					: Ui::Premium::ButtonGradientStops();
				_particles3d->setColors(
					stops.front().second,
					stops.back().second);
			} else {
				_particles3d->setColors(
					QColor(255, 255, 255),
					QColor(0xC8, 0xC8, 0xD0));
			}
			_particles3d->setGlyph(StarParticles::Glyph::Dollar);
		}
		auto event = QResizeEvent(size(), size());
		resizeEvent(&event);
	}, lifetime());

	if (_light) {
		const auto smallTopShadow = CreateChild<FadeShadow>(this);
		smallTopShadow->setDuration(st::fadeWrapDuration);
		sizeValue(
		) | rpl::on_next([=](QSize size) {
			smallTopShadow->resizeToWidth(size.width());
			smallTopShadow->moveToLeft(
				0,
				height() - smallTopShadow->height());
			const auto shown = (minimumHeight() * 2 > size.height());
			smallTopShadow->toggle(shown, anim::type::normal);
		}, lifetime());
	}
}

TopBar::~TopBar() = default;

void TopBar::setPaused(bool paused) {
	_ministars.setPaused(paused);
	if (_star3d) {
		_star3d->setPaused(paused);
	}
	if (_diamond3d) {
		_diamond3d->setPaused(paused);
	}
	if (_coin3d) {
		_coin3d->setPaused(paused);
	}
	if (_particles3d) {
		_particles3d->setPaused(paused);
	}
}

void TopBar::setTextPosition(int x, int y) {
	_titlePosition = { x, y };
}

rpl::producer<int> TopBar::additionalHeight() const {
	return _about->heightValue(
	) | rpl::map([l = st().about.style.lineHeight](int height) {
		return std::max(height - l, 0);
	});
}

void TopBar::resizeEvent(QResizeEvent *e) {
	const auto max = maximumHeight();
	const auto min = minimumHeight();
	const auto progress = (max > min)
		? ((e->size().height() - min) / float64(max - min))
		: 1.;
	_progress.top = 1.
		- std::clamp((1. - progress) / kBodyAnimationPart, 0., 1.);
	_progress.body = _progress.top;
	_progress.title = 1. - progress;
	_progress.scaleTitle = 1. + kTitleAdditionalScale * progress;

	_ministars.setCenter(starRect(_progress.top, 1.).toRect());

	_starRect = starRect(_progress.top, _progress.body);

	if (_star3d) {
		auto enlarged = Rect(_starRect.size() * kStar3dScale);
		enlarged.moveCenter(rect::center(_starRect));
		_star3d->setGeometry(enlarged.toRect());
		_star3d->setShownProgress(_progress.body);
	}
	if (_diamond3d) {
		auto enlarged = Rect(_starRect.size() * kDiamond3dScale);
		enlarged.moveCenter(rect::center(_starRect));
		_diamond3d->setGeometry(enlarged.toRect());
		_diamond3d->setShownProgress(_progress.body);
	}
	if (_coin3d) {
		auto enlarged = Rect(_starRect.size() * kCoin3dScale);
		enlarged.moveCenter(rect::center(_starRect));
		_coin3d->setGeometry(enlarged.toRect());
		_coin3d->setShownProgress(_progress.body);
	}

	const auto &padding = st::boxRowPadding;
	const auto availableWidth = width() - padding.left() - padding.right();
	const auto titleTop = rect::bottom(_starRect)
		+ _titlePadding.top();
	const auto titlePathRect = _titlePath.boundingRect();
	const auto aboutTop = titleTop
		+ titlePathRect.height()
		+ _titlePadding.bottom();
	_about->resizeToWidth(_aboutMaxWidth ? _aboutMaxWidth : availableWidth);
	_about->moveToLeft(
		padding.left()
			+ (_aboutMaxWidth ? (availableWidth - _about->width()) / 2 : 0),
		aboutTop);
	_about->setOpacity(_progress.body);

	RpWidget::resizeEvent(e);
}

void TopBar::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	const auto r = rect();

	if (!_light && !TopBarAbstract::isDark()) {
		const auto gradientPointTop = r.height() / 3. * 2.;
		auto gradient = QLinearGradient(
			QPointF(0, gradientPointTop),
			QPointF(r.width(), r.height() - gradientPointTop));
		gradient.setStops(ButtonGradientStops());

		TopBarAbstract::paintEdges(p, gradient);
	} else {
		TopBarAbstract::paintEdges(p);
	}

	if (_particles3d) {
		if (_progress.top) {
			auto field = Rect(_starRect.size() * kStarParticlesFieldScale);
			field.moveCenter(rect::center(_starRect));
			p.setOpacity(_progress.body);
			_particles3d->paint(p, field);
			p.setOpacity(1.);
		}
	} else {
		p.setOpacity(_progress.body);
		p.translate(rect::center(_starRect));
		p.scale(_progress.body, _progress.body);
		p.translate(-rect::center(_starRect));
		if (_progress.top) {
			_ministars.paint(p);
		}
		if (_lottie) {
			_lottie->paint(
				p,
				_starRect.left()
					+ (_starRect.width() - _lottie->width()) / 2
					- st::lineWidth * 6,
				_starRect.top());
			if (!_lottie->animating() && _lottie->frameIndex() > 0) {
				_lottie->animate(
					[=] {
						update(_starRect.toRect() + Margins(st::lineWidth));
					},
					0,
					_lottie->framesCount() - 1);
			}
		}
		p.resetTransform();
	}


	if (!_dollar.isNull() && !_coin3d) {
		auto hq = PainterHighQualityEnabler(p);
		p.drawImage(_starRect, _dollar);
	} else if (!_star3d && !_diamond3d && !_coin3d) {
		_star.render(&p, _starRect);
	}

	const auto color = _light ? st().titleFg : st::premiumButtonFg;
	p.setPen(color);

	const auto titlePathRect = _titlePath.boundingRect();

	// Title.
	PainterHighQualityEnabler hq(p);
	p.setOpacity(1.);
	p.setFont(_titleFont);
	const auto fullStarRect = starRect(1., 1.);
	const auto fullTitleTop = fullStarRect.top()
		+ fullStarRect.height()
		+ _titlePadding.top();
	p.translate(
		anim::interpolate(
			(width() - titlePathRect.width()) / 2,
			_titlePosition.x(),
			_progress.title),
		anim::interpolate(fullTitleTop, _titlePosition.y(), _progress.title));

	p.translate(rect::center(titlePathRect));
	p.scale(_progress.scaleTitle, _progress.scaleTitle);
	p.translate(-rect::center(titlePathRect));
	p.fillPath(_titlePath, color);
}

} // namespace Ui::Premium
