/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_render_thread.h"

#include "base/invoke_queued.h"

#include <QtCore/QDeadlineTimer>
#include <QtCore/QThread>

namespace Tgw {
namespace {

constexpr auto kFinishTimeout = crl::time(2000);
constexpr auto kStackSize = uint(8 * 1024 * 1024);

struct Instance {
	QThread thread;
	QObject context;
	std::atomic<bool> finishing = false;
};

std::atomic<Instance*> Current = nullptr;

[[nodiscard]] Instance &Started() {
	static const auto instance = [] {
		const auto result = new Instance();
		result->thread.setObjectName(u"widget.render"_q);
		result->context.moveToThread(&result->thread);
		result->thread.setStackSize(kStackSize);
		result->thread.start();
		Current = result;
		return result;
	}();
	return *instance;
}

} // namespace

void PostToRenderThread(Fn<void()> task) {
	auto &instance = Started();
	if (!instance.finishing) {
		InvokeQueued(&instance.context, std::move(task));
	}
}

bool RenderThreadStarted() {
	return (Current.load() != nullptr);
}

bool OnRenderThread() {
	const auto instance = Current.load();
	return instance && (QThread::currentThread() == &instance->thread);
}

bool RenderThreadBarrier(Fn<void()> done) {
	const auto instance = Current.load();
	if (!instance || instance->finishing) {
		return false;
	}
	InvokeQueued(&instance->context, [done = std::move(done)] {
		crl::on_main(done);
	});
	return true;
}

void FinishRenderThread(Fn<void()> last) {
	const auto instance = Current.load();
	if (!instance || instance->finishing.exchange(true)) {
		return;
	}
	InvokeQueued(&instance->context, [=] {
		last();
		instance->thread.quit();
	});
	if (!instance->thread.wait(QDeadlineTimer(kFinishTimeout))) {
		LOG(("Tgw Error: The widget render thread did not finish in %1 ms."
			).arg(kFinishTimeout));
	}
}

} // namespace Tgw
