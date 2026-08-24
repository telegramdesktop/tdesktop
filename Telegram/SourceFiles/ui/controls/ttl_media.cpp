/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/ttl_media.h"

#include "base/unixtime.h"
#include "lang/lang_keys.h"
#include "ui/arc_angles.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/rp_widget.h"
#include "ui/widgets/labels.h"
#include "styles/style_ttl_media.h"

#include <QSvgRenderer>
#include <QtMath>

namespace Ui {
namespace {

class TtlCountdownBadge final : public RpWidget {
public:
	TtlCountdownBadge(QWidget *parent, TimeId destroyAt, crl::time ttl);

private:
	void paintEvent(QPaintEvent *e) override;

	TtlCountdown _countdown;

};

TtlCountdownBadge::TtlCountdownBadge(
	QWidget *parent,
	TimeId destroyAt,
	crl::time ttl)
: RpWidget(parent)
, _countdown([=] { update(); }) {
	_countdown.deadline = crl::now()
		+ (destroyAt - base::unixtime::now()) * crl::time(1000);
	_countdown.total = std::max(ttl, crl::time(1)) * crl::time(1000);
	_countdown.animation.start();
	const auto margin = st::ttlMediaBadgeMargin;
	resize(Size(st::ttlMediaBadgeSize + 2 * margin));
	setAttribute(Qt::WA_TransparentForMouseEvents);
	show();
}

void TtlCountdownBadge::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto circle = rect() - Margins(st::ttlMediaBadgeMargin);
	p.setPen(Qt::NoPen);
	p.setBrush(st::radialBg);
	p.drawEllipse(circle);
	const auto line = st::ttlMediaBadgeLine;
	PaintTtlCountdown(
		p,
		circle - Margins(2 * line),
		line,
		&_countdown,
		st::radialFg,
		!window()->isActiveWindow());
	const auto left = std::max(
		_countdown.deadline - crl::now(),
		crl::time(0));
	p.setPen(st::radialFg);
	p.setFont(st::normalFont);
	const auto seconds = int((left + 999) / 1000);
	p.drawText(
		circle,
		Qt::AlignCenter,
		tr::lng_seconds_tiny(tr::now, lt_count, seconds));
}

} // namespace

TtlCountdown::TtlCountdown(Fn<void()> repaint)
: animation(std::move(repaint)) {
}

std::unique_ptr<TtlCountdown> MakeTtlCountdown(
		TimeId destroyAt,
		crl::time ttl,
		Fn<void()> repaint) {
	if (destroyAt <= 0) {
		return nullptr;
	}
	auto result = std::make_unique<TtlCountdown>(std::move(repaint));
	result->destroyAt = destroyAt;
	result->deadline = crl::now()
		+ (destroyAt - base::unixtime::now()) * crl::time(1000);
	result->total = std::max(ttl, crl::time(1)) * crl::time(1000);
	result->animation.start();
	return result;
}

void PaintTtlCountdown(
		QPainter &p,
		QRect inner,
		int line,
		not_null<TtlCountdown*> countdown,
		const style::color &color,
		bool paused) {
	const auto left = countdown->deadline - crl::now();
	if (left <= 0) {
		countdown->animation.stop();
		return;
	}
	const auto length = int(base::SafeRound(std::clamp(
		left / float64(countdown->total),
		0.,
		1.) * arc::kFullLength));
	if (!length) {
		return;
	}
	auto hq = PainterHighQualityEnabler(p);
	auto pen = color->p;
	pen.setWidthF(line);
	pen.setCapStyle(Qt::RoundCap);
	p.setPen(pen);
	p.drawArc(inner, arc::kQuarterLength, length);

	if (paused) {
		return;
	}
	const auto angle = ((arc::kQuarterLength + length) / 16.)
		* M_PI / 180.;
	const auto cosa = std::cos(angle);
	const auto sina = std::sin(angle);
	const auto rect = QRectF(inner);
	const auto radius = rect.width() / 2.;
	const auto center = rect.center();
	countdown->particles.paint(
		p,
		QPointF(center.x() + radius * cosa, center.y() - radius * sina),
		QPointF(-sina, -cosa),
		color->c,
		1.);
}

void PaintTtlFireIcon(QPainter &p, QRect inner, QImage &cache) {
	const auto ratio = style::DevicePixelRatio();
	if (cache.size() != inner.size() * ratio) {
		cache = QImage(
			inner.size() * ratio,
			QImage::Format_ARGB32_Premultiplied);
		cache.setDevicePixelRatio(ratio);
		cache.fill(Qt::transparent);
		auto q = QPainter(&cache);
		auto svg = QSvgRenderer(u":/gui/ttl/video_message_icon.svg"_q);
		svg.render(&q, Rect(inner.size()) - Margins(inner.width() / 4));
	}
	p.drawImage(inner.topLeft(), cache);
}

std::unique_ptr<RpWidget> MakeTtlCountdownBadge(
		QWidget *parent,
		TimeId destroyAt,
		crl::time ttl) {
	return std::make_unique<TtlCountdownBadge>(parent, destroyAt, ttl);
}

std::unique_ptr<RpWidget> MakeTtlOnceBadge(QWidget *parent) {
	auto result = std::make_unique<RpWidget>(parent);
	const auto raw = result.get();
	const auto margin = st::ttlMediaBadgeMargin;
	raw->resize(Size(st::ttlMediaBadgeSize + 2 * margin));
	raw->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(raw);
		st::ttlMediaOnceIcon.paintInCenter(
			p,
			raw->rect() - Margins(margin));
	}, raw->lifetime());
	raw->setAttribute(Qt::WA_TransparentForMouseEvents);
	raw->show();
	return result;
}

object_ptr<RpWidget> MakeTtlTooltipContent(
		QWidget *parent,
		rpl::producer<TextWithEntities> text) {
	auto result = object_ptr<RpWidget>(parent);
	const auto raw = result.data();
	const auto label = CreateChild<FlatLabel>(
		raw,
		std::move(text),
		st::ttlMediaTooltipLabel);
	label->resizeToWidth(st::ttlMediaTooltipMaxWidth);
	const auto icon = st::ttlMediaTooltipIconSize;
	const auto skip = st::ttlMediaTooltipIconSkip;
	const auto height = std::max(label->height(), icon);
	label->move(icon + skip, (height - label->height()) / 2);
	raw->resize(icon + skip + label->width(), height);
	const auto cache = raw->lifetime().make_state<QImage>();
	raw->paintRequest() | rpl::on_next([=] {
		const auto ratio = style::DevicePixelRatio();
		const auto size = Size(icon);
		if (cache->size() != size * ratio) {
			*cache = QImage(
				size * ratio,
				QImage::Format_ARGB32_Premultiplied);
			cache->setDevicePixelRatio(ratio);
			cache->fill(Qt::transparent);
			auto q = QPainter(cache);
			auto svg = QSvgRenderer(u":/gui/ttl/video_message_icon.svg"_q);
			svg.render(&q, Rect(size));
		}
		auto p = QPainter(raw);
		p.drawImage(QPoint(0, (raw->height() - icon) / 2), *cache);
	}, raw->lifetime());
	return result;
}

} // namespace Ui
