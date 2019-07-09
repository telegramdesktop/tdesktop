/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/main_queue_processor.h"

#include "core/sandbox.h"
#include "platform/platform_specific.h"

namespace Core {
namespace {

constexpr auto kProcessorEvent = QEvent::Type(QEvent::User + 1);
static_assert(kProcessorEvent < QEvent::MaxUser);

QMutex ProcessorMutex;
MainQueueProcessor *ProcessorInstance/* = nullptr*/;

enum class ProcessState : int {
	Processed,
	FillingUp,
	Waiting,
};

std::atomic<ProcessState> MainQueueProcessState/* = ProcessState(0)*/;
void (*MainQueueProcessCallback)(void*)/* = nullptr*/;
void *MainQueueProcessArgument/* = nullptr*/;

void PushToMainQueueGeneric(void (*callable)(void*), void *argument) {
	Expects(Platform::UseMainQueueGeneric());

	auto expected = ProcessState::Processed;
	const auto fill = MainQueueProcessState.compare_exchange_strong(
		expected,
		ProcessState::FillingUp);
	if (fill) {
		MainQueueProcessCallback = callable;
		MainQueueProcessArgument = argument;
		MainQueueProcessState.store(ProcessState::Waiting);
	}

	auto event = std::make_unique<QEvent>(kProcessorEvent);

	QMutexLocker lock(&ProcessorMutex);
	if (ProcessorInstance) {
		QApplication::postEvent(ProcessorInstance, event.release());
	}
}

void DrainMainQueueGeneric() {
	Expects(Platform::UseMainQueueGeneric());

	if (MainQueueProcessState.load() != ProcessState::Waiting) {
		return;
	}
	const auto callback = MainQueueProcessCallback;
	const auto argument = MainQueueProcessArgument;
	MainQueueProcessState.store(ProcessState::Processed);

	callback(argument);
}

} // namespace

MainQueueProcessor::MainQueueProcessor() {
	if constexpr (Platform::UseMainQueueGeneric()) {
		acquire();
		crl::init_main_queue(PushToMainQueueGeneric);
	} else {
		crl::wrap_main_queue([](void (*callable)(void*), void *argument) {
			Sandbox::Instance().customEnterFromEventLoop([&] {
				callable(argument);
			});
		});
	}

	Core::Sandbox::Instance().widgetUpdateRequests(
	) | rpl::start_with_next([] {
		if constexpr (Platform::UseMainQueueGeneric()) {
			DrainMainQueueGeneric();
		} else {
			Platform::DrainMainQueue();
		}
	}, _lifetime);

	base::InitObservables([] {
		Global::RefHandleObservables().call();
	});
}

bool MainQueueProcessor::event(QEvent *event) {
	if constexpr (Platform::UseMainQueueGeneric()) {
		if (event->type() == kProcessorEvent) {
			DrainMainQueueGeneric();
			return true;
		}
	}
	return QObject::event(event);
}

void MainQueueProcessor::acquire() {
	Expects(Platform::UseMainQueueGeneric());
	Expects(ProcessorInstance == nullptr);

	QMutexLocker lock(&ProcessorMutex);
	ProcessorInstance = this;
}

void MainQueueProcessor::release() {
	Expects(Platform::UseMainQueueGeneric());
	Expects(ProcessorInstance == this);

	QMutexLocker lock(&ProcessorMutex);
	ProcessorInstance = nullptr;
}

MainQueueProcessor::~MainQueueProcessor() {
	if constexpr (Platform::UseMainQueueGeneric()) {
		release();
	}
}

} // namespace
