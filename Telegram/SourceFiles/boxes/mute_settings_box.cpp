/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

This code is in Public Domain, see license terms in .github/CONTRIBUTING.md
Copyright (C) 2017, Nicholas Guriev <guriev-ns@ya.ru>
*/
#include "boxes/mute_settings_box.h"

#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_peer.h"
#include "ui/special_buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"

namespace {

constexpr auto kForeverHours = 24 * 365;

} // namespace

MuteSettingsBox::MuteSettingsBox(QWidget *parent, not_null<PeerData*> peer)
: _peer(peer) {
}

void MuteSettingsBox::prepare() {
	setTitle(tr::lng_disable_notifications_from_tray());
	auto y = 0;

	object_ptr<Ui::FlatLabel> info(this, st::boxLabel);
	info->setText(tr::lng_mute_box_tip(tr::now));
	info->moveToLeft(st::boxPadding.left(), y);
	y += info->height() + st::boxLittleSkip;

	const auto icon = object_ptr<Ui::UserpicButton>(
		this,
		_peer,
		Ui::UserpicButton::Role::Custom,
		st::mutePhotoButton);
	icon->setPointerCursor(false);
	icon->moveToLeft(st::boxPadding.left(), y);

	object_ptr<Ui::FlatLabel> title(this, st::muteChatTitle);
	title->setText(_peer->name);
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
				return tr::lng_mute_duration_hours(tr::now, lt_count, hours);
			} else if (hours < kForeverHours) {
				return tr::lng_mute_duration_days(tr::now, lt_count, hours / 24);
			} else {
				return tr::lng_mute_duration_forever(tr::now);
			}
		}();
		object_ptr<Ui::Radiobutton> option(this, group, hours, text);
		option->moveToLeft(st::boxPadding.left(), y);
		y += option->heightNoMargins() + st::boxOptionListSkip;
	}
	y += st::boxOptionListPadding.bottom()
		- st::boxOptionListSkip
		+ st::defaultCheckbox.margin.bottom();

	_save = [=] {
		const auto muteForSeconds = group->value() * 3600;
		_peer->owner().updateNotifySettings(
			_peer,
			muteForSeconds);
		closeBox();
	};
	addButton(tr::lng_box_ok(), _save);
	addButton(tr::lng_cancel(), [this] { closeBox(); });

	setDimensions(st::boxWidth, y);
}

void MuteSettingsBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		if (_save) {
			_save();
		}
	}
}

// vi: ts=4 tw=80
