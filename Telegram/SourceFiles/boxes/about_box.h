/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"

namespace Ui {
class LinkButton;
class FlatLabel;
} // namespace Ui

class AboutBox : public Ui::BoxContent {
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
