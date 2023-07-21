/*
This file is part of exteraGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/exteraGramDesktop/exteraGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"

namespace Ui {
class Radiobutton;
} // namespace Ui

class AutoLockBox : public Ui::BoxContent {
public:
	AutoLockBox(QWidget*);

protected:
	void prepare() override;

private:
	void durationChanged(int seconds);

	std::vector<object_ptr<Ui::Radiobutton>> _options;

};
