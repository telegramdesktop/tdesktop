/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/invite_link_buttons.h"

#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/padding_wrap.h"
#include "lang/lang_keys.h"
#include "styles/style_info.h"

namespace Ui {

void AddCopyShareLinkButtons(
		not_null<VerticalLayout*> container,
		Fn<void()> copyLink,
		Fn<void()> shareLink) {
	const auto wrap = container->add(
		object_ptr<FixedHeightWidget>(
			container,
			st::inviteLinkButton.height),
		st::inviteLinkButtonsPadding);
	const auto copy = CreateChild<RoundButton>(
		wrap,
		tr::lng_group_invite_copy(),
		st::inviteLinkCopy);
	copy->setTextTransform(RoundButton::TextTransform::NoTransform);
	copy->setClickedCallback(copyLink);
	const auto share = CreateChild<RoundButton>(
		wrap,
		tr::lng_group_invite_share(),
		st::inviteLinkShare);
	share->setTextTransform(RoundButton::TextTransform::NoTransform);
	share->setClickedCallback(shareLink);

	wrap->widthValue(
	) | rpl::start_with_next([=](int width) {
		const auto buttonWidth = (width - st::inviteLinkButtonsSkip) / 2;
		copy->setFullWidth(buttonWidth);
		share->setFullWidth(buttonWidth);
		copy->moveToLeft(0, 0, width);
		share->moveToRight(0, 0, width);
	}, wrap->lifetime());
}

} // namespace Ui
