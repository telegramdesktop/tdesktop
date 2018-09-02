/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "old_settings/settings_layer.h"

namespace OldSettings {

class InnerWidget;

class Widget : public Layer, private base::Subscriber {
public:
	Widget(QWidget*);

	void refreshLang();
	void scrollToUpdateRow();

	void parentResized() override;

protected:
	void keyPressEvent(QKeyEvent *e) override;

private:
	void resizeUsingInnerHeight(int newWidth, int innerHeight) override;

	QPointer<InnerWidget> _inner;

};

} // namespace Settings
