/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
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
		const style::FlatLabel &textSt,
		const style::margins &padding) {
	auto result = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		parent,
		object_ptr<Ui::VerticalLayout>(parent),
		padding);
	result->setDuration(
		st::infoSlideDuration
	);
	auto layout = result->entity();
	auto nonEmptyText = std::move(text)
		| rpl::before_next([slide = result.data()](
				const TextWithEntities &value) {
			if (value.text.isEmpty()) {
				slide->hide(anim::type::normal);
			}
		})
		| rpl::filter([](const TextWithEntities &value) {
			return !value.text.isEmpty();
		})
		| rpl::after_next([slide = result.data()](
				const TextWithEntities &value) {
			slide->show(anim::type::normal);
		});
	auto labeled = layout->add(object_ptr<Ui::FlatLabel>(
		layout,
		std::move(nonEmptyText),
		textSt));
	labeled->setSelectable(true);
	layout->add(Ui::CreateSkipWidget(layout, st::infoLabelSkip));
	layout->add(object_ptr<Ui::FlatLabel>(
		layout,
		std::move(label),
		st::infoLabel));
	result->finishAnimating();
	return { std::move(result), labeled };
}

} // namespace Profile
} // namespace Info
