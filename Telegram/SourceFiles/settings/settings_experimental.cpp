/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_experimental.h"

#include "ui/wrap/vertical_layout.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "chat_helpers/tabbed_panel.h"
#include "lang/lang_keys.h"
#include "styles/style_settings.h"
#include "styles/style_layers.h"

namespace Settings {
namespace {

void SetupExperimental(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	AddSkip(container, st::settingsCheckboxesSkip);

	container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			tr::lng_settings_experimental_about(),
			st::boxLabel),
		st::settingsDividerLabelPadding);

	AddDivider(container);

	AddSkip(container, st::settingsCheckboxesSkip);
}

} // namespace

Experimental::Experimental(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent) {
	setupContent(controller);
}

void Experimental::setupContent(
		not_null<Window::SessionController*> controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	SetupExperimental(controller, content);

	Ui::ResizeFitChild(this, content);
}

} // namespace Settings
