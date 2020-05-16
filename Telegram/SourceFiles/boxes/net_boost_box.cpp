/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.
For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/net_boost_box.h"

#include "lang/lang_keys.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "boxes/confirm_box.h"
#include "core/enhanced_settings.h"
#include "app.h"

NetBoostBox::NetBoostBox(QWidget* parent)
{
}

void NetBoostBox::prepare() {
	setTitle(rpl::single(Lang::Current().getCustomLangValue("lng_net_speed_boost_title")));

	addButton(tr::lng_settings_save(), [=] { save(); });
	addButton(tr::lng_cancel(), [=] { closeBox(); });

	auto y = st::boxOptionListPadding.top();
	_description.create(
		this,
		Lang::Current().getCustomLangValue("lng_net_speed_boost_desc"),
		st::boxLabel);
	_description->moveToLeft(st::boxPadding.left(), y);

	y += _description->height() + st::boxMediumSkip;

	_boostGroup = std::make_shared<Ui::RadiobuttonGroup>(cNetSpeedBoost());

	for (int i = 0; i <= 3; i++) {
		const auto button = Ui::CreateChild<Ui::Radiobutton>(
			this,
			_boostGroup,
			i,
			BoostLabel(i),
			st::autolockButton);
		button->moveToLeft(st::boxPadding.left(), y);
		y += button->heightNoMargins() + st::boxOptionListSkip;
	}
	showChildren();
	setDimensions(st::boxWidth, y);
}

QString NetBoostBox::BoostLabel(int boost) {
	switch (boost) {
		case 0:
			return Lang::Current().getCustomLangValue("lng_net_speed_boost_default");

		case 1:
			return Lang::Current().getCustomLangValue("lng_net_speed_boost_slight");

		case 2:
			return Lang::Current().getCustomLangValue("lng_net_speed_boost_medium");

		case 3:
			return Lang::Current().getCustomLangValue("lng_net_speed_boost_big");

		default:
			Unexpected("Boost in NetBoostBox::BoostLabel.");
	}
	return QString();
}

void NetBoostBox::save() {
	const auto changeBoost = [=] {
		SetNetworkBoost(_boostGroup->value());
		EnhancedSettings::Write();
		App::restart();
	};

	const auto box = std::make_shared<QPointer<BoxContent>>();

	*box = getDelegate()->show(
		Box<ConfirmBox>(
			Lang::Current().getCustomLangValue("lng_net_boost_restart_desc"),
			tr::lng_settings_restart_now(tr::now),
			tr::lng_cancel(tr::now),
			changeBoost));
}