/*
This file is part of Telegram Desktop,
an official desktop messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://tdesktop.com
*/
#pragma once

#include "gui/flatinput.h"

class CountryCodeInput : public FlatInput {
	Q_OBJECT

public:

	CountryCodeInput(QWidget *parent, const style::flatInput &st);

public slots:

	void startErasing(QKeyEvent *e);
	void codeSelected(const QString &code);

signals:

	void codeChanged(const QString &code);
	void addedToNumber(const QString &added);

protected:

	void correctValue(QKeyEvent *e, const QString &was);

private:

	bool _nosignal;

};
