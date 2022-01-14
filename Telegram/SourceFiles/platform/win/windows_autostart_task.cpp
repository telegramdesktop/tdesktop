/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/win/windows_autostart_task.h"

#include "base/platform/win/base_windows_winrt.h"

#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>

namespace Platform::AutostartTask {
namespace {

using namespace winrt::Windows::ApplicationModel;
using namespace winrt::Windows::System;
using namespace winrt::Windows::Foundation;

[[nodiscard]] bool IsEnabled(StartupTaskState state) {
	switch (state) {
	case StartupTaskState::Enabled:
	case StartupTaskState::EnabledByPolicy:
		return true;
	case StartupTaskState::Disabled:
	case StartupTaskState::DisabledByPolicy:
	case StartupTaskState::DisabledByUser:
	default:
		return false;
	}
}

} // namespace

void Toggle(bool enabled, Fn<void(bool)> done) {
	if (!base::WinRT::Supported()) {
		return;
	}
	const auto processEnableResult = [=](StartupTaskState state) {
		LOG(("Startup Task: Enable finished, state: %1").arg(int(state)));

		done(IsEnabled(state));
	};
	const auto processTask = [=](StartupTask task) {
		LOG(("Startup Task: Got it, state: %1, requested: %2"
			).arg(int(task.State())
			).arg(Logs::b(enabled)));

		if (IsEnabled(task.State()) == enabled) {
			return;
		}
		if (!enabled) {
			LOG(("Startup Task: Disabling."));
			task.Disable();
			return;
		}
		LOG(("Startup Task: Requesting enable."));
		const auto asyncState = task.RequestEnableAsync();
		if (!done) {
			return;
		}
		asyncState.Completed([=](
				IAsyncOperation<StartupTaskState> operation,
				AsyncStatus status) {
			base::WinRT::Try([&] {
				processEnableResult(operation.GetResults());
			});
		});
	};
	base::WinRT::Try([&] {
		StartupTask::GetAsync(L"TelegramStartupTask").Completed([=](
				IAsyncOperation<StartupTask> operation,
				AsyncStatus status) {
			base::WinRT::Try([&] {
				processTask(operation.GetResults());
			});
		});
	});
}

void RequestState(Fn<void(bool)> callback) {
	Expects(callback != nullptr);

	if (!base::WinRT::Supported()) {
		return;
	}
	const auto processTask = [=](StartupTask task) {
		DEBUG_LOG(("Startup Task: Got value, state: %1"
			).arg(int(task.State())));

		callback(IsEnabled(task.State()));
	};
	base::WinRT::Try([&] {
		StartupTask::GetAsync(L"TelegramStartupTask").Completed([=](
				IAsyncOperation<StartupTask> operation,
				AsyncStatus status) {
			base::WinRT::Try([&] {
				processTask(operation.GetResults());
			});
		});
	});
}

void OpenSettings() {
	Launcher::LaunchUriAsync(Uri(L"ms-settings:startupapps"));
}

} // namespace Platform::AutostartTask
