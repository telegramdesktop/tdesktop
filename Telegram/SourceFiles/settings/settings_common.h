/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

namespace Info {
namespace Profile {
class Button;
} // namespace Profile
} // namespace Info

namespace Settings {

enum class Type {
	Main,
	Information,
	Notifications,
	PrivacySecurity,
	General,
	Chat,
};

using Button = Info::Profile::Button;

class Section : public Ui::RpWidget {
public:
	using RpWidget::RpWidget;

	virtual rpl::producer<Type> sectionShowOther() {
		return rpl::never<Type>();
	}

};

object_ptr<Section> CreateSection(
	Type type,
	not_null<QWidget*> parent,
	UserData *self = nullptr);

} // namespace Settings
