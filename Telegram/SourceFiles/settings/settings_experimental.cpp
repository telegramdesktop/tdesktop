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
#include "base/options.h"
#include "chat_helpers/tabbed_panel.h"
#include "lang/lang_keys.h"
#include "styles/style_settings.h"
#include "styles/style_layers.h"

namespace Settings {
namespace {

void AddOption(
		not_null<Ui::VerticalLayout*> container,
		base::options::option<bool> &option) {
	const auto name = option.name().isEmpty() ? option.id() : option.name();
	AddButton(
		container,
		rpl::single(name),
		st::settingsButton
	)->toggleOn(rpl::single(option.value()))->toggledChanges(
	) | rpl::start_with_next([=, &option](bool toggled) {
		option.set(toggled);
	}, container->lifetime());

	const auto &description = option.description();
	if (!description.isEmpty()) {
		AddSkip(container, st::settingsCheckboxesSkip);
		AddDividerText(container, rpl::single(description));
	}
}

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

	const auto addToggle = [&](const char name[]) {
		AddOption(container, base::options::lookup<bool>(name));
	};

	addToggle(ChatHelpers::kOptionTabbedPanelShowOnClick);

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
