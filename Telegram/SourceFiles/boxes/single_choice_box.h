/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"
#include <vector>

enum LangKey : int;

namespace Ui {
class Radiobutton;
} // namespace Ui

class SingleChoiceBox : public BoxContent {
public:
	SingleChoiceBox(QWidget*, LangKey title, std::vector<QString> optionTexts, int initialSelection, Fn<void(int)> callback) : _title(title), _optionTexts(optionTexts), _initialSelection(initialSelection), _callback(callback) {
	}

protected:
	void prepare() override;

private:
	LangKey _title;
	std::vector<QString> _optionTexts;
	int _initialSelection = 0;
	Fn<void(int)> _callback;
	std::vector<object_ptr<Ui::Radiobutton>> _options;

};

