#include <windows.h>

extern "C" {
using EventSetInformationPtr = unsigned long (WINAPI *)(
	unsigned long long regHandle,
	int informationClass,
	void *eventInformation,
	unsigned long informationLength);

static unsigned long WINAPI EventSetInformationFallback(
		unsigned long long regHandle,
		int informationClass,
		void *eventInformation,
		unsigned long informationLength) {
	return ERROR_NOT_SUPPORTED;
}

static unsigned long WINAPI EventSetInformationDynamic(
		unsigned long long regHandle,
		int informationClass,
		void *eventInformation,
		unsigned long informationLength) {
	static const auto func = reinterpret_cast<EventSetInformationPtr>(
		::GetProcAddress(
			::GetModuleHandleW(L"advapi32.dll"),
			"EventSetInformation"));
	if (!func) {
		return EventSetInformationFallback(
			regHandle,
			informationClass,
			eventInformation,
			informationLength);
	}
	return (*func)(
		regHandle,
		informationClass,
		eventInformation,
		informationLength);
}

unsigned long (WINAPI *__imp_EventSetInformation)(
	unsigned long long,
	int,
	void *,
	unsigned long) = &EventSetInformationDynamic;
} // extern "C"
