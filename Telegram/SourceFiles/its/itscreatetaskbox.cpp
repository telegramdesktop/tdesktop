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
//
#include "stdafx.h"
#include "lang.h"

#include "application.h"
#include "itscreatetaskbox.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "localstorage.h"

ITSCreateTaskBox::ITSCreateTaskBox(QString taskTitle, QString taskDescription) : AbstractBox(1000),
_createTask(this, qsl("Create task"), st::defaultBoxButton),
_cancel(this, lang(lng_cancel), st::cancelBoxButton),
_taskTitle(this, st::defaultInputField, qsl("Task title")),
_taskDescription(this, st::taskDescriptionFlat, qsl("Task description")),
_openInBrowser(this, qsl("Open created task in browser"), cOpenCreatedTaskInBrowser()){

	ITSBitrix24& bitrix24 = ITSBitrix24::Instance();
	bitrix24.loadConfigFromLocalStorage();

	setBlueTitle(true);

	resizeMaxHeight(1000, 700);

	_taskDescription.setMinHeight(500);
	_taskDescription.setMaxHeight(500);
		
	_taskDescription.setLineWrapMode(QTextEdit::NoWrap);
	_taskDescription.setFrameShape(QFrame::Shape::VLine);
	_taskDescription.verticalScrollBar()->show();	
	_taskDescription.verticalScrollBar()->setVisible(true);
	_taskDescription.ensureCursorVisible();
	_taskDescription.setSubmitSettings(FlatTextarea::SubmitSettings::CtrlEnter);

	_taskDescription.setStyleSheet("border-color: #e0e0e0; border-width: 2px; border-style: solid;");
	connect(&_taskDescription, &FlatTextarea::focusIn, [&](){
		_taskDescription.setStyleSheet("border-color: #62c0f7; border-width: 2px; border-style: solid;");
	});

	connect(&_taskDescription, &FlatTextarea::focusOut, [&]() {
		_taskDescription.setStyleSheet("border-color: #e0e0e0; border-width: 2px; border-style: solid;");
	});

	_taskTitle.setText(prepareText(taskTitle));
	_taskDescription.setText(prepareText(taskDescription));

	connect(&_taskTitle, SIGNAL(changed()), this, SLOT(onTaskTitleChanged()));
	connect(&_taskDescription, SIGNAL(changed()), this, SLOT(onTaskDescriptionChanged()));

	connect(&_createTask, SIGNAL(clicked()), this, SLOT(onCreateTaskClicked()));
	connect(&_cancel, SIGNAL(clicked()), this, SLOT(onClose()));

	connect(&_openInBrowser, SIGNAL(changed()), this, SLOT(onOpenInBrowserChanged()));

	prepare();
}

void ITSCreateTaskBox::hideAll() {

	_createTask.hide();
	_cancel.hide();
	_taskTitle.hide();
	_taskDescription.hide();
	_openInBrowser.hide();

	AbstractBox::hideAll();
}

void ITSCreateTaskBox::showAll() {
	
	_createTask.show();
	_cancel.show();
	_taskTitle.show();
	_taskDescription.show();
	_openInBrowser.show();

	AbstractBox::showAll();
}

void ITSCreateTaskBox::showDone() {
	_taskTitle.setFocus();
}

void ITSCreateTaskBox::paintEvent(QPaintEvent *e) {

	Painter p(this);
	if (paint(p)) return;

	paintTitle(p, qsl("Create task"));

}

void ITSCreateTaskBox::resizeEvent(QResizeEvent *e) {

	int32 y = st::boxTitleHeight + st::usernamePadding.top();

	_taskTitle.resize(width() - st::usernamePadding.left() - st::usernamePadding.right(), _taskTitle.height());
	_taskTitle.moveToLeft(st::usernamePadding.left(), y);
	y += st::usernamePadding.top() + _taskTitle.height();

	_taskDescription.resize(width() - st::usernamePadding.left() - st::usernamePadding.right(), _taskDescription.height());
	_taskDescription.moveToLeft(st::usernamePadding.left(), y);
	y += st::usernamePadding.top() + _taskDescription.height();
	
	_createTask.moveToRight(st::boxButtonPadding.right(), height() - st::boxButtonPadding.bottom() - _createTask.height());
	_cancel.moveToRight(st::boxButtonPadding.right() + _createTask.width() + st::boxButtonPadding.left(), _createTask.y());
	_openInBrowser.moveToLeft(st::usernamePadding.left(), _createTask.y() + 10);

	AbstractBox::resizeEvent(e);
}

QString ITSCreateTaskBox::prepareText(QString text) {

	QString clearedText;
	for (int i = 0; i < text.length(); i++) {
		if (text.data()[i] < 0x500) {
			clearedText.append(text.data()[i]);
		}
	}
	return clearedText;
}

void ITSCreateTaskBox::onTaskTitleChanged() {
	//_taskTitle.setText(prepareText(_taskTitle.text()));
}

void ITSCreateTaskBox::onTaskDescriptionChanged() {
	//_taskDescription.setText(prepareText(_taskDescription.getLastText()));	
}

void ITSCreateTaskBox::onOpenInBrowserChanged() {
	cSetOpenCreatedTaskInBrowser(_openInBrowser.checked());
	Local::writeUserSettings();
}

void ITSCreateTaskBox::onCreateTaskClicked() {

	ITSBitrix24& bitrix24 = ITSBitrix24::Instance();
	bitrix24.loadConfigFromLocalStorage();

	QString
		preparedTaskTitle = prepareText(_taskTitle.text()),
		preparedTaskDescription = prepareText(_taskDescription.getLastText());

	auto connection = std::make_shared<QMetaObject::Connection>();
	*connection = QObject::connect(&bitrix24, &ITSBitrix24::createTaskFinished, [this, connection, &bitrix24, preparedTaskTitle, preparedTaskDescription](bool success, QString errorDescription) {
	
		QObject::disconnect(*connection);

		if (success) {
			QString createdTaskUrl = QString("%1/workgroups/group/%2/tasks/task/view/%3/").arg(bitrix24.getPortalUrl(), QString::number(bitrix24.getDefaultGroupId()), errorDescription);
			if (_openInBrowser.checked()) {
				QDesktopServices::openUrl(createdTaskUrl);
				emit closed();
			}
			else {
				InformBox *box = new InformBox("Task create success.", "Ok", st::defaultBoxButton);
				//QObject::connect(box, &InformBox::confirmed, [this, connection, &bitrix24, preparedTaskTitle, preparedTaskDescription]() {
				//	emit closed();				
				//});
				Ui::showLayer(box, CloseOtherLayers);
			}
		}
		else {
			InformBox *box = new InformBox("Task create failed. Error:" + errorDescription, "Ok", st::defaultBoxButton);
			Ui::showLayer(box, KeepOtherLayers);
		}
	
		
	});

	bitrix24.createTask(preparedTaskTitle, preparedTaskDescription, true);
}

void ITSCreateTaskBox::onCancelClicked() {

}

void ITSCreateTaskBox::onClose() {
	emit closed();
}
