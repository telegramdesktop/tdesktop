/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_privacy_security.h"

#include "settings/settings_common.h"
#include "boxes/abstract_box.h"
#include "ui/wrap/vertical_layout.h"
#include "lang/lang_keys.h"
#include "styles/style_settings.h"

namespace Settings {

PrivacySecurity::PrivacySecurity(QWidget *parent, not_null<UserData*> self)
: Section(parent)
, _self(self) {
	setupContent();
}

void PrivacySecurity::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	content->add(object_ptr<BoxContentDivider>(content));

	Ui::ResizeFitChild(this, content);
}

} // namespace Settings
