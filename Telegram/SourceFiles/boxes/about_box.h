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
class FlatLabel;
} // namespace Ui

class AboutBox : public BoxContent {
public:
	AboutBox(QWidget*);

protected:
	void prepare() override;

	void resizeEvent(QResizeEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

private:
	void showVersionHistory();

	object_ptr<Ui::LinkButton> _version;
	object_ptr<Ui::FlatLabel> _text1;
	object_ptr<Ui::FlatLabel> _text2;
	object_ptr<Ui::FlatLabel> _text3;

};

QString telegramFaqLink();
QString currentVersionText();
