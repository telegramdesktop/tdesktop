/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_text.h"

#include <rpl/before_next.h>
#include <rpl/filter.h>
#include <rpl/after_next.h>
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_info.h"

namespace Info {
namespace Profile {

TextWithLabel CreateTextWithLabel(
		QWidget *parent,
		rpl::producer<TextWithEntities> &&label,
		rpl::producer<TextWithEntities> &&text,
		const style::FlatLabel &labelSt,
		const style::FlatLabel &textSt,
		const style::margins &padding) {
	auto result = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		parent,
		object_ptr<Ui::VerticalLayout>(parent),
		padding);
	result->setDuration(st::infoSlideDuration);
	auto layout = result->entity();
	auto nonEmptyText = rpl::duplicate(
		text
	) | rpl::before_next([slide = result.data()](
			const TextWithEntities &value) {
		if (value.text.isEmpty()) {
			slide->hide(anim::type::normal);
		}
	}) | rpl::filter([](const TextWithEntities &value) {
		return !value.text.isEmpty();
	}) | rpl::after_next([slide = result.data()](
			const TextWithEntities &value) {
		slide->show(anim::type::normal);
	});
	const auto labeled = layout->add(object_ptr<Ui::FlatLabel>(
		layout,
		std::move(nonEmptyText),
		textSt));
	std::move(text) | rpl::start_with_next([=] {
		labeled->resizeToWidth(layout->width());
	}, labeled->lifetime());
	labeled->setSelectable(true);
	layout->add(Ui::CreateSkipWidget(layout, st::infoLabelSkip));
	const auto subtext = layout->add(object_ptr<Ui::FlatLabel>(
		layout,
		std::move(
			label
		) | rpl::after_next([=] {
			layout->resizeToWidth(layout->widthNoMargins());
		}),
		labelSt));
	result->finishAnimating();
	return { std::move(result), labeled, subtext };
}

} // namespace Profile
} // namespace Info
