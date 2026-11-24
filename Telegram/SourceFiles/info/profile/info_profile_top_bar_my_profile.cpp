/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_top_bar.h"
#include "info/profile/info_profile_top_bar_action_button.h"
#include "lang/lang_keys.h"
#include "media/stories/media_stories_post.h"
#include "settings/sections/settings_information.h"
#include "ui/widgets/horizontal_fit_container.h"
#include "window/window_session_controller.h"

#include "styles/style_info_profile_top_bar.h"

namespace Info::Profile {

void TopBar::setupMyProfileActions(
		not_null<Window::SessionController*> controller,
		Fn<void()> choosePhoto) {
	_actions = base::make_unique_q<Ui::HorizontalFitContainer>(
		this,
		st::infoProfileTopBarActionButtonsSpace);
	auto buttons = std::vector<not_null<TopBarActionButton*>>();
	const auto add = [&](
		rpl::producer<QString> text,
		const style::icon &icon) {
		const auto button = Ui::CreateChild<TopBarActionButton>(
			this,
			QString(),
			icon);
		std::move(text) | rpl::on_next([=](const QString &label) {
			button->setText(label);
			button->setAccessibleName(label);
		}, button->lifetime());
		_actions->add(button);
		buttons.push_back(button);
		return button;
	};

	add(
		tr::lng_profile_set_photo_button(),
		st::infoProfileTopBarActionSetPhoto
	)->setClickedCallback(std::move(choosePhoto));

	const auto peer = _peer;
	if (::Media::Stories::CanPostStory(peer)) {
		const auto story = add(
			tr::lng_story_add_button(),
			st::infoProfileTopBarActionStory);
		story->setClickedCallback([=] {
			::Media::Stories::PostStory(
				controller,
				peer,
				{
					.setLoading = crl::guard(
						story,
						[=](bool loading) {
							story->setDisabled(
								loading);
						}),
				});
		});
		setupStoryDrop(story, controller);
	}

	add(
		tr::lng_profile_action_short_edit_info(),
		st::infoProfileTopBarActionEditInfo
	)->setClickedCallback([=] {
		controller->showSettings(::Settings::InformationId());
	});

	finalizeActions(buttons);
}

} // namespace Info::Profile
