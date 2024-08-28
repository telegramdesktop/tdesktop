/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/premium_graphics.h"

#include "data/data_premium_subscription_option.h"
#include "lang/lang_keys.h"
#include "ui/abstract_button.h"
#include "ui/effects/animations.h"
#include "ui/effects/gradient.h"
#include "ui/effects/numbers_animation.h"
#include "ui/effects/premium_bubble.h"
#include "ui/text/text_utilities.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_options.h"
#include "ui/widgets/checkbox.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_premium.h"
#include "styles/style_settings.h"
#include "styles/style_window.h"

#include <QtCore/QFile>
#include <QtGui/QBrush>
#include <QtSvg/QSvgRenderer>

namespace Ui {
namespace Premium {
namespace {

class GradientRadioView : public Ui::RadioView {
public:
	GradientRadioView(
		const style::Radio &st,
		bool checked,
		Fn<void()> updateCallback = nullptr);

	void setBrush(std::optional<QBrush> brush);

	void paint(QPainter &p, int left, int top, int outerWidth) override;

private:

	not_null<const style::Radio*> _st;
	std::optional<QBrush> _brushOverride;

};

GradientRadioView::GradientRadioView(
	const style::Radio &st,
	bool checked,
	Fn<void()> updateCallback)
: Ui::RadioView(st, checked, updateCallback)
, _st(&st) {
}

void GradientRadioView::paint(QPainter &p, int left, int top, int outerW) {
	auto hq = PainterHighQualityEnabler(p);

	const auto toggled = currentAnimationValue();
	const auto toggledFg = _brushOverride
		? (*_brushOverride)
		: QBrush(_st->toggledFg);

	{
		const auto skip = (_st->outerSkip / 10.) + (_st->thickness / 2);
		const auto rect = QRectF(left, top, _st->diameter, _st->diameter)
			- Margins(skip);

		p.setBrush(_st->bg);
		if (toggled < 1) {
			p.setPen(QPen(_st->untoggledFg, _st->thickness));
			p.drawEllipse(style::rtlrect(rect, outerW));
		}
		if (toggled > 0) {
			p.setOpacity(toggled);
			p.setPen(QPen(toggledFg, _st->thickness));
			p.drawEllipse(style::rtlrect(rect, outerW));
		}
	}

	if (toggled > 0) {
		p.setPen(Qt::NoPen);
		p.setBrush(toggledFg);

		const auto skip0 = _st->diameter / 2.;
		const auto skip1 = _st->skip / 10.;
		const auto checkSkip = skip0 * (1. - toggled) + skip1 * toggled;
		const auto rect = QRectF(left, top, _st->diameter, _st->diameter)
			- Margins(checkSkip);
		p.drawEllipse(style::rtlrect(rect, outerW));
	}
}

void GradientRadioView::setBrush(std::optional<QBrush> brush) {
	_brushOverride = brush;
}

class PartialGradient final {
public:
	PartialGradient(int from, int to, QGradientStops stops);

	[[nodiscard]] QLinearGradient compute(int position, int size) const;

private:
	const int _from;
	const int _to;
	QLinearGradient _gradient;

};

PartialGradient::PartialGradient(int from, int to, QGradientStops stops)
: _from(from)
, _to(to)
, _gradient(0, 0, 0, to - from) {
	_gradient.setStops(std::move(stops));
}

QLinearGradient PartialGradient::compute(int position, int size) const {
	const auto pointTop = position - _from;
	const auto pointBottom = pointTop + size;
	const auto ratioTop = pointTop / float64(_to - _from);
	const auto ratioBottom = pointBottom / float64(_to - _from);

	auto resultGradient = QLinearGradient(
		QPointF(),
		QPointF(0, pointBottom - pointTop));

	resultGradient.setColorAt(
		.0,
		anim::gradient_color_at(_gradient, ratioTop));
	resultGradient.setColorAt(
		.1,
		anim::gradient_color_at(_gradient, ratioBottom));
	return resultGradient;
}

class Line final : public Ui::RpWidget {
public:
	Line(
		not_null<Ui::RpWidget*> parent,
		const style::PremiumLimits &st,
		int max,
		TextFactory textFactory,
		int min,
		float64 ratio);
	Line(
		not_null<Ui::RpWidget*> parent,
		const style::PremiumLimits &st,
		QString max,
		QString min,
		float64 ratio);

	Line(
		not_null<Ui::RpWidget*> parent,
		const style::PremiumLimits &st,
		LimitRowLabels labels,
		rpl::producer<LimitRowState> state);

	void setColorOverride(QBrush brush);

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	void recache(const QSize &s);

	const style::PremiumLimits &_st;

	QPixmap _leftPixmap;
	QPixmap _rightPixmap;

	float64 _ratio = 0.;
	Ui::Animations::Simple _animation;
	rpl::event_stream<> _recaches;
	Ui::Text::String _leftLabel;
	Ui::Text::String _leftText;
	Ui::Text::String _rightLabel;
	Ui::Text::String _rightText;
	bool _dynamic = false;

	std::optional<QBrush> _overrideBrush;

};

Line::Line(
	not_null<Ui::RpWidget*> parent,
	const style::PremiumLimits &st,
	int max,
	TextFactory textFactory,
	int min,
	float64 ratio)
: Line(
	parent,
	st,
	max ? textFactory(max) : QString(),
	min ? textFactory(min) : QString(),
	ratio) {
}

Line::Line(
	not_null<Ui::RpWidget*> parent,
	const style::PremiumLimits &st,
	QString max,
	QString min,
	float64 ratio)
: Line(parent, st, LimitRowLabels{
	.leftLabel = tr::lng_premium_free(),
	.leftCount = rpl::single(min),
	.rightLabel = tr::lng_premium(),
	.rightCount = rpl::single(max),
}, rpl::single(LimitRowState{ ratio })) {
}

Line::Line(
	not_null<Ui::RpWidget*> parent,
	const style::PremiumLimits &st,
	LimitRowLabels labels,
	rpl::producer<LimitRowState> state)
: Ui::RpWidget(parent)
, _st(st) {
	resize(width(), st::requestsAcceptButton.height);

	const auto set = [&](
			Ui::Text::String &label,
			rpl::producer<QString> &text) {
		std::move(text) | rpl::start_with_next([=, &label](QString text) {
			label = { st::semiboldTextStyle, text };
			_recaches.fire({});
		}, lifetime());
	};
	set(_leftLabel, labels.leftLabel);
	set(_leftText, labels.leftCount);
	set(_rightLabel, labels.rightLabel);
	set(_rightText, labels.rightCount);

	std::move(state) | rpl::start_with_next([=](LimitRowState state) {
		_dynamic = state.dynamic;
		if (width() > 0) {
			const auto from = state.animateFromZero
				? 0.
				: _animation.value(_ratio);
			const auto duration = Bubble::SlideNoDeflectionDuration();
			_animation.start([=] {
				update();
			}, from, state.ratio, duration, anim::easeOutCirc);
		}
		_ratio = state.ratio;
	}, lifetime());

	rpl::combine(
		sizeValue(),
		parent->widthValue(),
		_recaches.events_starting_with({})
	) | rpl::filter([](const QSize &size, int parentWidth, auto) {
		return !size.isEmpty() && parentWidth;
	}) | rpl::start_with_next([=](const QSize &size, auto, auto) {
		recache(size);
		update();
	}, lifetime());

}

void Line::setColorOverride(QBrush brush) {
	if (brush.style() == Qt::NoBrush) {
		_overrideBrush = std::nullopt;
	} else {
		_overrideBrush = brush;
	}
}

void Line::paintEvent(QPaintEvent *event) {
	Painter p(this);

	const auto ratio = _animation.value(_ratio);
	const auto left = int(base::SafeRound(ratio * width()));
	const auto dpr = int(_leftPixmap.devicePixelRatio());
	const auto height = _leftPixmap.height() / dpr;
	p.drawPixmap(
		QRect(0, 0, left, height),
		_leftPixmap,
		QRect(0, 0, left * dpr, height * dpr));
	p.drawPixmap(
		QRect(left, 0, width() - left, height),
		_rightPixmap,
		QRect(left * dpr, 0, (width() - left) * dpr, height * dpr));

	p.setFont(st::normalFont);

	const auto textPadding = st::premiumLineTextSkip;
	const auto textTop = (height - _leftLabel.minHeight()) / 2;

	const auto leftMinWidth = _leftLabel.maxWidth()
		+ _leftText.maxWidth()
		+ 3 * textPadding;
	const auto pen = [&](bool gradient) {
		return gradient ? st::activeButtonFg : _st.nonPremiumFg;
	};
	if (!_dynamic && left >= leftMinWidth) {
		p.setPen(pen(_st.gradientFromLeft));
		_leftLabel.drawLeft(
			p,
			textPadding,
			textTop,
			left - textPadding,
			left);
		_leftText.drawRight(
			p,
			textPadding,
			textTop,
			left - textPadding,
			left,
			style::al_right);
	}
	const auto right = width() - left;
	const auto rightMinWidth = 2 * _rightText.maxWidth()
		+ 3 * textPadding;
	if (!_dynamic && right >= rightMinWidth) {
		p.setPen(pen(!_st.gradientFromLeft));
		_rightLabel.drawLeftElided(
			p,
			left + textPadding,
			textTop,
			(right - _rightText.countWidth(right) - textPadding * 2),
			right);
		_rightText.drawRight(
			p,
			textPadding,
			textTop,
			right - textPadding,
			width(),
			style::al_right);
	}
}

void Line::recache(const QSize &s) {
	const auto r = [&](int width) {
		return QRect(0, 0, width, s.height());
	};
	const auto pixmap = [&](int width) {
		auto result = QPixmap(r(width).size() * style::DevicePixelRatio());
		result.setDevicePixelRatio(style::DevicePixelRatio());
		result.fill(Qt::transparent);
		return result;
	};

	const auto pathRound = [&](int width) {
		auto result = QPainterPath();
		result.addRoundedRect(
			r(width),
			st::premiumLineRadius,
			st::premiumLineRadius);
		return result;
	};
	const auto width = s.width();
	const auto fill = [&](QPainter &p, QPainterPath path, bool gradient) {
		if (!gradient) {
			p.fillPath(path, _st.nonPremiumBg);
		} else if (_overrideBrush) {
			p.fillPath(path, *_overrideBrush);
		} else {
			p.fillPath(path, QBrush(ComputeGradient(this, 0, width)));
		}
	};
	const auto textPadding = st::premiumLineTextSkip;
	const auto textTop = (s.height() - _leftLabel.minHeight()) / 2;
	const auto rwidth = _rightLabel.maxWidth();
	const auto pen = [&](bool gradient) {
		return gradient ? st::activeButtonFg : _st.nonPremiumFg;
	};
	{
		auto leftPixmap = pixmap(width);
		auto p = Painter(&leftPixmap);
		auto hq = PainterHighQualityEnabler(p);
		fill(p, pathRound(width), _st.gradientFromLeft);
		if (_dynamic) {
			p.setFont(st::normalFont);
			p.setPen(pen(_st.gradientFromLeft));
			_leftLabel.drawLeft(p, textPadding, textTop, width, width);
			_rightLabel.drawRight(p, textPadding, textTop, rwidth, width);
		}
		_leftPixmap = std::move(leftPixmap);
	}
	{
		auto rightPixmap = pixmap(width);
		auto p = Painter(&rightPixmap);
		auto hq = PainterHighQualityEnabler(p);
		fill(p, pathRound(width), !_st.gradientFromLeft);
		if (_dynamic) {
			p.setFont(st::normalFont);
			p.setPen(pen(!_st.gradientFromLeft));
			_leftLabel.drawLeft(p, textPadding, textTop, width, width);
			_rightLabel.drawRight(p, textPadding, textTop, rwidth, width);
		}
		_rightPixmap = std::move(rightPixmap);
	}
}

} // namespace

QString Svg() {
	return u":/gui/icons/settings/star.svg"_q;
}

QByteArray ColorizedSvg(const QGradientStops &gradientStops) {
	auto f = QFile(Svg());
	if (!f.open(QIODevice::ReadOnly)) {
		return QByteArray();
	}
	auto content = QString::fromUtf8(f.readAll());
	auto stops = [&] {
		auto s = QString();
		for (const auto &stop : gradientStops) {
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

void AddLimitRow(
		not_null<Ui::VerticalLayout*> parent,
		const style::PremiumLimits &st,
		QString max,
		QString min,
		float64 ratio) {
	parent->add(
		object_ptr<Line>(parent, st, max, min, ratio),
		st::boxRowPadding);
}

void AddLimitRow(
		not_null<Ui::VerticalLayout*> parent,
		const style::PremiumLimits &st,
		int max,
		std::optional<tr::phrase<lngtag_count>> phrase,
		int min,
		float64 ratio) {
	const auto factory = ProcessTextFactory(phrase);
	AddLimitRow(
		parent,
		st,
		max ? factory(max) : QString(),
		min ? factory(min) : QString(),
		ratio);
}

void AddLimitRow(
		not_null<Ui::VerticalLayout*> parent,
		const style::PremiumLimits &st,
		LimitRowLabels labels,
		rpl::producer<LimitRowState> state,
		const style::margins &padding) {
	parent->add(
		object_ptr<Line>(parent, st, std::move(labels), std::move(state)),
		padding);
}

void AddAccountsRow(
		not_null<Ui::VerticalLayout*> parent,
		AccountsRowArgs &&args) {
	const auto container = parent->add(
		object_ptr<Ui::FixedHeightWidget>(parent, st::premiumAccountsHeight),
		st::boxRowPadding);

	struct Account {
		not_null<Ui::AbstractButton*> widget;
		Ui::RoundImageCheckbox checkbox;
		Ui::Text::String name;
		QPixmap badge;
	};
	struct State {
		std::vector<Account> accounts;
	};
	const auto state = container->lifetime().make_state<State>();
	const auto group = args.group;

	const auto imageRadius = args.st.imageRadius;
	const auto checkSelectWidth = args.st.selectWidth;
	const auto nameFg = args.stNameFg;

	const auto cacheBadge = [=](int center) {
		const auto &padding = st::premiumAccountsLabelPadding;
		const auto size = st::premiumAccountsLabelSize
			+ QSize(
				padding.left() + padding.right(),
				padding.top() + padding.bottom());
		auto badge = QPixmap(size * style::DevicePixelRatio());
		badge.setDevicePixelRatio(style::DevicePixelRatio());
		badge.fill(Qt::transparent);

		auto p = QPainter(&badge);
		auto hq = PainterHighQualityEnabler(p);

		p.setPen(Qt::NoPen);
		const auto rectOut = QRect(QPoint(), size);
		const auto rectIn = rectOut - padding;

		const auto radius = st::premiumAccountsLabelRadius;
		p.setBrush(st::premiumButtonFg);
		p.drawRoundedRect(rectOut, radius, radius);

		const auto left = center - rectIn.width() / 2;
		p.setBrush(QBrush(ComputeGradient(container, left, rectIn.width())));
		p.drawRoundedRect(rectIn, radius / 2, radius / 2);

		p.setPen(st::premiumButtonFg);
		p.setFont(st::semiboldFont);
		p.drawText(rectIn, u"+1"_q, style::al_center);

		return badge;
	};

	for (auto &entry : args.entries) {
		const auto widget = Ui::CreateChild<Ui::AbstractButton>(container);
		auto name = Ui::Text::String(imageRadius * 2);
		name.setText(args.stName, entry.name, Ui::NameTextOptions());
		state->accounts.push_back(Account{
			.widget = widget,
			.checkbox = Ui::RoundImageCheckbox(
				args.st,
				[=] { widget->update(); },
				base::take(entry.paintRoundImage)),
			.name = std::move(name),
		});
		const auto index = int(state->accounts.size()) - 1;
		state->accounts[index].checkbox.setChecked(
			index == group->current(),
			anim::type::instant);

		widget->paintRequest(
		) | rpl::start_with_next([=] {
			Painter p(widget);
			const auto width = widget->width();
			const auto photoLeft = (width - (imageRadius * 2)) / 2;
			const auto photoTop = checkSelectWidth;
			const auto &account = state->accounts[index];
			account.checkbox.paint(p, photoLeft, photoTop, width);

			const auto &badgeSize = account.badge.size()
				/ style::DevicePixelRatio();
			p.drawPixmap(
				(width - badgeSize.width()) / 2,
				photoTop + (imageRadius * 2) - badgeSize.height() / 2,
				account.badge);

			p.setPen(nameFg);
			p.setBrush(Qt::NoBrush);
			account.name.drawLeftElided(
				p,
				0,
				photoTop + imageRadius * 2 + st::premiumAccountsNameTop,
				width,
				width,
				2,
				style::al_top,
				0,
				-1,
				0,
				true);
		}, widget->lifetime());

		widget->setClickedCallback([=] {
			group->setValue(index);
		});
	}

	container->sizeValue(
	) | rpl::start_with_next([=](const QSize &size) {
		const auto count = state->accounts.size();
		const auto columnWidth = size.width() / count;
		for (auto i = 0; i < count; i++) {
			auto &account = state->accounts[i];
			account.widget->resize(columnWidth, size.height());
			const auto left = columnWidth * i;
			account.widget->moveToLeft(left, 0);
			account.badge = cacheBadge(left + columnWidth / 2);

			const auto photoWidth = ((imageRadius + checkSelectWidth) * 2);
			account.checkbox.setColorOverride(QBrush(
				ComputeGradient(
					container,
					left + (columnWidth - photoWidth) / 2,
					photoWidth)));
		}
	}, container->lifetime());

	group->setChangedCallback([=](int value) {
		for (auto i = 0; i < state->accounts.size(); i++) {
			state->accounts[i].checkbox.setChecked(i == value);
		}
	});
}

QGradientStops LimitGradientStops() {
	return {
		{ 0.0, st::premiumButtonBg1->c },
		{ .25, st::premiumButtonBg1->c },
		{ .85, st::premiumButtonBg2->c },
		{ 1.0, st::premiumButtonBg3->c },
	};
}

QGradientStops ButtonGradientStops() {
	return {
		{ 0., st::premiumButtonBg1->c },
		{ 0.6, st::premiumButtonBg2->c },
		{ 1., st::premiumButtonBg3->c },
	};
}

QGradientStops LockGradientStops() {
	return ButtonGradientStops();
}

QGradientStops FullHeightGradientStops() {
	return {
		{ 0.0, st::premiumIconBg1->c },
		{ .28, st::premiumIconBg2->c },
		{ .55, st::premiumButtonBg2->c },
		{ .75, st::premiumButtonBg1->c },
		{ 1.0, st::premiumIconBg3->c },
	};
}

QGradientStops GiftGradientStops() {
	return {
		{ 0., st::premiumButtonBg1->c },
		{ 1., st::premiumButtonBg2->c },
	};
}

QGradientStops StoriesIconsGradientStops() {
	return {
		{ 0., st::premiumButtonBg1->c },
		{ .33, st::premiumButtonBg2->c },
		{ .66, st::premiumButtonBg3->c },
		{ 1., st::premiumIconBg1->c },
	};
}

QGradientStops CreditsIconGradientStops() {
	return {
		{ 0., st::creditsBg1->c },
		{ 1., st::creditsBg2->c },
	};
}

QLinearGradient ComputeGradient(
		not_null<QWidget*> content,
		int left,
		int width) {
	// Take a full width of parent box without paddings.
	const auto fullGradientWidth = content->parentWidget()->width();
	auto fullGradient = QLinearGradient(0, 0, fullGradientWidth, 0);
	fullGradient.setStops(ButtonGradientStops());

	auto gradient = QLinearGradient(0, 0, width, 0);
	const auto fullFinal = float64(fullGradient.finalStop().x());
	left += ((fullGradientWidth - content->width()) / 2);
	gradient.setColorAt(
		.0,
		anim::gradient_color_at(fullGradient, left / fullFinal));
	gradient.setColorAt(
		1.,
		anim::gradient_color_at(
			fullGradient,
			std::min(1., (left + width) / fullFinal)));

	return gradient;
}

void ShowListBox(
		not_null<Ui::GenericBox*> box,
		const style::PremiumLimits &st,
		std::vector<ListEntry> entries) {
	box->setWidth(st::boxWideWidth);

	const auto &stLabel = st::defaultFlatLabel;
	const auto &titlePadding = st::settingsPremiumPreviewTitlePadding;
	const auto &aboutPadding = st::settingsPremiumPreviewAboutPadding;
	const auto iconTitlePadding = st::settingsPremiumPreviewIconTitlePadding;
	const auto iconAboutPadding = st::settingsPremiumPreviewIconAboutPadding;

	auto lines = std::vector<Line*>();
	lines.reserve(int(entries.size()));

	auto icons = std::shared_ptr<std::vector<QColor>>();

	const auto content = box->verticalLayout();
	for (auto &entry : entries) {
		const auto title = content->add(
			object_ptr<Ui::FlatLabel>(
				content,
				base::take(entry.title) | Ui::Text::ToBold(),
				stLabel),
			entry.icon ? iconTitlePadding : titlePadding);
		content->add(
			object_ptr<Ui::FlatLabel>(
				content,
				base::take(entry.about),
				st::boxDividerLabel),
			entry.icon ? iconAboutPadding : aboutPadding);
		if (const auto outlined = entry.icon) {
			if (!icons) {
				icons = std::make_shared<std::vector<QColor>>();
			}
			const auto index = int(icons->size());
			icons->push_back(QColor());
			const auto icon = Ui::CreateChild<Ui::RpWidget>(content.get());
			icon->resize(outlined->size());
			title->topValue() | rpl::start_with_next([=](int y) {
				const auto shift = st::settingsPremiumPreviewIconPosition;
				icon->move(QPoint(0, y) + shift);
			}, icon->lifetime());
			icon->paintRequest() | rpl::start_with_next([=] {
				auto p = QPainter(icon);
				outlined->paintInCenter(p, icon->rect(), (*icons)[index]);
			}, icon->lifetime());
		}
		if (entry.leftNumber || entry.rightNumber) {
			auto factory = [=, text = ProcessTextFactory({})](int n) {
				if (entry.customRightText && (n == entry.rightNumber)) {
					return *entry.customRightText;
				} else {
					return text(n);
				}
			};
			const auto limitRow = content->add(
				object_ptr<Line>(
					content,
					st,
					entry.rightNumber,
					TextFactory(std::move(factory)),
					entry.leftNumber,
					kLimitRowRatio),
				st::settingsPremiumPreviewLinePadding);
			lines.push_back(limitRow);
		}
	}

	content->resizeToWidth(content->height());

	// Colors for icons.
	if (icons) {
		box->addSkip(st::settingsPremiumPreviewLinePadding.bottom());

		const auto stops = Ui::Premium::StoriesIconsGradientStops();
		for (auto i = 0, count = int(icons->size()); i != count; ++i) {
			(*icons)[i] = anim::gradient_color_at(
				stops,
				(count > 1) ? (i / float64(count - 1)) : 0.);
		}
	}

	// Color lines.
	if (!lines.empty()) {
		box->addSkip(st::settingsPremiumPreviewLinePadding.bottom());

		const auto from = lines.front()->y();
		const auto to = lines.back()->y() + lines.back()->height();

		const auto partialGradient = [&] {
			auto stops = Ui::Premium::FullHeightGradientStops();
			// Reverse.
			for (auto &stop : stops) {
				stop.first = std::abs(stop.first - 1.);
			}
			return PartialGradient(from, to, std::move(stops));
		}();

		for (auto i = 0; i < int(lines.size()); i++) {
			const auto &line = lines[i];

			const auto brush = QBrush(
				partialGradient.compute(line->y(), line->height()));
			line->setColorOverride(brush);
		}
		box->addSkip(st::settingsPremiumPreviewLinePadding.bottom());
	}
}

void AddGiftOptions(
		not_null<Ui::VerticalLayout*> parent,
		std::shared_ptr<Ui::RadiobuttonGroup> group,
		std::vector<Data::PremiumSubscriptionOption> gifts,
		const style::PremiumOption &st,
		bool topBadges) {

	struct Edges {
		Ui::RpWidget *top = nullptr;
		Ui::RpWidget *bottom = nullptr;
	};
	const auto edges = parent->lifetime().make_state<Edges>();
	struct Animation {
		int nowIndex = 0;
		Ui::Animations::Simple animation;
	};
	const auto wasGroupValue = group->current();
	const auto animation = parent->lifetime().make_state<Animation>();
	animation->nowIndex = wasGroupValue;

	const auto stops = GiftGradientStops();

	const auto addRow = [&](
			const Data::PremiumSubscriptionOption &info,
			int index) {
		const auto row = parent->add(
			object_ptr<Ui::AbstractButton>(parent),
			st.rowPadding);
		row->resize(row->width(), st.rowHeight);
		{
			if (!index) {
				edges->top = row;
			}
			edges->bottom = row;
		}

		const auto &stCheckbox = st::defaultBoxCheckbox;
		auto radioView = std::make_unique<GradientRadioView>(
			st::defaultRadio,
			(group->hasValue() && group->current() == index));
		const auto radioViewRaw = radioView.get();
		const auto radio = Ui::CreateChild<Ui::Radiobutton>(
			row,
			group,
			index,
			QString(),
			stCheckbox,
			std::move(radioView));
		radio->setAttribute(Qt::WA_TransparentForMouseEvents);
		radio->show();
		{ // Paint the last frame instantly for the layer animation.
			group->setValue(0);
			group->setValue(wasGroupValue);
			radio->finishAnimating();
		}

		row->sizeValue(
		) | rpl::start_with_next([=, margins = stCheckbox.margin](
				const QSize &s) {
			const auto radioHeight = radio->height()
				- margins.top()
				- margins.bottom();
			radio->moveToLeft(
				st.rowMargins.left(),
				(s.height() - radioHeight) / 2);
		}, radio->lifetime());

		{
			auto onceLifetime = std::make_shared<rpl::lifetime>();
			row->paintRequest(
			) | rpl::take(
				1
			) | rpl::start_with_next([=]() mutable {
				const auto from = edges->top->y();
				const auto to = edges->bottom->y() + edges->bottom->height();
				auto partialGradient = PartialGradient(from, to, stops);

				radioViewRaw->setBrush(
					partialGradient.compute(row->y(), row->height()));
				if (onceLifetime) {
					base::take(onceLifetime)->destroy();
				}
			}, *onceLifetime);
		}

		row->paintRequest(
		) | rpl::start_with_next([=](const QRect &r) {
			auto p = QPainter(row);
			auto hq = PainterHighQualityEnabler(p);

			p.fillRect(r, Qt::transparent);

			const auto left = st.textLeft;
			const auto halfHeight = row->height() / 2;

			const auto titleFont = st::semiboldFont;
			p.setFont(titleFont);
			p.setPen(st::boxTextFg);
			if (info.costPerMonth.isEmpty() && info.discount.isEmpty()) {
				const auto r = row->rect().translated(
					-row->rect().left() + left,
					0);
				p.drawText(r, info.duration, style::al_left);
			} else {
				p.drawText(
					left,
					st.subtitleTop + titleFont->ascent,
					info.duration);
			}

			const auto discountFont = st::windowFiltersButton.badgeStyle.font;
			const auto discountWidth = discountFont->width(info.discount);
			const auto &discountMargins = discountWidth
				? st.badgeMargins
				: style::margins();

			const auto bottomLeftRect = QRect(
				left,
				halfHeight + discountMargins.top(),
				discountWidth
					+ discountMargins.left()
					+ discountMargins.right(),
				st.badgeHeight);
			const auto discountRect = topBadges
				? bottomLeftRect.translated(
					titleFont->width(info.duration) + st.badgeShift.x(),
					-bottomLeftRect.top()
						+ st.badgeShift.y()
						+ st.subtitleTop
						+ (titleFont->height - bottomLeftRect.height()) / 2)
				: bottomLeftRect;
			const auto from = edges->top->y();
			const auto to = edges->bottom->y() + edges->bottom->height();
			auto partialGradient = PartialGradient(from, to, stops);
			const auto partialGradientBrush = partialGradient.compute(
				row->y(),
				row->height());
			{
				p.setPen(Qt::NoPen);
				p.setBrush(partialGradientBrush);
				const auto round = st.badgeRadius;
				p.drawRoundedRect(discountRect, round, round);
			}

			if (st.borderWidth && (animation->nowIndex == index)) {
				const auto progress = animation->animation.value(1.);
				const auto w = row->width();
				auto gradient = QLinearGradient(w - w * progress, 0, w * 2, 0);
				gradient.setSpread(QGradient::Spread::RepeatSpread);
				gradient.setStops(stops);
				const auto pen = QPen(
					QBrush(partialGradientBrush),
					st.borderWidth);
				p.setPen(pen);
				p.setBrush(Qt::NoBrush);
				const auto borderRect = row->rect()
					- Margins(pen.width() / 2);
				const auto round = st.borderRadius;
				p.drawRoundedRect(borderRect, round, round);
			}

			p.setPen(st::premiumButtonFg);
			p.setFont(discountFont);
			p.drawText(discountRect, info.discount, style::al_center);

			const auto perRect = QMargins(0, 0, row->width(), 0)
				+ bottomLeftRect.translated(
					topBadges
						? 0
						: bottomLeftRect.width() + discountMargins.left(),
					0);
			p.setPen(st::windowSubTextFg);
			p.setFont(st::shareBoxListItem.nameStyle.font);
			p.drawText(perRect, info.costPerMonth, style::al_left);

			const auto totalRect = row->rect()
				- QMargins(0, 0, st.rowMargins.right(), 0);
			p.setFont(st::normalFont);
			p.drawText(totalRect, info.costTotal, style::al_right);
		}, row->lifetime());

		row->setClickedCallback([=, duration = st::defaultCheck.duration] {
			group->setValue(index);
			animation->nowIndex = group->current();
			animation->animation.stop();
			animation->animation.start(
				[=] { parent->update(); },
				0.,
				1.,
				duration);
		});

	};
	for (auto i = 0; i < gifts.size(); i++) {
		addRow(gifts[i], i);
	}

	parent->resizeToWidth(parent->height());
	parent->update();
}

} // namespace Premium
} // namespace Ui
