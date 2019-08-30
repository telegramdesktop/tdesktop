/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/win/notifications_manager_win.h"

#include "window/notifications_utilities.h"
#include "platform/win/windows_app_user_model_id.h"
#include "platform/win/windows_event_filter.h"
#include "platform/win/windows_dlls.h"
#include "history/history.h"
#include "mainwindow.h"

#include <Shobjidl.h>
#include <shellapi.h>

#include <roapi.h>
#include <wrl/client.h>
#include "platform/win/wrapper_wrl_implements_h.h"
#include <windows.ui.notifications.h>

#include <strsafe.h>
#include <intsafe.h>

HICON qt_pixmapToWinHICON(const QPixmap &);

using namespace Microsoft::WRL;
using namespace ABI::Windows::UI::Notifications;
using namespace ABI::Windows::Data::Xml::Dom;
using namespace Windows::Foundation;

namespace Platform {
namespace Notifications {
namespace {

class StringReferenceWrapper {
public:
	StringReferenceWrapper(_In_reads_(length) PCWSTR stringRef, _In_ UINT32 length) throw() {
		HRESULT hr = Dlls::WindowsCreateStringReference(stringRef, length, &_header, &_hstring);
		if (!SUCCEEDED(hr)) {
			RaiseException(static_cast<DWORD>(STATUS_INVALID_PARAMETER), EXCEPTION_NONCONTINUABLE, 0, nullptr);
		}
	}

	~StringReferenceWrapper() {
		Dlls::WindowsDeleteString(_hstring);
	}

	template <size_t N>
	StringReferenceWrapper(_In_reads_(N) wchar_t const (&stringRef)[N]) throw() {
		UINT32 length = N - 1;
		HRESULT hr = Dlls::WindowsCreateStringReference(stringRef, length, &_header, &_hstring);
		if (!SUCCEEDED(hr)) {
			RaiseException(static_cast<DWORD>(STATUS_INVALID_PARAMETER), EXCEPTION_NONCONTINUABLE, 0, nullptr);
		}
	}

	template <size_t _>
	StringReferenceWrapper(_In_reads_(_) wchar_t(&stringRef)[_]) throw() {
		UINT32 length;
		HRESULT hr = SizeTToUInt32(wcslen(stringRef), &length);
		if (!SUCCEEDED(hr)) {
			RaiseException(static_cast<DWORD>(STATUS_INVALID_PARAMETER), EXCEPTION_NONCONTINUABLE, 0, nullptr);
		}

		Dlls::WindowsCreateStringReference(stringRef, length, &_header, &_hstring);
	}

	HSTRING Get() const throw() {
		return _hstring;
	}

private:
	HSTRING _hstring;
	HSTRING_HEADER _header;

};

template<class T>
_Check_return_ __inline HRESULT _1_GetActivationFactory(_In_ HSTRING activatableClassId, _COM_Outptr_ T** factory) {
	return Dlls::RoGetActivationFactory(activatableClassId, IID_INS_ARGS(factory));
}

template<typename T>
inline HRESULT wrap_GetActivationFactory(_In_ HSTRING activatableClassId, _Inout_ Details::ComPtrRef<T> factory) throw() {
	return _1_GetActivationFactory(activatableClassId, factory.ReleaseAndGetAddressOf());
}

bool init() {
	if (QSysInfo::windowsVersion() < QSysInfo::WV_WINDOWS8) {
		return false;
	}
	if ((Dlls::SetCurrentProcessExplicitAppUserModelID == nullptr)
		|| (Dlls::PropVariantToString == nullptr)
		|| (Dlls::RoGetActivationFactory == nullptr)
		|| (Dlls::WindowsCreateStringReference == nullptr)
		|| (Dlls::WindowsDeleteString == nullptr)) {
		return false;
	}

	if (!AppUserModelId::validateShortcut()) {
		return false;
	}

	auto appUserModelId = AppUserModelId::getId();
	if (!SUCCEEDED(Dlls::SetCurrentProcessExplicitAppUserModelID(appUserModelId))) {
		return false;
	}
	return true;
}

HRESULT SetNodeValueString(_In_ HSTRING inputString, _In_ IXmlNode *node, _In_ IXmlDocument *xml) {
	ComPtr<IXmlText> inputText;

	HRESULT hr = xml->CreateTextNode(inputString, &inputText);
	if (!SUCCEEDED(hr)) return hr;
	ComPtr<IXmlNode> inputTextNode;

	hr = inputText.As(&inputTextNode);
	if (!SUCCEEDED(hr)) return hr;

	ComPtr<IXmlNode> pAppendedChild;
	return node->AppendChild(inputTextNode.Get(), &pAppendedChild);
}

HRESULT SetAudioSilent(_In_ IXmlDocument *toastXml) {
	ComPtr<IXmlNodeList> nodeList;
	HRESULT hr = toastXml->GetElementsByTagName(StringReferenceWrapper(L"audio").Get(), &nodeList);
	if (!SUCCEEDED(hr)) return hr;

	ComPtr<IXmlNode> audioNode;
	hr = nodeList->Item(0, &audioNode);
	if (!SUCCEEDED(hr)) return hr;

	if (audioNode) {
		ComPtr<IXmlElement> audioElement;
		hr = audioNode.As(&audioElement);
		if (!SUCCEEDED(hr)) return hr;

		hr = audioElement->SetAttribute(StringReferenceWrapper(L"silent").Get(), StringReferenceWrapper(L"true").Get());
		if (!SUCCEEDED(hr)) return hr;
	} else {
		ComPtr<IXmlElement> audioElement;
		hr = toastXml->CreateElement(StringReferenceWrapper(L"audio").Get(), &audioElement);
		if (!SUCCEEDED(hr)) return hr;

		hr = audioElement->SetAttribute(StringReferenceWrapper(L"silent").Get(), StringReferenceWrapper(L"true").Get());
		if (!SUCCEEDED(hr)) return hr;

		ComPtr<IXmlNode> audioNode;
		hr = audioElement.As(&audioNode);
		if (!SUCCEEDED(hr)) return hr;

		ComPtr<IXmlNodeList> nodeList;
		hr = toastXml->GetElementsByTagName(StringReferenceWrapper(L"toast").Get(), &nodeList);
		if (!SUCCEEDED(hr)) return hr;

		ComPtr<IXmlNode> toastNode;
		hr = nodeList->Item(0, &toastNode);
		if (!SUCCEEDED(hr)) return hr;

		ComPtr<IXmlNode> appendedNode;
		hr = toastNode->AppendChild(audioNode.Get(), &appendedNode);
	}
	return hr;
}

HRESULT SetImageSrc(_In_z_ const wchar_t *imagePath, _In_ IXmlDocument *toastXml) {
	wchar_t imageSrc[MAX_PATH] = L"file:///";
	HRESULT hr = StringCchCat(imageSrc, ARRAYSIZE(imageSrc), imagePath);
	if (!SUCCEEDED(hr)) return hr;

	ComPtr<IXmlNodeList> nodeList;
	hr = toastXml->GetElementsByTagName(StringReferenceWrapper(L"image").Get(), &nodeList);
	if (!SUCCEEDED(hr)) return hr;

	ComPtr<IXmlNode> imageNode;
	hr = nodeList->Item(0, &imageNode);
	if (!SUCCEEDED(hr)) return hr;

	ComPtr<IXmlNamedNodeMap> attributes;
	hr = imageNode->get_Attributes(&attributes);
	if (!SUCCEEDED(hr)) return hr;

	ComPtr<IXmlNode> srcAttribute;
	hr = attributes->GetNamedItem(StringReferenceWrapper(L"src").Get(), &srcAttribute);
	if (!SUCCEEDED(hr)) return hr;

	return SetNodeValueString(StringReferenceWrapper(imageSrc).Get(), srcAttribute.Get(), toastXml);
}

typedef ABI::Windows::Foundation::ITypedEventHandler<ToastNotification*, ::IInspectable *> DesktopToastActivatedEventHandler;
typedef ABI::Windows::Foundation::ITypedEventHandler<ToastNotification*, ToastDismissedEventArgs*> DesktopToastDismissedEventHandler;
typedef ABI::Windows::Foundation::ITypedEventHandler<ToastNotification*, ToastFailedEventArgs*> DesktopToastFailedEventHandler;

class ToastEventHandler final : public Implements<
	DesktopToastActivatedEventHandler,
	DesktopToastDismissedEventHandler,
	DesktopToastFailedEventHandler> {
public:
	// We keep a weak pointer to a member field of native notifications manager.
	ToastEventHandler(
		const std::shared_ptr<Manager*> &guarded,
		const PeerId &peer,
		MsgId msg)
	: _peerId(peer)
	, _msgId(msg)
	, _weak(guarded) {
	}

	void performOnMainQueue(FnMut<void(Manager *manager)> task) {
		const auto weak = _weak;
		crl::on_main(weak, [=, task = std::move(task)]() mutable {
			task(*weak.lock());
		});
	}

	// DesktopToastActivatedEventHandler
	IFACEMETHODIMP Invoke(_In_ IToastNotification *sender, _In_ IInspectable* args) {
		performOnMainQueue([peerId = _peerId, msgId = _msgId](Manager *manager) {
			manager->notificationActivated(peerId, msgId);
		});
		return S_OK;
	}

	// DesktopToastDismissedEventHandler
	IFACEMETHODIMP Invoke(_In_ IToastNotification *sender, _In_ IToastDismissedEventArgs *e) {
		ToastDismissalReason tdr;
		if (SUCCEEDED(e->get_Reason(&tdr))) {
			switch (tdr) {
			case ToastDismissalReason_ApplicationHidden:
			break;
			case ToastDismissalReason_UserCanceled:
			case ToastDismissalReason_TimedOut:
			default:
				performOnMainQueue([peerId = _peerId, msgId = _msgId](Manager *manager) {
					manager->clearNotification(peerId, msgId);
				});
			break;
			}
		}
		return S_OK;
	}

	// DesktopToastFailedEventHandler
	IFACEMETHODIMP Invoke(_In_ IToastNotification *sender, _In_ IToastFailedEventArgs *e) {
		performOnMainQueue([peerId = _peerId, msgId = _msgId](Manager *manager) {
			manager->clearNotification(peerId, msgId);
		});
		return S_OK;
	}

	// IUnknown
	IFACEMETHODIMP_(ULONG) AddRef() {
		return InterlockedIncrement(&_refCount);
	}

	IFACEMETHODIMP_(ULONG) Release() {
		auto refCount = InterlockedDecrement(&_refCount);
		if (refCount == 0) {
			delete this;
		}
		return refCount;
	}

	IFACEMETHODIMP QueryInterface(_In_ REFIID riid, _COM_Outptr_ void **ppv) {
		if (IsEqualIID(riid, IID_IUnknown))
			*ppv = static_cast<IUnknown*>(static_cast<DesktopToastActivatedEventHandler*>(this));
		else if (IsEqualIID(riid, __uuidof(DesktopToastActivatedEventHandler)))
			*ppv = static_cast<DesktopToastActivatedEventHandler*>(this);
		else if (IsEqualIID(riid, __uuidof(DesktopToastDismissedEventHandler)))
			*ppv = static_cast<DesktopToastDismissedEventHandler*>(this);
		else if (IsEqualIID(riid, __uuidof(DesktopToastFailedEventHandler)))
			*ppv = static_cast<DesktopToastFailedEventHandler*>(this);
		else *ppv = nullptr;

		if (*ppv) {
			reinterpret_cast<IUnknown*>(*ppv)->AddRef();
			return S_OK;
		}

		return E_NOINTERFACE;
	}

private:
	ULONG _refCount = 0;
	PeerId _peerId = 0;
	MsgId _msgId = 0;
	std::weak_ptr<Manager*> _weak;

};

auto Checked = false;
auto InitSucceeded = false;

void Check() {
	InitSucceeded = init();
}

} // namespace

bool Supported() {
	if (!Checked) {
		Checked = true;
		Check();
	}
	return InitSucceeded;
}

std::unique_ptr<Window::Notifications::Manager> Create(Window::Notifications::System *system) {
	if (Global::NativeNotifications() && Supported()) {
		auto result = std::make_unique<Manager>(system);
		if (result->init()) {
			return std::move(result);
		}
	}
	return nullptr;
}

void FlashBounce() {
	auto window = App::wnd();
	if (!window || GetForegroundWindow() == window->psHwnd()) {
		return;
	}

	FLASHWINFO info;
	info.cbSize = sizeof(info);
	info.hwnd = window->psHwnd();
	info.dwFlags = FLASHW_ALL;
	info.dwTimeout = 0;
	info.uCount = 1;
	FlashWindowEx(&info);
}

class Manager::Private {
public:
	using Type = Window::Notifications::CachedUserpics::Type;

	explicit Private(Manager *instance, Type type);
	bool init();

	bool showNotification(
		not_null<PeerData*> peer,
		MsgId msgId,
		const QString &title,
		const QString &subtitle,
		const QString &msg,
		bool hideNameAndPhoto,
		bool hideReplyButton);
	void clearAll();
	void clearFromHistory(not_null<History*> history);
	void beforeNotificationActivated(PeerId peerId, MsgId msgId);
	void afterNotificationActivated(PeerId peerId, MsgId msgId);
	void clearNotification(PeerId peerId, MsgId msgId);

	~Private();

private:
	Window::Notifications::CachedUserpics _cachedUserpics;

	std::shared_ptr<Manager*> _guarded;

	ComPtr<IToastNotificationManagerStatics> _notificationManager;
	ComPtr<IToastNotifier> _notifier;
	ComPtr<IToastNotificationFactory> _notificationFactory;

	struct NotificationPtr {
		NotificationPtr() {
		}
		NotificationPtr(const ComPtr<IToastNotification> &ptr) : p(ptr) {
		}

		ComPtr<IToastNotification> p;
	};
	QMap<PeerId, QMap<MsgId, NotificationPtr>> _notifications;

};

Manager::Private::Private(Manager *instance, Type type)
: _cachedUserpics(type)
, _guarded(std::make_shared<Manager*>(instance)) {
}

bool Manager::Private::init() {
	if (!SUCCEEDED(wrap_GetActivationFactory(StringReferenceWrapper(RuntimeClass_Windows_UI_Notifications_ToastNotificationManager).Get(), &_notificationManager))) {
		return false;
	}

	auto appUserModelId = AppUserModelId::getId();
	if (!SUCCEEDED(_notificationManager->CreateToastNotifierWithId(StringReferenceWrapper(appUserModelId, wcslen(appUserModelId)).Get(), &_notifier))) {
		return false;
	}

	if (!SUCCEEDED(wrap_GetActivationFactory(StringReferenceWrapper(RuntimeClass_Windows_UI_Notifications_ToastNotification).Get(), &_notificationFactory))) {
		return false;
	}
	return true;
}

Manager::Private::~Private() {
	clearAll();

	_notifications.clear();
	if (_notificationManager) _notificationManager.Reset();
	if (_notifier) _notifier.Reset();
	if (_notificationFactory) _notificationFactory.Reset();
}

void Manager::Private::clearAll() {
	if (!_notifier) return;

	auto temp = base::take(_notifications);
	for_const (auto &notifications, temp) {
		for_const (auto &notification, notifications) {
			_notifier->Hide(notification.p.Get());
		}
	}
}

void Manager::Private::clearFromHistory(not_null<History*> history) {
	if (!_notifier) return;

	auto i = _notifications.find(history->peer->id);
	if (i != _notifications.cend()) {
		auto temp = base::take(i.value());
		_notifications.erase(i);

		for_const (auto &notification, temp) {
			_notifier->Hide(notification.p.Get());
		}
	}
}

void Manager::Private::beforeNotificationActivated(PeerId peerId, MsgId msgId) {
	clearNotification(peerId, msgId);
}

void Manager::Private::afterNotificationActivated(PeerId peerId, MsgId msgId) {
	if (auto window = App::wnd()) {
		SetForegroundWindow(window->psHwnd());
	}
}

void Manager::Private::clearNotification(PeerId peerId, MsgId msgId) {
	auto i = _notifications.find(peerId);
	if (i != _notifications.cend()) {
		i.value().remove(msgId);
		if (i.value().isEmpty()) {
			_notifications.erase(i);
		}
	}
}

bool Manager::Private::showNotification(
		not_null<PeerData*> peer,
		MsgId msgId,
		const QString &title,
		const QString &subtitle,
		const QString &msg,
		bool hideNameAndPhoto,
		bool hideReplyButton) {
	if (!_notificationManager || !_notifier || !_notificationFactory) return false;

	ComPtr<IXmlDocument> toastXml;
	bool withSubtitle = !subtitle.isEmpty();

	HRESULT hr = _notificationManager->GetTemplateContent(
		(withSubtitle
			? ToastTemplateType_ToastImageAndText04
			: ToastTemplateType_ToastImageAndText02),
		&toastXml);
	if (!SUCCEEDED(hr)) return false;

	hr = SetAudioSilent(toastXml.Get());
	if (!SUCCEEDED(hr)) return false;

	const auto key = hideNameAndPhoto
		? InMemoryKey()
		: peer->userpicUniqueKey();
	const auto userpicPath = _cachedUserpics.get(key, peer);
	const auto userpicPathWide = QDir::toNativeSeparators(userpicPath).toStdWString();

	hr = SetImageSrc(userpicPathWide.c_str(), toastXml.Get());
	if (!SUCCEEDED(hr)) return false;

	ComPtr<IXmlNodeList> nodeList;
	hr = toastXml->GetElementsByTagName(StringReferenceWrapper(L"text").Get(), &nodeList);
	if (!SUCCEEDED(hr)) return false;

	UINT32 nodeListLength;
	hr = nodeList->get_Length(&nodeListLength);
	if (!SUCCEEDED(hr)) return false;

	if (nodeListLength < (withSubtitle ? 3U : 2U)) return false;

	{
		ComPtr<IXmlNode> textNode;
		hr = nodeList->Item(0, &textNode);
		if (!SUCCEEDED(hr)) return false;

		std::wstring wtitle = title.toStdWString();
		hr = SetNodeValueString(StringReferenceWrapper(wtitle.data(), wtitle.size()).Get(), textNode.Get(), toastXml.Get());
		if (!SUCCEEDED(hr)) return false;
	}
	if (withSubtitle) {
		ComPtr<IXmlNode> textNode;
		hr = nodeList->Item(1, &textNode);
		if (!SUCCEEDED(hr)) return false;

		std::wstring wsubtitle = subtitle.toStdWString();
		hr = SetNodeValueString(StringReferenceWrapper(wsubtitle.data(), wsubtitle.size()).Get(), textNode.Get(), toastXml.Get());
		if (!SUCCEEDED(hr)) return false;
	}
	{
		ComPtr<IXmlNode> textNode;
		hr = nodeList->Item(withSubtitle ? 2 : 1, &textNode);
		if (!SUCCEEDED(hr)) return false;

		std::wstring wmsg = msg.toStdWString();
		hr = SetNodeValueString(StringReferenceWrapper(wmsg.data(), wmsg.size()).Get(), textNode.Get(), toastXml.Get());
		if (!SUCCEEDED(hr)) return false;
	}

	ComPtr<IToastNotification> toast;
	hr = _notificationFactory->CreateToastNotification(toastXml.Get(), &toast);
	if (!SUCCEEDED(hr)) return false;

	EventRegistrationToken activatedToken, dismissedToken, failedToken;
	ComPtr<ToastEventHandler> eventHandler(new ToastEventHandler(_guarded, peer->id, msgId));

	hr = toast->add_Activated(eventHandler.Get(), &activatedToken);
	if (!SUCCEEDED(hr)) return false;

	hr = toast->add_Dismissed(eventHandler.Get(), &dismissedToken);
	if (!SUCCEEDED(hr)) return false;

	hr = toast->add_Failed(eventHandler.Get(), &failedToken);
	if (!SUCCEEDED(hr)) return false;

	auto i = _notifications.find(peer->id);
	if (i != _notifications.cend()) {
		auto j = i->find(msgId);
		if (j != i->cend()) {
			ComPtr<IToastNotification> notify = j->p;
			i->erase(j);
			_notifier->Hide(notify.Get());
			i = _notifications.find(peer->id);
		}
	}
	if (i == _notifications.cend()) {
		i = _notifications.insert(peer->id, QMap<MsgId, NotificationPtr>());
	}
	hr = _notifier->Show(toast.Get());
	if (!SUCCEEDED(hr)) {
		i = _notifications.find(peer->id);
		if (i != _notifications.cend() && i->isEmpty()) _notifications.erase(i);
		return false;
	}
	_notifications[peer->id].insert(msgId, toast);

	return true;
}

Manager::Manager(Window::Notifications::System *system) : NativeManager(system)
, _private(std::make_unique<Private>(this, Private::Type::Rounded)) {
}

bool Manager::init() {
	return _private->init();
}

void Manager::clearNotification(PeerId peerId, MsgId msgId) {
	_private->clearNotification(peerId, msgId);
}

Manager::~Manager() = default;

void Manager::doShowNativeNotification(
		not_null<PeerData*> peer,
		MsgId msgId,
		const QString &title,
		const QString &subtitle,
		const QString &msg,
		bool hideNameAndPhoto,
		bool hideReplyButton) {
	_private->showNotification(
		peer,
		msgId,
		title,
		subtitle,
		msg,
		hideNameAndPhoto,
		hideReplyButton);
}

void Manager::doClearAllFast() {
	_private->clearAll();
}

void Manager::doClearFromHistory(not_null<History*> history) {
	_private->clearFromHistory(history);
}

void Manager::onBeforeNotificationActivated(PeerId peerId, MsgId msgId) {
	_private->beforeNotificationActivated(peerId, msgId);
}

void Manager::onAfterNotificationActivated(PeerId peerId, MsgId msgId) {
	_private->afterNotificationActivated(peerId, msgId);
}

namespace {

bool QuietHoursEnabled = false;
DWORD QuietHoursValue = 0;

bool useQuietHoursRegistryEntry() {
	// Taken from QSysInfo.
	OSVERSIONINFO result = { sizeof(OSVERSIONINFO), 0, 0, 0, 0,{ '\0' } };
	if (const auto library = GetModuleHandle(L"ntdll.dll")) {
		using RtlGetVersionFunction = NTSTATUS(NTAPI*)(LPOSVERSIONINFO);
		const auto RtlGetVersion = reinterpret_cast<RtlGetVersionFunction>(
			GetProcAddress(library, "RtlGetVersion"));
		if (RtlGetVersion) {
			RtlGetVersion(&result);
		}
	}
	// At build 17134 (Redstone 4) the "Quiet hours" was replaced
	// by "Focus assist" and it looks like it doesn't use registry.
	return (result.dwMajorVersion == 10
		&& result.dwMinorVersion == 0
		&& result.dwBuildNumber < 17134);
}

// Thanks https://stackoverflow.com/questions/35600128/get-windows-quiet-hours-from-win32-or-c-sharp-api
void queryQuietHours() {
	if (!useQuietHoursRegistryEntry()) {
		// There are quiet hours in Windows starting from Windows 8.1
		// But there were several reports about the notifications being shut
		// down according to the registry while no quiet hours were enabled.
		// So we try this method only starting with Windows 10.
		return;
	}

	LPCWSTR lpKeyName = L"Software\\Microsoft\\Windows\\CurrentVersion\\Notifications\\Settings";
	LPCWSTR lpValueName = L"NOC_GLOBAL_SETTING_TOASTS_ENABLED";
	HKEY key;
	auto result = RegOpenKeyEx(HKEY_CURRENT_USER, lpKeyName, 0, KEY_READ, &key);
	if (result != ERROR_SUCCESS) {
		return;
	}

	DWORD value = 0, type = 0, size = sizeof(value);
	result = RegQueryValueEx(key, lpValueName, 0, &type, (LPBYTE)&value, &size);
	RegCloseKey(key);

	auto quietHoursEnabled = (result == ERROR_SUCCESS) && (value == 0);
	if (QuietHoursEnabled != quietHoursEnabled) {
		QuietHoursEnabled = quietHoursEnabled;
		QuietHoursValue = value;
		LOG(("Quiet hours changed, entry value: %1").arg(value));
	} else if (QuietHoursValue != value) {
		QuietHoursValue = value;
		LOG(("Quiet hours value changed, was value: %1, entry value: %2").arg(QuietHoursValue).arg(value));
	}
}

QUERY_USER_NOTIFICATION_STATE UserNotificationState = QUNS_ACCEPTS_NOTIFICATIONS;

void queryUserNotificationState() {
	if (Dlls::SHQueryUserNotificationState != nullptr) {
		QUERY_USER_NOTIFICATION_STATE state;
		if (SUCCEEDED(Dlls::SHQueryUserNotificationState(&state))) {
			UserNotificationState = state;
		}
	}
}

static constexpr auto kQuerySettingsEachMs = 1000;
crl::time LastSettingsQueryMs = 0;

void querySystemNotificationSettings() {
	auto ms = crl::now();
	if (LastSettingsQueryMs > 0 && ms <= LastSettingsQueryMs + kQuerySettingsEachMs) {
		return;
	}
	LastSettingsQueryMs = ms;
	queryQuietHours();
	queryUserNotificationState();
}

} // namespace

bool SkipAudio() {
	querySystemNotificationSettings();

	if (UserNotificationState == QUNS_NOT_PRESENT
		|| UserNotificationState == QUNS_PRESENTATION_MODE
		|| QuietHoursEnabled) {
		return true;
	}
	if (const auto filter = EventFilter::GetInstance()) {
		if (filter->sessionLoggedOff()) {
			return true;
		}
	}
	return false;
}

bool SkipToast() {
	querySystemNotificationSettings();

	if (UserNotificationState == QUNS_PRESENTATION_MODE
		|| UserNotificationState == QUNS_RUNNING_D3D_FULL_SCREEN
		//|| UserNotificationState == QUNS_BUSY
		|| QuietHoursEnabled) {
		return true;
	}
	return false;
}

} // namespace Notifications
} // namespace Platform
