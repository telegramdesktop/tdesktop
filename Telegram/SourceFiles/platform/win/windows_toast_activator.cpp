/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/win/windows_toast_activator.h"

#pragma warning(push)
// class has virtual functions, but destructor is not virtual
#pragma warning(disable:4265)
#pragma warning(disable:5104)
#include <wrl/module.h>
#pragma warning(pop)

namespace {

rpl::event_stream<ToastActivation> GlobalToastActivations;

} // namespace

QString ToastActivation::String(LPCWSTR value) {
	const auto length = int(wcslen(value));
	auto result = value
		? QString::fromWCharArray(value, std::min(length, 16384))
		: QString();
	if (result.indexOf(QChar('\n')) < 0) {
		result.replace(QChar('\r'), QChar('\n'));
	}
	return result;
}

HRESULT ToastActivator::Activate(
		_In_ LPCWSTR appUserModelId,
		_In_ LPCWSTR invokedArgs,
		_In_reads_(dataCount) const NOTIFICATION_USER_INPUT_DATA *data,
		ULONG dataCount) {
	DEBUG_LOG(("Toast Info: COM Activated \"%1\" with args \"%2\"."
		).arg(QString::fromWCharArray(appUserModelId)
		).arg(QString::fromWCharArray(invokedArgs)));
	const auto string = &ToastActivation::String;
	auto input = std::vector<ToastActivation::UserInput>();
	input.reserve(dataCount);
	for (auto i = 0; i != dataCount; ++i) {
		input.push_back({
			.key = string(data[i].Key),
			.value = string(data[i].Value),
		});
	}
	auto activation = ToastActivation{
		.args = string(invokedArgs),
		.input = std::move(input),
	};
	crl::on_main([activation = std::move(activation)]() mutable {
		GlobalToastActivations.fire(std::move(activation));
	});
	return S_OK;
}

HRESULT ToastActivator::QueryInterface(
		REFIID riid,
		void **ppObj) {
	if (riid == IID_IUnknown
		|| riid == IID_INotificationActivationCallback) {
		*ppObj = static_cast<INotificationActivationCallback*>(this);
		AddRef();
		return S_OK;
	}

	*ppObj = NULL;
	return E_NOINTERFACE;
}

ULONG ToastActivator::AddRef() {
	return InterlockedIncrement(&_ref);
}

ULONG ToastActivator::Release() {
	long ref = 0;
	ref = InterlockedDecrement(&_ref);
	if (!ref) {
		delete this;
	}
	return ref;
}

rpl::producer<ToastActivation> ToastActivations() {
	return GlobalToastActivations.events();
}

CoCreatableClass(ToastActivator);
