/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_section_stack.h"

#include "ui/widgets/box_content_divider.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/vertical_list.h"
#include "ui/rect_part.h"

#include "styles/style_info.h"
#include "styles/style_layers.h"

namespace Info::Profile {
namespace {

[[nodiscard]] not_null<Ui::SlideWrap<>*> CreatePlainSeparator(
		not_null<Ui::VerticalLayout*> layout) {
	auto inner = object_ptr<Ui::VerticalLayout>(layout);
	Ui::AddSkip(inner.data(), st::infoProfileSkip);
	inner->add(object_ptr<Ui::BoxContentDivider>(
		inner.data(),
		st::boxDividerHeight,
		st::defaultDividerBar));
	Ui::AddSkip(inner.data(), st::infoProfileSkip);
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
	auto label = object_ptr<Ui::FlatLabel>(
		inner.data(),
		std::move(text),
		st::defaultDividerLabel.label);
	const auto rawLabel = label.data();
	inner->add(object_ptr<Ui::DividerLabel>(
		inner.data(),
		std::move(label),
		st::defaultBoxDividerLabelPadding,
		st::defaultDividerLabel.bar,
		RectPart::Top | RectPart::Bottom));
	Ui::AddSkip(inner.data(), st::infoProfileSkip);
	const auto wrap = layout->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			layout,
			std::move(inner)));
	if (setup) {
		setup(rawLabel);
	}
	return wrap;
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

	Ui::AddSkip(_layout, st::infoProfileSkip);

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
		case RowType::Block:
			_layout->add(std::move(row.widget));
			intrinsic.push_back(shownOrTrue(std::move(row.shown)));
			break;
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

	Ui::AddSkip(_layout, st::infoProfileSkip);

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
}

} // namespace Info::Profile
