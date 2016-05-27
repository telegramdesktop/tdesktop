/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/

#pragma once

#include "boxes/abstractbox.h"
#include "boxes/confirmbox.h"
#include "its/itsbitrix24.h"
#include <memory>


class ITSCreateTaskBox : public AbstractBox, public RPCSender {
	Q_OBJECT

public:

	ITSCreateTaskBox(QString taskTitle, QString taskDescription);
	void paintEvent(QPaintEvent *e);
	void resizeEvent(QResizeEvent *e);

public slots:

	//void onSendTask();
	void onClose();	
	void onTaskTitleChanged();
	void onTaskDescriptionChanged();
	void onCreateTaskClicked();
	void onCancelClicked();
	void onOpenInBrowserChanged();

protected:

	void hideAll();
	void showAll();
	void showDone();	

private:

	BoxButton
		_createTask,
		_cancel;

	MaskedInputField
		_taskTitle;
	FlatTextarea
		_taskDescription;

	Checkbox
		_openInBrowser;

	QString prepareText(QString text);
};

