/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

enum LangKey : int;

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

namespace style {
struct InfoProfileButton;
} // namespace style

namespace Settings {

enum class Type {
	Main,
	Information,
	Notifications,
	PrivacySecurity,
	Advanced,
	Chat,
	Calls,
};

using Button = Info::Profile::Button;

class Section : public Ui::RpWidget {
public:
	using RpWidget::RpWidget;

	virtual rpl::producer<Type> sectionShowOther() {
		return rpl::never<Type>();
	}
	virtual rpl::producer<bool> sectionCanSaveChanges() {
		return rpl::single(false);
	}
	virtual void sectionSaveChanges(FnMut<void()> done) {
		done();
	}

};

object_ptr<Section> CreateSection(
	Type type,
	not_null<QWidget*> parent,
	Window::Controller *controller = nullptr,
	UserData *self = nullptr);

void AddSkip(not_null<Ui::VerticalLayout*> container);
void AddSkip(not_null<Ui::VerticalLayout*> container, int skip);
void AddDivider(not_null<Ui::VerticalLayout*> container);
void AddDividerText(
	not_null<Ui::VerticalLayout*> container,
	rpl::producer<QString> text);
not_null<Button*> AddButton(
	not_null<Ui::VerticalLayout*> container,
	LangKey text,
	const style::InfoProfileButton &st,
	const style::icon *leftIcon = nullptr,
	int iconLeft = 0);
not_null<Button*> AddButton(
	not_null<Ui::VerticalLayout*> container,
	rpl::producer<QString> text,
	const style::InfoProfileButton &st,
	const style::icon *leftIcon = nullptr,
	int iconLeft = 0);
not_null<Button*> AddButtonWithLabel(
	not_null<Ui::VerticalLayout*> container,
	LangKey text,
	rpl::producer<QString> label,
	const style::InfoProfileButton &st,
	const style::icon *leftIcon = nullptr,
	int iconLeft = 0);
void CreateRightLabel(
	not_null<Button*> button,
	rpl::producer<QString> label,
	const style::InfoProfileButton &st,
	LangKey buttonText);
void AddSubsectionTitle(
	not_null<Ui::VerticalLayout*> container,
	rpl::producer<QString> text);
void AddSubsectionTitle(
	not_null<Ui::VerticalLayout*> conatiner,
	LangKey text);

using MenuCallback = Fn<QAction*(
	const QString &text,
	Fn<void()> handler)>;

void FillMenu(
	Fn<void(Type)> showOther,
	MenuCallback addAction);

} // namespace Settings
