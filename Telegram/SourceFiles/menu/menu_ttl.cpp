/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "menu/menu_ttl.h"

#include "lang/lang_keys.h"
#include "ui/boxes/confirm_box.h"
#include "ui/boxes/time_picker_box.h"
#include "ui/layers/generic_box.h"
#include "ui/text/format_values.h"
#include "ui/widgets/labels.h"
#include "styles/style_layers.h"

namespace TTLMenu {

void TTLBox(not_null<Ui::GenericBox*> box, Args args) {
	if (args.about) {
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			std::move(args.about),
			st::boxLabel));
	}

	const auto ttls = std::vector<TimeId>{
		(86400 * 1),
		(86400 * 2),
		(86400 * 3),
		(86400 * 4),
		(86400 * 5),
		(86400 * 6),
		(86400 * 7 * 1),
		(86400 * 7 * 2),
		(86400 * 7 * 3),
		(86400 * 31 * 1),
		(86400 * 31 * 2),
		(86400 * 31 * 3),
		(86400 * 31 * 4),
		(86400 * 31 * 5),
		(86400 * 31 * 6),
		(86400 * 365),
	};
	const auto phrases = ranges::views::all(
		ttls
	) | ranges::views::transform(Ui::FormatTTL) | ranges::to_vector;

	const auto pickerTtl = TimePickerBox(box, ttls, phrases, args.startTtl);

	Ui::ConfirmBox(box, {
		.confirmed = [=](Fn<void()> close) {
			args.callback(pickerTtl(), std::move(close));
		},
		.confirmText = tr::lng_settings_save(),
		.cancelText = tr::lng_cancel(),
	});

	box->setTitle(tr::lng_manage_messages_ttl_title());

	if (args.startTtl && !args.hideDisable) {
		box->addLeftButton(tr::lng_manage_messages_ttl_disable(), [=] {
			args.callback(0, [=] { box->closeBox(); });
		});
	}
}

} // namespace TTLMenu
