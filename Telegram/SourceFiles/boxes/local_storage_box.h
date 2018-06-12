/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"

namespace Ui {
class LinkButton;
} // namespace Ui

class LocalStorageBox : public BoxContent {
	Q_OBJECT

public:
	LocalStorageBox(QWidget*);

private slots:
	void onTempDirCleared(int task);
	void onTempDirClearFailed(int task);

protected:
	void prepare() override;

	void paintEvent(QPaintEvent *e) override;

private:
	void clearStorage();
	void updateControls();
	void checkLocalStoredCounts();

	enum class State {
		Normal,
		Clearing,
		Cleared,
		ClearFailed,
	};
	State _state = State::Normal;

	object_ptr<Ui::LinkButton> _clear;

	int _imagesCount = -1;
	int _audiosCount = -1;

};
