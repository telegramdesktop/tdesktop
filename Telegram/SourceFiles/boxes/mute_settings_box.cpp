/* This file is part of Telegram Desktop, the official desktop version of
 * Telegram messaging app, see https://desktop.telegram.org
 *
 * This code is in Public Domain, see license terms in .github/CONTRIBUTING.md
 * Copyright (C) 2017, Nicholas Guriev <guriev-ns@ya.ru>
 */

#include "boxes/mute_settings_box.h"

#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "styles/style_boxes.h"
#include "ui/special_buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"

void MuteSettingsBox::prepare() {
	setTitle(langFactory(lng_disable_notifications_from_tray));
	int y = 0;

	object_ptr<Ui::FlatLabel> info(this, st::boxLabel);
	info->setText(lang(lng_mute_box_tip));
	info->moveToLeft(st::boxPadding.left(), y);
	y += info->height() + st::boxLittleSkip;

	object_ptr<Ui::PeerAvatarButton> icon(this, _peer, st::mutePhotoButton);
	icon->setPointerCursor(false);
	icon->moveToLeft(st::boxPadding.left(), y);

	object_ptr<Ui::FlatLabel> title(this, st::muteChatTitle);
	title->setText(App::peerName(_peer, true));
	title->moveToLeft(st::boxPadding.left(),
		y + icon->height() / 2 - title->height() / 2);
	// the icon is always higher than this chat title
	y += icon->height() + st::boxMediumSkip;

	const int FOREVER = 8760;  // in fact, this is mute only for 1 year
	auto group = std::make_shared<Ui::RadiobuttonGroup>(FOREVER);
	y += st::boxOptionListPadding.top();
	for (int value : { 1, 4, 18, 72, FOREVER }) {  // periods in hours
		QString text;
		if (value < 24) {
			text = lng_mute_duration_hours(lt_count, value);
		} else if (value < FOREVER) {
			text = lng_rights_chat_banned_day(lt_count, value / 24);
		} else {
			text = lang(lng_rights_chat_banned_forever);
		}
		object_ptr<Ui::Radiobutton> option(this, group, value, text);
		option->moveToLeft(st::boxPadding.left(), y);
		y += option->heightNoMargins() + st::boxOptionListSkip;
	}
	y += st::boxOptionListPadding.bottom() - st::boxOptionListSkip + st::defaultCheckbox.margin.bottom();

	addButton(langFactory(lng_box_ok), [this, group] {
		App::main()->updateNotifySetting(_peer, NotifySettingSetMuted,
			SilentNotifiesDontChange, group->value() * 3600);
		closeBox();
	});
	addButton(langFactory(lng_cancel), [this] { closeBox(); });

	setDimensions(st::boxWidth, y);
}
// vi: ts=4 tw=80
