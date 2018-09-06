/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Window {
class Controller;
} // namespace Window

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
	not_null<Window::Controller*> controller,
	UserData *self = nullptr);

void AddSkip(not_null<Ui::VerticalLayout*> container);
void AddDivider(not_null<Ui::VerticalLayout*> container);

using MenuCallback = Fn<QAction*(
	const QString &text,
	Fn<void()> handler)>;

void FillMenu(
	Fn<void(Type)> showOther,
	MenuCallback addAction);

} // namespace Settings
