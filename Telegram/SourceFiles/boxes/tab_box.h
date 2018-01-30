/*
This file is part of Telegreat,
modified by Sean.

For license and copyright information please follow this link:
https://git.io/TD
*/
#pragma once

#include "boxes/abstract_box.h"

namespace Ui {
class Checkbox;
} // namespace Ui

class TabBox : public BoxContent {
	Q_OBJECT

public:
	TabBox(QWidget *parent);

protected:
	void prepare() override;

	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private slots:
	void onHideMute();
	void onSave();

private:
	object_ptr<Ui::Checkbox> _hideMuted;
	object_ptr<Ui::Checkbox> _showUser;
	object_ptr<Ui::Checkbox> _showGroup;
	object_ptr<Ui::Checkbox> _showChannel;
	object_ptr<Ui::Checkbox> _showBot;

	int _sectionHeight = 0;

};
