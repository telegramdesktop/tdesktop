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

class TypingBox : public BoxContent {
	Q_OBJECT

public:
	TypingBox(QWidget *parent);

protected:
	void prepare() override;

	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private slots:
	void onSave();

private:
	object_ptr<Ui::Checkbox> _onlineContact;
	object_ptr<Ui::Checkbox> _onlineEveryone;

	object_ptr<Ui::Checkbox> _typingPrivateContact;
	object_ptr<Ui::Checkbox> _typingGroupContact;
	object_ptr<Ui::Checkbox> _typingSupergroupContact;

	object_ptr<Ui::Checkbox> _typingPrivate;
	object_ptr<Ui::Checkbox> _typingGroup;
	object_ptr<Ui::Checkbox> _typingSupergroup;

	Text _about;

	int _sectionHeight1 = 0;
	int _sectionHeight2 = 0;

};
