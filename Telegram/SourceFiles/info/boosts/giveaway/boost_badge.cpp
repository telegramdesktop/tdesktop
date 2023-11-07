/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/boosts/giveaway/boost_badge.h"

#include "ui/effects/radial_animation.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/rp_widget.h"
#include "ui/widgets/labels.h"
#include "styles/style_giveaway.h"
#include "styles/style_statistics.h"
#include "styles/style_widgets.h"

namespace Info::Statistics {

not_null<Ui::RpWidget*> InfiniteRadialAnimationWidget(
		not_null<Ui::RpWidget*> parent,
		int size) {
	class Widget final : public Ui::RpWidget {
	public:
		Widget(not_null<Ui::RpWidget*> p, int size)
		: Ui::RpWidget(p)
		, _animation([=] { update(); }, st::startGiveawayButtonLoading) {
			resize(size, size);
			shownValue() | rpl::start_with_next([=](bool v) {
				return v
					? _animation.start()
					: _animation.stop(anim::type::instant);
			}, lifetime());
		}

	protected:
		void paintEvent(QPaintEvent *e) override {
			auto p = QPainter(this);
			p.setPen(st::activeButtonFg);
			p.setBrush(st::activeButtonFg);
			const auto r = rect()
				- Margins(st::startGiveawayButtonLoading.thickness);
			_animation.draw(p, r.topLeft(), r.size(), width());
		}

	private:
		Ui::InfiniteRadialAnimation _animation;

	};

	return Ui::CreateChild<Widget>(parent.get(), size);
}

QImage CreateBadge(
		const style::TextStyle &textStyle,
		const QString &text,
		int badgeHeight,
		const style::margins &textPadding,
		const style::color &bg,
		const style::color &fg,
		float64 bgOpacity,
		const style::margins &iconPadding,
		const style::icon &icon) {
	auto badgeText = Ui::Text::String(textStyle, text);
	const auto badgeTextWidth = badgeText.maxWidth();
	const auto badgex = 0;
	const auto badgey = 0;
	const auto badgeh = 0 + badgeHeight;
	const auto badgew = badgeTextWidth
		+ rect::m::sum::h(textPadding);
	auto result = QImage(
		QSize(badgew, badgeh) * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);
	result.setDevicePixelRatio(style::DevicePixelRatio());
	{
		auto p = Painter(&result);

		p.setPen(Qt::NoPen);
		p.setBrush(bg);

		const auto r = QRect(badgex, badgey, badgew, badgeh);
		{
			auto hq = PainterHighQualityEnabler(p);
			auto o = ScopedPainterOpacity(p, bgOpacity);
			p.drawRoundedRect(r, badgeh / 2, badgeh / 2);
		}

		p.setPen(fg);
		p.setBrush(Qt::NoBrush);
		badgeText.drawLeftElided(
			p,
			r.x() + textPadding.left(),
			badgey + textPadding.top(),
			badgew,
			badgew * 2);

		icon.paint(
			p,
			QPoint(r.x() + iconPadding.left(), r.y() + iconPadding.top()),
			badgew * 2);
	}
	return result;
}

void AddLabelWithBadgeToButton(
		not_null<Ui::RpWidget*> parent,
		rpl::producer<QString> text,
		rpl::producer<int> number,
		rpl::producer<bool> shown) {
	struct State {
		QImage badge;
	};
	const auto state = parent->lifetime().make_state<State>();
	const auto label = Ui::CreateChild<Ui::LabelSimple>(
		parent.get(),
		st::startGiveawayButtonLabelSimple);
	std::move(
		text
	) | rpl::start_with_next([=](const QString &s) {
		label->setText(s);
	}, label->lifetime());
	const auto count = Ui::CreateChild<Ui::RpWidget>(parent.get());
	count->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(count);
		p.drawImage(0, 0, state->badge);
	}, count->lifetime());
	std::move(
		number
	) | rpl::start_with_next([=](int c) {
		state->badge = Info::Statistics::CreateBadge(
			st::startGiveawayButtonTextStyle,
			QString::number(c),
			st::boostsListBadgeHeight,
			st::startGiveawayButtonBadgeTextPadding,
			st::activeButtonFg,
			st::activeButtonBg,
			1.,
			st::boostsListMiniIconPadding,
			st::startGiveawayButtonMiniIcon);
		count->resize(state->badge.size() / style::DevicePixelRatio());
		count->update();
	}, count->lifetime());

	std::move(
		shown
	) | rpl::start_with_next([=](bool shown) {
		count->setVisible(shown);
		label->setVisible(shown);
	}, count->lifetime());

	rpl::combine(
		parent->sizeValue(),
		label->sizeValue(),
		count->sizeValue()
	) | rpl::start_with_next([=](
			const QSize &s,
			const QSize &s1,
			const QSize &s2) {
		const auto sum = st::startGiveawayButtonMiniIconSkip
			+ s1.width()
			+ s2.width();
		const auto contentLeft = (s.width() - sum) / 2;
		label->moveToLeft(contentLeft, (s.height() - s1.height()) / 2);
		count->moveToLeft(
			contentLeft + sum - s2.width(),
			(s.height() - s2.height()) / 2 + st::boostsListMiniIconSkip);
	}, parent->lifetime());
}

} // namespace Info::Statistics
