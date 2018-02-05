/*
This file is part of Telegreat,
modified by Sean.

For license and copyright information please follow this link:
https://git.io/TD
*/
#pragma once

#include "settings/settings_block_widget.h"
#include "ui/rp_widget.h"

namespace Settings {

class GreatWidget : public BlockWidget {
	Q_OBJECT

public:
	GreatWidget(QWidget *parent, UserData *self);

private slots:
	void onRestart();
	void onTyping();

private:
	void refreshControls();
	void onUnstable();
	void onCallbackData();
	void onUsername();
	void onIgnore();
	void onTagMention();
	void onAutoCopy();

	Ui::Checkbox *_enableCallbackData = nullptr;
	Ui::Checkbox *_enableUsername = nullptr;
	Ui::Checkbox *_enableIgnore = nullptr;
	Ui::Checkbox *_enableTagMention = nullptr;
	Ui::Checkbox *_enableAutoCopy = nullptr;
	Ui::Checkbox *_enableUnstable = nullptr;
	Ui::LinkButton *_typing = nullptr;

};

} // namespace Settings
