/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "settings/settings_common.h"

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Settings {

class Sessions : public Section<Sessions> {
public:
	Sessions(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent(not_null<Window::SessionController*> controller);

};

void AddSessionInfoRow(
	not_null<Ui::VerticalLayout*> container,
	rpl::producer<QString> label,
	const QString &value,
	const style::icon &icon);

} // namespace Settings
