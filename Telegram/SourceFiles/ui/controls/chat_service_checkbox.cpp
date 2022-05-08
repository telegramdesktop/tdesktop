/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/chat_service_checkbox.h"

#include "ui/widgets/checkbox.h"
#include "styles/style_layers.h"

#include <QCoreApplication>

namespace Ui {
namespace {

constexpr auto kAnimationTimerDelta = crl::time(7);

class ServiceCheck final : public AbstractCheckView {
public:
	ServiceCheck(const style::ServiceCheck &st, bool checked);

	QSize getSize() const override;
	void paint(
		Painter &p,
		int left,
		int top,
		int outerWidth) override;
	QImage prepareRippleMask() const override;
	bool checkRippleStartPosition(QPoint position) const override;

private:
	class Generator {
	public:
		Generator();

		void paintFrame(
			Painter &p,
			int left,
			int top,
			not_null<const style::ServiceCheck*> st,
			float64 toggled);
		void invalidate();

	private:
		struct Frames {
			QImage image;
			std::vector<bool> ready;
		};

		not_null<Frames*> framesForStyle(
			not_null<const style::ServiceCheck*> st);
		static void FillFrame(
			QImage &image,
			not_null<const style::ServiceCheck*> st,
			int index,
			int count);
		static void PaintFillingFrame(
			Painter &p,
			not_null<const style::ServiceCheck*> st,
			float64 progress);
		static void PaintCheckingFrame(
			Painter &p,
			not_null<const style::ServiceCheck*> st,
			float64 progress);

		base::flat_map<not_null<const style::ServiceCheck*>, Frames> _data;
		rpl::lifetime _lifetime;

	};
	static Generator &Frames();

	const style::ServiceCheck &_st;

};

ServiceCheck::Generator::Generator() {
	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		invalidate();
	}, _lifetime);
}

auto ServiceCheck::Generator::framesForStyle(
		not_null<const style::ServiceCheck*> st) -> not_null<Frames*> {
	if (const auto i = _data.find(st); i != _data.end()) {
		return &i->second;
	}
	const auto result = &_data.emplace(st, Frames()).first->second;
	const auto size = st->diameter;
	const auto count = (st->duration / kAnimationTimerDelta) + 2;
	result->image = QImage(
		QSize(count * size, size) * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	result->image.fill(Qt::transparent);
	result->image.setDevicePixelRatio(style::DevicePixelRatio());
	result->ready.resize(count);
	return result;
}

void ServiceCheck::Generator::FillFrame(
		QImage &image,
		not_null<const style::ServiceCheck*> st,
		int index,
		int count) {
	Expects(count > 1);
	Expects(index >= 0 && index < count);

	Painter p(&image);
	PainterHighQualityEnabler hq(p);

	p.translate(index * st->diameter, 0);
	const auto progress = index / float64(count - 1);
	if (progress > 0.5) {
		PaintCheckingFrame(p, st, (progress - 0.5) * 2);
	} else {
		PaintFillingFrame(p, st, progress * 2);
	}
}

void ServiceCheck::Generator::PaintFillingFrame(
		Painter &p,
		not_null<const style::ServiceCheck*> st,
		float64 progress) {
	const auto shift = progress * st->shift;
	p.setBrush(st->color);
	p.setPen(Qt::NoPen);
	p.drawEllipse(QRectF(
		shift,
		shift,
		st->diameter - 2 * shift,
		st->diameter - 2 * shift));
	if (progress < 1.) {
		const auto remove = progress * (st->diameter / 2. - st->thickness);
		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.setPen(Qt::NoPen);
		p.setBrush(Qt::transparent);
		p.drawEllipse(QRectF(
			st->thickness + remove,
			st->thickness + remove,
			st->diameter - 2 * (st->thickness + remove),
			st->diameter - 2 * (st->thickness + remove)));
	}
}

void ServiceCheck::Generator::PaintCheckingFrame(
		Painter &p,
		not_null<const style::ServiceCheck*> st,
		float64 progress) {
	const auto shift = (1. - progress) * st->shift;
	p.setBrush(st->color);
	p.setPen(Qt::NoPen);
	p.drawEllipse(QRectF(
		shift,
		shift,
		st->diameter - 2 * shift,
		st->diameter - 2 * shift));
	if (progress > 0.) {
		const auto tip = QPointF(st->tip.x(), st->tip.y());
		const auto left = tip - QPointF(st->small, st->small) * progress;
		const auto right = tip - QPointF(-st->large, st->large) * progress;

		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.setBrush(Qt::NoBrush);
		auto pen = QPen(Qt::transparent);
		pen.setWidth(st->stroke);
		pen.setCapStyle(Qt::RoundCap);
		pen.setJoinStyle(Qt::RoundJoin);
		p.setPen(pen);
		auto path = QPainterPath();
		path.moveTo(left);
		path.lineTo(tip);
		path.lineTo(right);
		p.drawPath(path);
	}
}

void ServiceCheck::Generator::paintFrame(
		Painter &p,
		int left,
		int top,
		not_null<const style::ServiceCheck*> st,
		float64 toggled) {
	const auto frames = framesForStyle(st);
	auto &image = frames->image;
	const auto count = int(frames->ready.size());
	const auto index = int(base::SafeRound(toggled * (count - 1)));
	Assert(index >= 0 && index < count);
	if (!frames->ready[index]) {
		frames->ready[index] = true;
		FillFrame(image, st, index, count);
	}
	const auto size = st->diameter;
	const auto part = size * style::DevicePixelRatio();
	p.drawImage(
		QPoint(left, top),
		image,
		QRect(index * part, 0, part, part));
}

void ServiceCheck::Generator::invalidate() {
	_data.clear();
}

ServiceCheck::Generator &ServiceCheck::Frames() {
	static const auto Instance = Ui::CreateChild<Generator>(
		QCoreApplication::instance());
	return *Instance;
}

ServiceCheck::ServiceCheck(
	const style::ServiceCheck &st,
	bool checked)
: AbstractCheckView(st.duration, checked, nullptr)
, _st(st) {
}

QSize ServiceCheck::getSize() const {
	const auto inner = QRect(0, 0, _st.diameter, _st.diameter);
	return inner.marginsAdded(_st.margin).size();
}

void ServiceCheck::paint(
		Painter &p,
		int left,
		int top,
		int outerWidth) {
	Frames().paintFrame(
		p,
		left + _st.margin.left(),
		top + _st.margin.top(),
		&_st,
		currentAnimationValue());
}

QImage ServiceCheck::prepareRippleMask() const {
	return QImage();
}

bool ServiceCheck::checkRippleStartPosition(QPoint position) const {
	return false;
}

void SetupBackground(not_null<Checkbox*> checkbox, Fn<QColor()> bg) {
	checkbox->paintRequest(
	) | rpl::map(
		bg ? bg : [] { return st::msgServiceBg->c; }
	) | rpl::filter([=](const QColor &color) {
		return color.alpha() > 0;
	}) | rpl::start_with_next([=](const QColor &color) {
		Painter p(checkbox);
		PainterHighQualityEnabler hq(p);
		p.setPen(Qt::NoPen);
		p.setBrush(color);
		const auto radius = checkbox->height() / 2.;
		p.drawRoundedRect(checkbox->rect(), radius, radius);
	}, checkbox->lifetime());
}

} // namespace

[[nodiscard]] object_ptr<Checkbox> MakeChatServiceCheckbox(
		QWidget *parent,
		const QString &text,
		const style::Checkbox &st,
		const style::ServiceCheck &stCheck,
		bool checked,
		Fn<QColor()> bg) {
	auto result = object_ptr<Checkbox>(
		parent,
		text,
		st,
		std::make_unique<ServiceCheck>(stCheck, checked));
	SetupBackground(result.data(), std::move(bg));
	return result;
}

} // namespace Ui
