/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/premium_stars_colored.h"

#include "ui/effects/premium_graphics.h" // GiftGradientStops.
#include "ui/text/text_custom_emoji.h"
#include "ui/rp_widget.h"

namespace Ui::Premium {

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
	class Emoji final : public Text::CustomEmoji {
	public:
		Emoji(
			QStringView entityData,
			QColor centerColor,
			QColor edgeColor,
			std::unique_ptr<Ui::Text::CustomEmoji> inner,
			Fn<void()> update,
			int size)
		: _entityData(entityData.toString())
		, _stars([=](QRect) { update(); }, MiniStars::Type::SlowStars)
		, _centerColor(centerColor)
		, _edgeColor(edgeColor)
		, _inner(std::move(inner))
		, _size(size) {
			_stars.setColorOverride(QGradientStops{
				{ 0., edgeColor },
				{ 1., centerColor },
			});
			_stars.setSize(QSize(size, size));
		}

		int width() override {
			return _inner->width();
		}

		QString entityData() override {
			return _entityData;
		}

		void paint(QPainter &p, const Context &context) override {
			_stars.setPosition(context.position);
			_stars.paint(p);

			_inner->paint(p, context);
		}

		void unload() override {
			_inner->unload();
		}

		bool ready() override {
			return _inner->ready();
		}

		bool readyInDefaultState() override {
			return _inner->readyInDefaultState();
		}

	private:
		QString _entityData;
		ColoredMiniStars _stars;
		QColor _centerColor;
		QColor _edgeColor;
		std::unique_ptr<Text::CustomEmoji> _inner;
		int _size = 0;

	};

	return std::make_unique<Emoji>(
		entityData,
		centerColor,
		edgeColor,
		std::move(inner),
		std::move(update),
		size);
}

} // namespace Ui::Premium
