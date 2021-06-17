/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "base/object_ptr.h"

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class VerticalLayout;
class FlatLabel;
class SettingsButton;
class AbstractButton;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace style {
struct SettingsButton;
} // namespace style

namespace Settings {

enum class Type {
	Main,
	Information,
	Notifications,
	PrivacySecurity,
	Advanced,
	Chat,
	Folders,
	Calls,
};

using Button = Ui::SettingsButton;

class Section : public Ui::RpWidget {
public:
	using RpWidget::RpWidget;

	virtual rpl::producer<Type> sectionShowOther() {
		return nullptr;
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
	not_null<Window::SessionController*> controller);

void AddSkip(not_null<Ui::VerticalLayout*> container);
void AddSkip(not_null<Ui::VerticalLayout*> container, int skip);
void AddDivider(not_null<Ui::VerticalLayout*> container);
void AddDividerText(
	not_null<Ui::VerticalLayout*> container,
	rpl::producer<QString> text);
not_null<Ui::RpWidget*> AddButtonIcon(
	not_null<Ui::AbstractButton*> button,
	const style::icon *leftIcon,
	int iconLeft,
	const style::color *leftIconOver);
object_ptr<Button> CreateButton(
	not_null<QWidget*> parent,
	rpl::producer<QString> text,
	const style::SettingsButton &st,
	const style::icon *leftIcon = nullptr,
	int iconLeft = 0,
	const style::color *leftIconOver = nullptr);
not_null<Button*> AddButton(
	not_null<Ui::VerticalLayout*> container,
	rpl::producer<QString> text,
	const style::SettingsButton &st,
	const style::icon *leftIcon = nullptr,
	int iconLeft = 0);
not_null<Button*> AddButtonWithLabel(
	not_null<Ui::VerticalLayout*> container,
	rpl::producer<QString> text,
	rpl::producer<QString> label,
	const style::SettingsButton &st,
	const style::icon *leftIcon = nullptr,
	int iconLeft = 0);
void CreateRightLabel(
	not_null<Button*> button,
	rpl::producer<QString> label,
	const style::SettingsButton &st,
	rpl::producer<QString> buttonText);
not_null<Ui::FlatLabel*> AddSubsectionTitle(
	not_null<Ui::VerticalLayout*> container,
	rpl::producer<QString> text);

using MenuCallback = Fn<QAction*(
	const QString &text,
	Fn<void()> handler)>;

void FillMenu(
	not_null<Window::SessionController*> controller,
	Type type,
	Fn<void(Type)> showOther,
	MenuCallback addAction);

} // namespace Settings
