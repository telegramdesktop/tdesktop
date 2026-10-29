/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_music_button.h"

#include "ui/effects/animation_value.h"
#include "ui/color_contrast.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"

#include "styles/style_info.h"
#include "styles/style_info_profile_top_bar.h"

namespace Info::Profile {
namespace {

[[nodiscard]] float64 FigureHeight(const style::font &font) {
	return -font->metrics().tightBoundingRect(u"0123456789"_q).top();
}

} // namespace

PillColors ComputePillColors(std::optional<QColor> edge) {
	if (!edge) {
		return {
			.fill = anim::with_alpha(
				st::boxBg->c,
				1. - st::infoProfileTopBarActionButtonBgOpacity),
			.primary = st::windowBoldFg->c,
			.secondary = st::windowSubTextFg->c,
		};
	}
	const auto light = Ui::IsLightBackground(*edge);
	return {
		.fill = Ui::BlendColors(
			*edge,
			Qt::black,
			st::infoProfileTopBarActionButtonBgOpacity),
		.primary = light ? QColor(Qt::black) : st::groupCallMembersFg->c,
		.secondary = light
			? anim::with_alpha(
				QColor(Qt::black),
				st::groupCallVideoSubTextFg->c.alphaF())
			: st::groupCallVideoSubTextFg->c,
	};
}

void PaintPillBackground(
		QPainter &p,
		const QRect &rect,
		const PillColors &colors) {
	auto hq = PainterHighQualityEnabler(p);
	const auto radius = rect.height() / 2.;
	p.setPen(Qt::NoPen);
	p.setBrush(colors.fill);
	p.drawRoundedRect(rect, radius, radius);
}

int PillTextBaseline() {
	const auto figure = FigureHeight(st::infoProfileBalancePillAmountFont);
	return int(base::SafeRound(
		(st::infoProfileBalancePillHeight + figure) / 2.));
}

int PillArrowTop() {
	const auto figure = FigureHeight(st::infoProfileBalancePillAmountFont);
	const auto arrow = st::infoProfileBalancePillArrow.height();
	return int(base::SafeRound(PillTextBaseline() - (figure + arrow) / 2.));
}

MusicButton::MusicButton(QWidget *parent)
: AbstractButton(parent)
, _colors(ComputePillColors(std::nullopt))
, _noteSymbol(u"♫"_q + QChar(' '))
, _noteWidth(st::infoProfileBalancePillLabelFont->width(_noteSymbol))
, _baseline(PillTextBaseline())
, _arrowTop(PillArrowTop()) {
	setAttribute(Qt::WA_TransparentForMouseEvents);
	hide();

	style::PaletteChanged(
	) | rpl::on_next([=] {
		_colors = ComputePillColors(_overrideBg);
		_cache = QImage();
		update();
	}, lifetime());
}

MusicButton::~MusicButton() = default;

void MusicButton::updateData(MusicButtonData data) {
	const auto result = data.name.textWithEntities();
	const auto performerLength = result.entities.empty()
		? 0
		: int(result.entities.front().length());
	_performer.setText(
		st::infoProfileMusicPillPerformerStyle,
		result.text.mid(0, performerLength));
	_title.setText(
		st::infoProfileMusicPillTitleStyle,
		result.text.mid(performerLength, result.text.size()));
	_textSkip = (_performer.isEmpty() || _title.isEmpty())
		? 0
		: st::infoProfileBalancePillLabelFont->spacew;
	const auto chrome = chromeWidth();
	setMinimumWidth(chrome);
	refreshShares(width());
	_cache = QImage();
	setAccessibleName(result.text);
	setNaturalWidth(chrome + _performer.maxWidth() + _title.maxWidth());
	update();
}

void MusicButton::setOverrideBg(std::optional<QColor> color) {
	if (_overrideBg == color) {
		return;
	}
	_overrideBg = color;
	_colors = ComputePillColors(color);
	_cache = QImage();
	update();
}

void MusicButton::setOpacity(float64 opacity) {
	if (_opacity != opacity) {
		_opacity = opacity;
		update();
	}
	const auto transparent = (_opacity <= 0.);
	if (testAttribute(Qt::WA_TransparentForMouseEvents) != transparent) {
		setAttribute(Qt::WA_TransparentForMouseEvents, transparent);
		if (transparent) {
			clearState();
		}
	}
}

bool MusicButton::actionable() const {
	return !isHidden() && (_opacity > 0.);
}

int MusicButton::chromeWidth() const {
	const auto &padding = st::infoProfileBalancePillPadding;
	return padding.left()
		+ _noteWidth
		+ _textSkip
		+ st::infoProfileBalancePillArrowSkip
		+ st::infoProfileBalancePillArrow.width()
		+ padding.right();
}

void MusicButton::refreshShares(int newWidth) {
	const auto performer = _performer.maxWidth();
	const auto title = _title.maxWidth();
	const auto room = std::max(newWidth - chromeWidth(), 0);
	const auto wasPerformer = _performerShare;
	const auto wasTitle = _titleShare;
	if (performer + title <= room) {
		_performerShare = performer;
		_titleShare = title;
	} else {
		const auto ratio = title / float64(performer + title);
		_performerShare = int(room * (1. - ratio));
		_titleShare = room - _performerShare;
	}
	if (_performerShare != wasPerformer || _titleShare != wasTitle) {
		_cache = QImage();
	}
}

void MusicButton::paintEvent(QPaintEvent *e) {
	if (_opacity <= 0.) {
		return;
	}
	auto p = QPainter(this);
	p.setOpacity(_opacity);
	PaintPillBackground(p, rect(), _colors);
	validateCache();
	p.drawImage(0, 0, _cache);
}

void MusicButton::validateCache() {
	refreshShares(width());
	const auto ratio = style::DevicePixelRatio();
	if (!_cache.isNull()
		&& _cache.devicePixelRatio() == ratio
		&& _cache.size() == size() * ratio) {
		return;
	}
	_cache = QImage(size() * ratio, QImage::Format_ARGB32_Premultiplied);
	_cache.setDevicePixelRatio(ratio);
	_cache.fill(Qt::transparent);

	auto p = QPainter(&_cache);
	const auto &padding = st::infoProfileBalancePillPadding;
	auto left = padding.left();
	p.setPen(_colors.primary);
	p.setFont(st::infoProfileBalancePillLabelFont);
	p.drawText(left, _baseline, _noteSymbol);
	left += _noteWidth;
	paintRun(p, _performer, left, _performerShare, _colors.primary);
	left += _performerShare + _textSkip;
	paintRun(p, _title, left, _titleShare, _colors.secondary);

	const auto &arrow = st::infoProfileBalancePillArrow;
	arrow.paint(
		p,
		width() - padding.right() - arrow.width(),
		_arrowTop,
		width(),
		_colors.primary);
}

void MusicButton::paintRun(
		QPainter &p,
		const Ui::Text::String &text,
		int left,
		int share,
		const QColor &color) const {
	if (share <= 0 || text.isEmpty()) {
		return;
	}
	const auto &font = text.style()->font;
	const auto shift = font->ascent - font->fascent.toReal();
	p.setClipRect(left, 0, share, height());
	p.setPen(color);
	p.translate(0., shift);
	text.draw(p, {
		.position = { left, _baseline - font->ascent },
		.availableWidth = text.maxWidth(),
		.elisionLines = 1,
	});
	p.translate(0., -shift);
	if (text.maxWidth() > share) {
		const auto fade = std::min(st::infoProfileMusicPillFadeWidth, share);
		const auto till = left + share;
		auto gradient = QLinearGradient(till - fade, 0, till, 0);
		gradient.setStops({
			{ 0., QColor(255, 255, 255, 255) },
			{ 1., QColor(255, 255, 255, 0) },
		});
		p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
		p.fillRect(till - fade, 0, fade, height(), gradient);
		p.setCompositionMode(QPainter::CompositionMode_SourceOver);
	}
	p.setClipping(false);
}

int MusicButton::resizeGetHeight(int newWidth) {
	refreshShares(newWidth);
	return st::infoProfileBalancePillHeight;
}

} // namespace Info::Profile
