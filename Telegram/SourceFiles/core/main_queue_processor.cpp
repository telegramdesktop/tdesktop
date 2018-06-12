/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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
