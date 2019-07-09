/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {

class FocusPersister {
public:
	FocusPersister(QWidget *parent, QWidget *steal = nullptr)
	: _weak(GrabFocused(parent)) {
		if (steal) {
			steal->setFocus();
		}
	}

	~FocusPersister() {
		if (auto strong = _weak.data()) {
			if (auto window = strong->window()) {
				if (window->focusWidget() != strong) {
					strong->setFocus();
				}
			}
		}
	}

private:
	static QWidget *GrabFocused(QWidget *parent) {
		if (auto window = parent ? parent->window() : nullptr) {
			return window->focusWidget();
		}
		return nullptr;
	}
	QPointer<QWidget> _weak;

};

} // namespace Ui
