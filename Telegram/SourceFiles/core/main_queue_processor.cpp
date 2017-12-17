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
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "core/main_queue_processor.h"

namespace Core {
namespace {

QMutex ProcessorMutex;
MainQueueProcessor *ProcessorInstance/* = nullptr*/;

constexpr auto kProcessorEvent = QEvent::Type(QEvent::User + 1);
static_assert(kProcessorEvent < QEvent::MaxUser);

class ProcessorEvent : public QEvent {
public:
	ProcessorEvent(void (*callable)(void*), void *argument);

	void process();

private:
	void (*_callable)(void*) = nullptr;
	void *_argument = nullptr;

};

ProcessorEvent::ProcessorEvent(void (*callable)(void*), void *argument)
: QEvent(kProcessorEvent)
, _callable(callable)
, _argument(argument) {
}

void ProcessorEvent::process() {
	_callable(_argument);
}

void ProcessMainQueue(void (*callable)(void*), void *argument) {
	QMutexLocker lock(&ProcessorMutex);

	if (ProcessorInstance) {
		const auto event = new ProcessorEvent(callable, argument);
		QCoreApplication::postEvent(ProcessorInstance, event);
	}
}

} // namespace

MainQueueProcessor::MainQueueProcessor() {
	acquire();
	crl::init_main_queue(ProcessMainQueue);
}

bool MainQueueProcessor::event(QEvent *event) {
	if (event->type() == kProcessorEvent) {
		static_cast<ProcessorEvent*>(event)->process();
		return true;
	}
	return QObject::event(event);
}

void MainQueueProcessor::acquire() {
	Expects(ProcessorInstance == nullptr);

	QMutexLocker lock(&ProcessorMutex);
	ProcessorInstance = this;
}

void MainQueueProcessor::release() {
	Expects(ProcessorInstance == this);

	QMutexLocker lock(&ProcessorMutex);
	ProcessorInstance = nullptr;
}

MainQueueProcessor::~MainQueueProcessor() {
	release();
}

} // namespace
