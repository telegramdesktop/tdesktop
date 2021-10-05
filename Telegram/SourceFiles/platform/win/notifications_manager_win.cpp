/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/win/notifications_manager_win.h"

#include "window/notifications_utilities.h"
#include "window/window_session_controller.h"
#include "base/platform/win/base_windows_co_task_mem.h"
#include "base/platform/win/base_windows_winrt.h"
#include "base/platform/base_platform_info.h"
#include "platform/win/windows_app_user_model_id.h"
#include "platform/win/windows_event_filter.h"
#include "platform/win/windows_dlls.h"
#include "history/history.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "main/main_session.h"
#include "mainwindow.h"
#include "windows_quiethours_h.h"

#include <QtCore/QOperatingSystemVersion>

#include <Shobjidl.h>
#include <shellapi.h>
#include <strsafe.h>

#ifndef __MINGW32__
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.UI.Notifications.h>
#include <winrt/Windows.UI.Notifications.Management.h>

HICON qt_pixmapToWinHICON(const QPixmap &);

using namespace winrt::Windows::UI::Notifications;
using namespace winrt::Windows::Data::Xml::Dom;
using namespace winrt::Windows::Foundation;
using winrt::com_ptr;
#endif // !__MINGW32__

namespace Platform {
namespace Notifications {

#ifndef __MINGW32__
namespace {

bool init() {
	if (!IsWindows8OrGreater()) {
		return false;
	}
	if ((Dlls::SetCurrentProcessExplicitAppUserModelID == nullptr)
		|| (Dlls::PropVariantToString == nullptr)
		|| !base::WinRT::Supported()) {
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

// Throws.
void SetNodeValueString(
		const XmlDocument &xml,
		const IXmlNode &node,
		const std::wstring &text) {
	node.AppendChild(xml.CreateTextNode(text).as<IXmlNode>());
}

// Throws.
void SetAudioSilent(const XmlDocument &toastXml) {
	const auto nodeList = toastXml.GetElementsByTagName(L"audio");
	if (const auto audioNode = nodeList.Item(0)) {
		audioNode.as<IXmlElement>().SetAttribute(L"silent", L"true");
	} else {
		auto audioElement = toastXml.CreateElement(L"audio");
		audioElement.SetAttribute(L"silent", L"true");
		auto nodeList = toastXml.GetElementsByTagName(L"toast");
		nodeList.Item(0).AppendChild(audioElement.as<IXmlNode>());
	}
}

// Throws.
void SetImageSrc(const XmlDocument &toastXml, const std::wstring &path) {
	const auto nodeList = toastXml.GetElementsByTagName(L"image");
	const auto attributes = nodeList.Item(0).Attributes();
	return SetNodeValueString(
		toastXml,
		attributes.GetNamedItem(L"src"),
		L"file:///" + path);
}

auto Checked = false;
auto InitSucceeded = false;

void Check() {
	InitSucceeded = init();
}

bool QuietHoursEnabled = false;
DWORD QuietHoursValue = 0;

[[nodiscard]] bool UseQuietHoursRegistryEntry() {
	static const bool result = [] {
		const auto version = QOperatingSystemVersion::current();

		// At build 17134 (Redstone 4) the "Quiet hours" was replaced
		// by "Focus assist" and it looks like it doesn't use registry.
		return (version.majorVersion() == 10)
			&& (version.minorVersion() == 0)
			&& (version.microVersion() < 17134);
	}();
	return result;
}

// Thanks https://stackoverflow.com/questions/35600128/get-windows-quiet-hours-from-win32-or-c-sharp-api
void QueryQuietHours() {
	if (!UseQuietHoursRegistryEntry()) {
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

bool FocusAssistBlocks = false;

// Thanks https://www.withinrafael.com/2019/09/19/determine-if-your-app-is-in-a-focus-assist-profiles-priority-list/
void QueryFocusAssist() {
	const auto quietHoursSettings = base::WinRT::TryCreateInstance<
		IQuietHoursSettings
	>(CLSID_QuietHoursSettings, CLSCTX_LOCAL_SERVER);
	if (!quietHoursSettings) {
		return;
	}

	auto profileId = base::CoTaskMemString();
	auto hr = quietHoursSettings->get_UserSelectedProfile(profileId.put());
	if (FAILED(hr) || !profileId) {
		return;
	}
	const auto profileName = QString::fromWCharArray(profileId.data());
	if (profileName.endsWith(".alarmsonly", Qt::CaseInsensitive)) {
		if (!FocusAssistBlocks) {
			LOG(("Focus Assist: Alarms Only."));
			FocusAssistBlocks = true;
		}
		return;
	} else if (!profileName.endsWith(".priorityonly", Qt::CaseInsensitive)) {
		if (!profileName.endsWith(".unrestricted", Qt::CaseInsensitive)) {
			LOG(("Focus Assist Warning: Unknown profile '%1'"
				).arg(profileName));
		}
		if (FocusAssistBlocks) {
			LOG(("Focus Assist: Unrestricted."));
			FocusAssistBlocks = false;
		}
		return;
	}
	const auto appUserModelId = std::wstring(AppUserModelId::getId());
	auto blocked = true;
	const auto guard = gsl::finally([&] {
		if (FocusAssistBlocks != blocked) {
			LOG(("Focus Assist: %1, AppUserModelId: %2, Blocks: %3"
				).arg(profileName
				).arg(QString::fromStdWString(appUserModelId)
				).arg(Logs::b(blocked)));
			FocusAssistBlocks = blocked;
		}
	});

	com_ptr<IQuietHoursProfile> profile;
	hr = quietHoursSettings->GetProfile(profileId.data(), profile.put());
	if (FAILED(hr) || !profile) {
		return;
	}

	auto apps = base::CoTaskMemStringArray();
	hr = profile->GetAllowedApps(apps.put_size(), apps.put());
	if (FAILED(hr) || !apps) {
		return;
	}
	for (const auto &app : apps) {
		if (app && app.data() == appUserModelId) {
			blocked = false;
			break;
		}
	}
}

QUERY_USER_NOTIFICATION_STATE UserNotificationState
	= QUNS_ACCEPTS_NOTIFICATIONS;

void QueryUserNotificationState() {
	if (Dlls::SHQueryUserNotificationState != nullptr) {
		QUERY_USER_NOTIFICATION_STATE state;
		if (SUCCEEDED(Dlls::SHQueryUserNotificationState(&state))) {
			UserNotificationState = state;
		}
	}
}

static constexpr auto kQuerySettingsEachMs = 1000;
crl::time LastSettingsQueryMs = 0;

void QuerySystemNotificationSettings() {
	auto ms = crl::now();
	if (LastSettingsQueryMs > 0 && ms <= LastSettingsQueryMs + kQuerySettingsEachMs) {
		return;
	}
	LastSettingsQueryMs = ms;
	QueryQuietHours();
	QueryFocusAssist();
	QueryUserNotificationState();
}

} // namespace
#endif // !__MINGW32__

bool SkipAudioForCustom() {
	QuerySystemNotificationSettings();

	return (UserNotificationState == QUNS_NOT_PRESENT)
		|| (UserNotificationState == QUNS_PRESENTATION_MODE)
		|| Core::App().screenIsLocked();
}

bool SkipToastForCustom() {
	QuerySystemNotificationSettings();

	return (UserNotificationState == QUNS_PRESENTATION_MODE)
		|| (UserNotificationState == QUNS_RUNNING_D3D_FULL_SCREEN);
}

bool SkipFlashBounceForCustom() {
	return SkipToastForCustom();
}

bool Supported() {
#ifndef __MINGW32__
	if (!Checked) {
		Checked = true;
		Check();
	}
	return InitSucceeded;
#endif // !__MINGW32__

	return false;
}

bool Enforced() {
	return false;
}

bool ByDefault() {
	return false;
}

void Create(Window::Notifications::System *system) {
#ifndef __MINGW32__
	if (Core::App().settings().nativeNotifications() && Supported()) {
		auto result = std::make_unique<Manager>(system);
		if (result->init()) {
			system->setManager(std::move(result));
			return;
		}
	}
#endif // !__MINGW32__
	system->setManager(nullptr);
}

#ifndef __MINGW32__
class Manager::Private {
public:
	using Type = Window::Notifications::CachedUserpics::Type;

	explicit Private(Manager *instance, Type type);
	bool init();

	bool showNotification(
		not_null<PeerData*> peer,
		std::shared_ptr<Data::CloudImageView> &userpicView,
		MsgId msgId,
		const QString &title,
		const QString &subtitle,
		const QString &msg,
		bool hideNameAndPhoto,
		bool hideReplyButton);
	void clearAll();
	void clearFromHistory(not_null<History*> history);
	void clearFromSession(not_null<Main::Session*> session);
	void beforeNotificationActivated(NotificationId id);
	void afterNotificationActivated(
		NotificationId id,
		not_null<Window::SessionController*> window);
	void clearNotification(NotificationId id);

	~Private();

private:
	bool showNotificationInTryCatch(
		not_null<PeerData*> peer,
		std::shared_ptr<Data::CloudImageView> &userpicView,
		MsgId msgId,
		const QString &title,
		const QString &subtitle,
		const QString &msg,
		bool hideNameAndPhoto,
		bool hideReplyButton);

	Window::Notifications::CachedUserpics _cachedUserpics;

	std::shared_ptr<Manager*> _guarded;
	ToastNotifier _notifier = nullptr;

	base::flat_map<
		FullPeer,
		base::flat_map<MsgId, ToastNotification>> _notifications;

};

Manager::Private::Private(Manager *instance, Type type)
: _cachedUserpics(type)
, _guarded(std::make_shared<Manager*>(instance)) {
}

bool Manager::Private::init() {
	return base::WinRT::Try([&] {
		_notifier = ToastNotificationManager::CreateToastNotifier(
			AppUserModelId::getId());
	});
}

Manager::Private::~Private() {
	clearAll();

	_notifications.clear();
	_notifier = nullptr;
}

void Manager::Private::clearAll() {
	if (!_notifier) {
		return;
	}

	auto temp = base::take(_notifications);
	for (const auto &[key, notifications] : base::take(_notifications)) {
		for (const auto &[msgId, notification] : notifications) {
			_notifier.Hide(notification);
		}
	}
}

void Manager::Private::clearFromHistory(not_null<History*> history) {
	if (!_notifier) {
		return;
	}

	auto i = _notifications.find(FullPeer{
		.sessionId = history->session().uniqueId(),
		.peerId = history->peer->id
	});
	if (i != _notifications.cend()) {
		auto temp = base::take(i->second);
		_notifications.erase(i);

		for (const auto &[msgId, notification] : temp) {
			_notifier.Hide(notification);
		}
	}
}

void Manager::Private::clearFromSession(not_null<Main::Session*> session) {
	if (!_notifier) {
		return;
	}

	const auto sessionId = session->uniqueId();
	for (auto i = _notifications.begin(); i != _notifications.end();) {
		if (i->first.sessionId != sessionId) {
			++i;
			continue;
		}
		const auto temp = base::take(i->second);
		_notifications.erase(i);

		for (const auto &[msgId, notification] : temp) {
			_notifier.Hide(notification);
		}
	}
}

void Manager::Private::beforeNotificationActivated(NotificationId id) {
	clearNotification(id);
}

void Manager::Private::afterNotificationActivated(
		NotificationId id,
		not_null<Window::SessionController*> window) {
	SetForegroundWindow(window->widget()->psHwnd());
}

void Manager::Private::clearNotification(NotificationId id) {
	auto i = _notifications.find(id.full);
	if (i != _notifications.cend()) {
		i->second.remove(id.msgId);
		if (i->second.empty()) {
			_notifications.erase(i);
		}
	}
}

bool Manager::Private::showNotification(
		not_null<PeerData*> peer,
		std::shared_ptr<Data::CloudImageView> &userpicView,
		MsgId msgId,
		const QString &title,
		const QString &subtitle,
		const QString &msg,
		bool hideNameAndPhoto,
		bool hideReplyButton) {
	if (!_notifier) {
		return false;
	}

	return base::WinRT::Try([&] {
		return showNotificationInTryCatch(
			peer,
			userpicView,
			msgId,
			title,
			subtitle,
			msg,
			hideNameAndPhoto,
			hideReplyButton);
	}).value_or(false);
}

bool Manager::Private::showNotificationInTryCatch(
		not_null<PeerData*> peer,
		std::shared_ptr<Data::CloudImageView> &userpicView,
		MsgId msgId,
		const QString &title,
		const QString &subtitle,
		const QString &msg,
		bool hideNameAndPhoto,
		bool hideReplyButton) {
	const auto withSubtitle = !subtitle.isEmpty();
	const auto toastXml = ToastNotificationManager::GetTemplateContent(
		(withSubtitle
			? ToastTemplateType::ToastImageAndText04
			: ToastTemplateType::ToastImageAndText02));
	SetAudioSilent(toastXml);

	const auto userpicKey = hideNameAndPhoto
		? InMemoryKey()
		: peer->userpicUniqueKey(userpicView);
	const auto userpicPath = _cachedUserpics.get(userpicKey, peer, userpicView);
	const auto userpicPathWide = QDir::toNativeSeparators(userpicPath).toStdWString();

	SetImageSrc(toastXml, userpicPathWide);

	const auto nodeList = toastXml.GetElementsByTagName(L"text");
	if (nodeList.Length() < (withSubtitle ? 3U : 2U)) {
		return false;
	}

	SetNodeValueString(toastXml, nodeList.Item(0), title.toStdWString());
	if (withSubtitle) {
		SetNodeValueString(
			toastXml,
			nodeList.Item(1),
			subtitle.toStdWString());
	}
	SetNodeValueString(
		toastXml,
		nodeList.Item(withSubtitle ? 2 : 1),
		msg.toStdWString());

	const auto weak = std::weak_ptr(_guarded);
	const auto performOnMainQueue = [=](FnMut<void(Manager *manager)> task) {
		crl::on_main(weak, [=, task = std::move(task)]() mutable {
			task(*weak.lock());
		});
	};

	const auto key = FullPeer{
		.sessionId = peer->session().uniqueId(),
		.peerId = peer->id,
	};
	const auto notificationId = NotificationId{
		.full = key,
		.msgId = msgId
	};
	auto toast = ToastNotification(toastXml);
	const auto token1 = toast.Activated([=](
			const ToastNotification &sender,
			const winrt::Windows::Foundation::IInspectable &args) {
		performOnMainQueue([notificationId](Manager *manager) {
			manager->notificationActivated(notificationId);
		});
	});
	const auto token2 = toast.Dismissed([=](
			const ToastNotification &sender,
			const ToastDismissedEventArgs &args) {
		base::WinRT::Try([&] {
			switch (args.Reason()) {
			case ToastDismissalReason::ApplicationHidden:
			case ToastDismissalReason::TimedOut: // Went to Action Center.
				break;
			case ToastDismissalReason::UserCanceled:
			default:
				performOnMainQueue([notificationId](Manager *manager) {
					manager->clearNotification(notificationId);
				});
				break;
			}
		});
	});
	const auto token3 = toast.Failed([=](
			const auto &sender,
			const ToastFailedEventArgs &args) {
		performOnMainQueue([notificationId](Manager *manager) {
			manager->clearNotification(notificationId);
		});
	});

	auto i = _notifications.find(key);
	if (i != _notifications.cend()) {
		auto j = i->second.find(msgId);
		if (j != i->second.end()) {
			const auto existing = j->second;
			i->second.erase(j);
			_notifier.Hide(existing);
			i = _notifications.find(key);
		}
	}
	if (i == _notifications.cend()) {
		i = _notifications.emplace(
			key,
			base::flat_map<MsgId, ToastNotification>()).first;
	}
	if (!base::WinRT::Try([&] { _notifier.Show(toast); })) {
		i = _notifications.find(key);
		if (i != _notifications.cend() && i->second.empty()) {
			_notifications.erase(i);
		}
		return false;
	}
	i->second.emplace(msgId, toast);
	return true;
}

Manager::Manager(Window::Notifications::System *system) : NativeManager(system)
, _private(std::make_unique<Private>(this, Private::Type::Rounded)) {
}

bool Manager::init() {
	return _private->init();
}

void Manager::clearNotification(NotificationId id) {
	_private->clearNotification(id);
}

Manager::~Manager() = default;

void Manager::doShowNativeNotification(
		not_null<PeerData*> peer,
		std::shared_ptr<Data::CloudImageView> &userpicView,
		MsgId msgId,
		const QString &title,
		const QString &subtitle,
		const QString &msg,
		bool hideNameAndPhoto,
		bool hideReplyButton) {
	_private->showNotification(
		peer,
		userpicView,
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

void Manager::doClearFromSession(not_null<Main::Session*> session) {
	_private->clearFromSession(session);
}

void Manager::onBeforeNotificationActivated(NotificationId id) {
	_private->beforeNotificationActivated(id);
}

void Manager::onAfterNotificationActivated(
		NotificationId id,
		not_null<Window::SessionController*> window) {
	_private->afterNotificationActivated(id, window);
}

bool Manager::doSkipAudio() const {
	return SkipAudioForCustom()
		|| QuietHoursEnabled
		|| FocusAssistBlocks;
}

bool Manager::doSkipToast() const {
	return false;
}

bool Manager::doSkipFlashBounce() const {
	return SkipFlashBounceForCustom()
		|| QuietHoursEnabled
		|| FocusAssistBlocks;
}
#endif // !__MINGW32__

} // namespace Notifications
} // namespace Platform
