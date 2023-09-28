/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/premium_top_bar.h"

#include "ui/color_contrast.h"
#include "ui/painter.h"
#include "ui/effects/premium_graphics.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/fade_wrap.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_premium.h"

#include <QtCore/QFile>

namespace Ui::Premium {
namespace {

constexpr auto kBodyAnimationPart = 0.90;
constexpr auto kTitleAdditionalScale = 0.15;
constexpr auto kMinAcceptableContrast = 4.5; // 1.14;

} // namespace

QString Svg() {
	return u":/gui/icons/settings/star.svg"_q;
}

QByteArray ColorizedSvg() {
	auto f = QFile(Svg());
	if (!f.open(QIODevice::ReadOnly)) {
		return QByteArray();
	}
	auto content = QString::fromUtf8(f.readAll());
	auto stops = [] {
		auto s = QString();
		for (const auto &stop : Ui::Premium::ButtonGradientStops()) {
			s += QString("<stop offset='%1' stop-color='%2'/>")
				.arg(QString::number(stop.first), stop.second.name());
		}
		return s;
	}();
	const auto color = QString("<linearGradient id='Gradient2' "
		"x1='%1' x2='%2' y1='%3' y2='%4'>%5</linearGradient>")
		.arg(0)
		.arg(1)
		.arg(1)
		.arg(0)
		.arg(std::move(stops));
	content.replace(u"gradientPlaceholder"_q, color);
	content.replace(u"#fff"_q, u"url(#Gradient2)"_q);
	f.close();
	return content.toUtf8();
}

QImage GenerateStarForLightTopBar(QRectF rect) {
	auto svg = QSvgRenderer(Ui::Premium::Svg());

	const auto size = rect.size().toSize();
	auto frame = QImage(
		size * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	frame.setDevicePixelRatio(style::DevicePixelRatio());

	auto mask = frame;
	mask.fill(Qt::transparent);
	{
		auto p = QPainter(&mask);
		auto gradient = QLinearGradient(
			0,
			size.height(),
			size.width(),
			0);
		gradient.setStops(Ui::Premium::ButtonGradientStops());
		p.setPen(Qt::NoPen);
		p.setBrush(gradient);
		p.drawRect(0, 0, size.width(), size.height());
	}
	frame.fill(Qt::transparent);
	{
		auto q = QPainter(&frame);
		svg.render(&q, QRect(QPoint(), size));
		q.setCompositionMode(QPainter::CompositionMode_SourceIn);
		q.drawImage(0, 0, mask);
	}
	return frame;
}

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
	paintEdges(p, st::boxBg);
	if (isDark()) {
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
		st::boxBg->c,
		st::premiumButtonFg->c);
	_isDark = (contrast > kMinAcceptableContrast);
}

TopBar::TopBar(
	not_null<QWidget*> parent,
	const style::PremiumCover &st,
	Fn<QVariant()> clickContextOther,
	rpl::producer<QString> title,
	rpl::producer<TextWithEntities> about,
	bool light)
: TopBarAbstract(parent, st)
, _light(light)
, _titleFont(st.titleFont)
, _titlePadding(st.titlePadding)
, _about(this, std::move(about), st.about)
, _ministars(this) {
	std::move(
		title
	) | rpl::start_with_next([=](QString text) {
		_titlePath = QPainterPath();
		_titlePath.addText(0, _titleFont->ascent, _titleFont, text);
		update();
	}, lifetime());

	if (clickContextOther) {
		_about->setClickHandlerFilter([=](
				const ClickHandlerPtr &handler,
				Qt::MouseButton button) {
			ActivateClickHandler(_about, handler, {
				button,
				clickContextOther()
			});
			return false;
		});
	}

	rpl::single() | rpl::then(
		style::PaletteChanged()
	) | rpl::start_with_next([=] {
		TopBarAbstract::computeIsDark();

		if (!_light && !TopBarAbstract::isDark()) {
			_star.load(Svg());
			_ministars.setColorOverride(st::premiumButtonFg->c);
		} else {
			_star.load(ColorizedSvg());
			_ministars.setColorOverride(std::nullopt);
		}
		auto event = QResizeEvent(size(), size());
		resizeEvent(&event);
	}, lifetime());

	if (_light) {
		const auto smallTopShadow = CreateChild<FadeShadow>(this);
		smallTopShadow->setDuration(st::fadeWrapDuration);
		sizeValue(
		) | rpl::start_with_next([=](QSize size) {
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
}

void TopBar::setTextPosition(int x, int y) {
	_titlePosition = { x, y };
}

rpl::producer<int> TopBar::additionalHeight() const {
	return _about->heightValue(
	) | rpl::map([l = st().about.style.lineHeight](int height) {
		return std::max(height - l * 2, 0);
	});
}

void TopBar::resizeEvent(QResizeEvent *e) {
	const auto progress = (e->size().height() - minimumHeight())
		/ float64(maximumHeight() - minimumHeight());
	_progress.top = 1. -
		std::clamp(
			(1. - progress) / kBodyAnimationPart,
			0.,
			1.);
	_progress.body = _progress.top;
	_progress.title = 1. - progress;
	_progress.scaleTitle = 1. + kTitleAdditionalScale * progress;

	_ministars.setCenter(starRect(_progress.top, 1.).toRect());

	_starRect = starRect(_progress.top, _progress.body);

	const auto &padding = st::boxRowPadding;
	const auto availableWidth = width() - padding.left() - padding.right();
	const auto titleTop = _starRect.top()
		+ _starRect.height()
		+ _titlePadding.top();
	const auto titlePathRect = _titlePath.boundingRect();
	const auto aboutTop = titleTop
		+ titlePathRect.height()
		+ _titlePadding.bottom();
	_about->resizeToWidth(availableWidth);
	_about->moveToLeft(padding.left(), aboutTop);
	_about->setOpacity(_progress.body);

	RpWidget::resizeEvent(e);
}

void TopBar::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	p.fillRect(e->rect(), Qt::transparent);

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

	p.setOpacity(_progress.body);
	p.translate(_starRect.center());
	p.scale(_progress.body, _progress.body);
	p.translate(-_starRect.center());
	if (_progress.top) {
		_ministars.paint(p);
	}
	p.resetTransform();

	_star.render(&p, _starRect);

	const auto color = _light
		? st::settingsPremiumUserTitle.textFg
		: st::premiumButtonFg;
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

	p.translate(titlePathRect.center());
	p.scale(_progress.scaleTitle, _progress.scaleTitle);
	p.translate(-titlePathRect.center());
	p.fillPath(_titlePath, color);
}

} // namespace Ui::Premium
