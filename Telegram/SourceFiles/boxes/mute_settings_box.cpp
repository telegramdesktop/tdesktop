/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

This code is in Public Domain, see license terms in .github/CONTRIBUTING.md
Copyright (C) 2017, Nicholas Guriev <guriev-ns@ya.ru>
*/
#include "boxes/mute_settings_box.h"

#include "lang/lang_keys.h"
#include "auth_session.h"
#include "data/data_session.h"
#include "styles/style_boxes.h"
#include "ui/special_buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"

namespace {

constexpr auto kForeverHours = 24 * 365;

} // namespace

void MuteSettingsBox::prepare() {
	setTitle(langFactory(lng_disable_notifications_from_tray));
	auto y = 0;

	object_ptr<Ui::FlatLabel> info(this, st::boxLabel);
	info->setText(lang(lng_mute_box_tip));
	info->moveToLeft(st::boxPadding.left(), y);
	y += info->height() + st::boxLittleSkip;

	const auto icon = object_ptr<Ui::UserpicButton>(
		this,
		controller(),
		_peer,
		Ui::UserpicButton::Role::Custom,
		st::mutePhotoButton);
	icon->setPointerCursor(false);
	icon->moveToLeft(st::boxPadding.left(), y);

	object_ptr<Ui::FlatLabel> title(this, st::muteChatTitle);
	title->setText(App::peerName(_peer, true));
	title->moveToLeft(
		st::boxPadding.left() + st::muteChatTitleLeft,
		y + (icon->height() / 2) - (title->height() / 2));
	// the icon is always higher than this chat title
	y += icon->height() + st::boxMediumSkip;

	// in fact, this is mute only for 1 year
	const auto group = std::make_shared<Ui::RadiobuttonGroup>(kForeverHours);
	y += st::boxOptionListPadding.top();
	for (const auto hours : { 1, 4, 18, 72, kForeverHours }) {
		const auto text = [&] {
			if (hours < 24) {
				return lng_mute_duration_hours(lt_count, hours);
			} else if (hours < kForeverHours) {
				return lng_rights_chat_banned_day(lt_count, hours / 24);
			} else {
				return lang(lng_rights_chat_banned_forever);
			}
		}();
		object_ptr<Ui::Radiobutton> option(this, group, hours, text);
		option->moveToLeft(st::boxPadding.left(), y);
		y += option->heightNoMargins() + st::boxOptionListSkip;
	}
	y += st::boxOptionListPadding.bottom()
		- st::boxOptionListSkip
		+ st::defaultCheckbox.margin.bottom();

	addButton(langFactory(lng_box_ok), [this, group] {
		auto muteForSeconds = group->value() * 3600;
		Auth().data().updateNotifySettings(
			_peer,
			muteForSeconds);
		closeBox();
	});
	addButton(langFactory(lng_cancel), [this] { closeBox(); });

	setDimensions(st::boxWidth, y);
}
// vi: ts=4 tw=80
