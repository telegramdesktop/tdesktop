/*
This file is part of Telegram Desktop x64,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/TDesktop-x64/tdesktop/blob/dev/LEGAL
*/
#pragma once

#include "settings/settings_common.h"

class BoxContent;

namespace Settings {
	void SetupEnhancedNetwork(not_null<Ui::VerticalLayout *> container);
	void SetupEnhancedMessages(not_null<Ui::VerticalLayout *> container);
	void SetupEnhancedButton(not_null<Ui::VerticalLayout *> container);

	class Enhanced : public Section {
	public:
		Enhanced(
				QWidget *parent,
				not_null<Window::SessionController *> controller);

	private:
		void setupContent(not_null<Window::SessionController *> controller);

	};

} // namespace Settings
