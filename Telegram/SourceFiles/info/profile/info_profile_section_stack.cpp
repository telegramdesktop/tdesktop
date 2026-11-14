/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_section_stack.h"

#include "ui/widgets/box_content_divider.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/vertical_list.h"
#include "ui/rect_part.h"
#include "ui/ui_utility.h"

#include "ui/painter.h"
#include "styles/style_info.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_dialogs.h"

namespace Info::Profile {
namespace {

[[nodiscard]] not_null<Ui::SlideWrap<>*> CreatePlainSeparator(
		not_null<Ui::VerticalLayout*> layout) {
	// Mokagram: между таблетками обычный зазор вместо полосы-разделителя.
	auto inner = object_ptr<Ui::VerticalLayout>(layout);
	Ui::AddSkip(inner.data(), 1);
	return layout->add(object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		layout,
		std::move(inner)));
}

[[nodiscard]] not_null<Ui::SlideWrap<>*> CreateTextSeparator(
		not_null<Ui::VerticalLayout*> layout,
		rpl::producer<TextWithEntities> text,
		Fn<void(not_null<Ui::FlatLabel*>)> setup) {
	auto inner = object_ptr<Ui::VerticalLayout>(layout);
	Ui::AddSkip(inner.data(), st::infoProfileSkip);
	// Mokagram: подпись под карточкой по центру.
	static const auto centered = [] {
		auto result = st::defaultDividerLabel.label;
		result.align = style::al_top;
		return result;
	}();
	auto label = object_ptr<Ui::FlatLabel>(
		inner.data(),
		std::move(text),
		centered);
	const auto rawLabel = label.data();
	// Mokagram: без серой полосы-подложки, просто подпись под карточкой.
	inner->add(
		std::move(label),
		style::margins(26, 0, 26, 0),
		style::al_top);
	// Mokagram: снизу запас больше, чтобы подпись стояла посередине между карточками.
	Ui::AddSkip(inner.data(), st::infoProfileSkip + 14);
	const auto wrap = layout->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			layout,
			std::move(inner)));
	if (setup) {
		setup(rawLabel);
	}
	return wrap;
}

constexpr auto kCardRadius = 28;
constexpr auto kCardInset = 4;

// Mokagram: секция профиля как отдельная скруглённая таблетка.
// Фон рисуется под содержимым, а углы закрывают четыре маленьких виджета
// цвета страницы: дочерние строки красят свой прямоугольник целиком.
// Радиус не больше половины высоты, чтобы низкие секции стали пилюлями.
void MakeCard(not_null<Ui::RpWidget*> card) {
	card->paintRequest(
	) | rpl::on_next([=](QRect clip) {
		auto p = QPainter(card);
		p.fillRect(clip, st::windowBg);
	}, card->lifetime());

	struct Corner {
		not_null<Ui::RpWidget*> widget;
		bool right;
		bool bottom;
	};
	struct State {
		std::vector<Corner> corners;
		int radius = kCardRadius;
		bool raiseScheduled = false;
	};
	const auto state = card->lifetime().make_state<State>();
	for (const auto right : { false, true }) {
		for (const auto bottom : { false, true }) {
			const auto corner = Ui::CreateChild<Ui::RpWidget>(card.get());
			corner->setAttribute(Qt::WA_TransparentForMouseEvents);
			corner->paintRequest(
			) | rpl::on_next([=] {
				auto p = QPainter(corner);
				auto hq = PainterHighQualityEnabler(p);
				const auto r = state->radius;
				auto path = QPainterPath();
				path.addRect(corner->rect());
				auto circle = QPainterPath();
				circle.addEllipse(QRectF(
					right ? -r : 0,
					bottom ? -r : 0,
					2 * r,
					2 * r));
				p.fillPath(path.subtracted(circle), st::dialogsBg);
			}, corner->lifetime());
			state->corners.push_back({ corner, right, bottom });
		}
	}
	const auto sync = [=] {
		const auto size = card->size();
		const auto r = std::max(1, std::min(
			kCardRadius,
			std::min(size.width(), size.height()) / 2));
		state->radius = r;
		for (const auto &corner : state->corners) {
			corner.widget->resize(r, r);
			corner.widget->move(
				corner.right ? size.width() - r : 0,
				corner.bottom ? size.height() - r : 0);
			corner.widget->show();
			corner.widget->update();
			corner.widget->raise();
		}
	};
	// Строки секции создаются заново (например, после выхода из режима
	// редактирования) и оказываются выше углов, а размер может прийти
	// раньше показа: пересчитываем углы при любом таком событии.
	card->events(
	) | rpl::filter([](not_null<QEvent*> e) {
		const auto type = e->type();
		return (type == QEvent::Show) || (type == QEvent::Resize);
	}) | rpl::on_next([=](not_null<QEvent*>) {
		sync();
	}, card->lifetime());
	card->events(
	) | rpl::filter([](not_null<QEvent*> e) {
		const auto type = e->type();
		return (type == QEvent::ChildAdded)
			|| (type == QEvent::LayoutRequest);
	}) | rpl::on_next([=](not_null<QEvent*>) {
		if (state->raiseScheduled) {
			return;
		}
		state->raiseScheduled = true;
		Ui::PostponeCall(card, [=] {
			state->raiseScheduled = false;
			sync();
		});
	}, card->lifetime());
	sync();
}

} // namespace

SectionStack::SectionStack(not_null<Ui::VerticalLayout*> layout)
: _layout(layout) {
}

void SectionStack::add(Section section) {
	Expects(!_finalized);
	Expects(section.widget != nullptr);

	_rows.push_back({
		.type = RowType::Block,
		.widget = std::move(section.widget),
		.shown = std::move(section.shown),
		.bare = section.bare,
	});
}

void SectionStack::addPlainSeparator() {
	Expects(!_finalized);

	if (_rows.empty() || _rows.back().type == RowType::PlainSeparator) {
		return;
	}
	_rows.push_back({ .type = RowType::PlainSeparator });
}

void SectionStack::addTextSeparator(
		rpl::producer<TextWithEntities> text,
		rpl::producer<bool> shown,
		Fn<void(not_null<Ui::FlatLabel*>)> setup) {
	Expects(!_finalized);

	_rows.push_back({
		.type = RowType::TextSeparator,
		.shown = std::move(shown),
		.text = std::move(text),
		.textSetup = std::move(setup),
	});
}

not_null<Ui::VerticalLayout*> SectionStack::layout() const {
	return _layout;
}

// Decides which separators are visible given the intrinsic visibility of
// every row. Blocks and text separators are shown by their own data; the
// computation only resolves plain separators, which exist purely to delimit
// content. A plain separator stays visible only when there is a visible block
// in the run immediately above and the run immediately below it, where each
// run is bounded by the nearest visible divider on that side. Scanning down a
// plain separator treats every later plain separator as a boundary so that, in
// a single gap, the lowest plain separator wins; scanning up it sees through
// plain separators that turned out hidden. Visible text separators bound runs
// on both sides, which keeps a plain separator from doubling a text divider.
std::vector<bool> SectionStack::ComputeVisibility(
		const std::vector<RowType> &kinds,
		const std::vector<bool> &intrinsic) {
	const auto count = int(kinds.size());
	auto result = std::vector<bool>(count, false);
	for (auto i = 0; i != count; ++i) {
		if (kinds[i] != RowType::PlainSeparator) {
			result[i] = intrinsic[i];
		}
	}
	const auto blockVisibleAbove = [&](int separator) {
		for (auto j = separator - 1; j >= 0; --j) {
			switch (kinds[j]) {
			case RowType::Block:
				if (intrinsic[j]) {
					return true;
				}
				break;
			case RowType::TextSeparator:
				if (intrinsic[j]) {
					return false;
				}
				break;
			case RowType::PlainSeparator:
				if (result[j]) {
					return false;
				}
				break;
			}
		}
		return false;
	};
	const auto blockVisibleBelow = [&](int separator) {
		for (auto j = separator + 1; j != count; ++j) {
			switch (kinds[j]) {
			case RowType::Block:
				if (intrinsic[j]) {
					return true;
				}
				break;
			case RowType::TextSeparator:
				if (intrinsic[j]) {
					return false;
				}
				break;
			case RowType::PlainSeparator:
				return false;
			}
		}
		return false;
	};
	for (auto i = 0; i != count; ++i) {
		if (kinds[i] == RowType::PlainSeparator) {
			result[i] = blockVisibleAbove(i) && blockVisibleBelow(i);
		}
	}
	return result;
}

void SectionStack::finalize() {
	Expects(!_finalized);

	_finalized = true;

	Ui::AddSkip(_layout, st::infoProfileSkip + 5);

	const auto count = int(_rows.size());
	auto kinds = std::vector<RowType>();
	auto intrinsic = std::vector<rpl::producer<bool>>();
	auto separators = std::vector<Ui::SlideWrap<>*>(count, nullptr);
	kinds.reserve(count);
	intrinsic.reserve(count);
	const auto shownOrTrue = [](rpl::producer<bool> shown) {
		return shown
			? std::move(shown)
			: rpl::producer<bool>(rpl::single(true));
	};
	for (auto i = 0; i != count; ++i) {
		auto &row = _rows[i];
		kinds.push_back(row.type);
		switch (row.type) {
		case RowType::Block: {
			auto shown = shownOrTrue(std::move(row.shown));
			const auto inner = row.widget.data();
			// Карточки разной природы дают разные внешние поля (0 или 5),
			// выравниваем так, чтобы видимая ширина везде была одинаковой.
			const auto side = 11 + inner->getMargins().left();
			auto content = std::move(row.widget);
			if (!row.bare) {
				// Mokagram: строки внутри карточки с отступом, как в
				// настройках, чтобы подсветка была таблеткой, а не резалась
				// углами карточки.
				auto padded = object_ptr<Ui::PaddingWrap<Ui::RpWidget>>(
					_layout,
					std::move(content),
					style::margins(
						kCardInset,
						kCardInset,
						kCardInset,
						kCardInset));
				MakeCard(padded.data());
				content = std::move(padded);
			}
			const auto wrap = _layout->add(
				object_ptr<Ui::SlideWrap<Ui::RpWidget>>(
					_layout,
					std::move(content),
					QMargins(side, 0, side, 12)),
				QMargins(),
				style::al_justify);
			wrap->toggleOn(rpl::duplicate(shown), anim::type::instant);
			intrinsic.push_back(std::move(shown));
			break;
		}
		case RowType::PlainSeparator:
			separators[i] = CreatePlainSeparator(_layout);
			intrinsic.push_back(rpl::single(true));
			break;
		case RowType::TextSeparator:
			separators[i] = CreateTextSeparator(
				_layout,
				std::move(row.text),
				std::move(row.textSetup));
			intrinsic.push_back(shownOrTrue(std::move(row.shown)));
			break;
		}
	}

	// Mokagram: у последней карточки нижнее поле гасим, остаётся край панели.
	_layout->add(
		object_ptr<Ui::FixedHeightWidget>(_layout, 0),
		QMargins(0, -12, 0, 0));


	if (intrinsic.empty()) {
		return;
	}
	const auto visible = _layout->lifetime().make_state<
		rpl::variable<std::vector<bool>>>(rpl::combine(
			std::move(intrinsic),
			[kinds = std::move(kinds)](const std::vector<bool> &values) {
				return SectionStack::ComputeVisibility(kinds, values);
			}));
	for (auto i = 0; i != count; ++i) {
		const auto wrap = separators[i];
		if (!wrap) {
			continue;
		}
		wrap->setDuration(st::infoSlideDuration)->toggleOn(
			visible->value() | rpl::map([=](const std::vector<bool> &v) {
				return (i < int(v.size())) && v[i];
			}) | rpl::distinct_until_changed());
	}
	visible->changes() | rpl::on_next([layout = _layout] {
		// A toggle landing inside a layout resize pass is skipped there,
		// leaving the rows below the separator overlapped by it.
		InvokeQueued(layout, [=] {
			layout->resizeToWidth(layout->widthNoMargins());
		});
	}, _layout->lifetime());
}

} // namespace Info::Profile
