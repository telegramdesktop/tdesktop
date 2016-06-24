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
#include "stdafx.h"
#include "platform/win/windows_toasts.h"

#include "platform/win/windows_app_user_model_id.h"
#include "platform/win/windows_dlls.h"
#include "mainwindow.h"

#include <Shobjidl.h>
#include <shellapi.h>

#include <roapi.h>
#include <wrl\client.h>
#include <wrl\implements.h>
#include <windows.ui.notifications.h>

#include <strsafe.h>
#include <intsafe.h>

HICON qt_pixmapToWinHICON(const QPixmap &);

using namespace Microsoft::WRL;
using namespace ABI::Windows::UI::Notifications;
using namespace ABI::Windows::Data::Xml::Dom;
using namespace Windows::Foundation;

namespace Platform {
namespace Toasts {
namespace {

bool _supported = false;

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
using Notifications = QMap<PeerId, QMap<MsgId, NotificationPtr>>;
Notifications _notifications;
struct Image {
	uint64 until;
	QString path;
};
using Images = QMap<StorageKey, Image>;
Images _images;
bool _imageSavedFlag = false;

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
	if (!SUCCEEDED(wrap_GetActivationFactory(StringReferenceWrapper(RuntimeClass_Windows_UI_Notifications_ToastNotificationManager).Get(), &_notificationManager))) {
		return false;
	}
	if (!SUCCEEDED(_notificationManager->CreateToastNotifierWithId(StringReferenceWrapper(appUserModelId, wcslen(appUserModelId)).Get(), &_notifier))) {
		return false;
	}
	if (!SUCCEEDED(wrap_GetActivationFactory(StringReferenceWrapper(RuntimeClass_Windows_UI_Notifications_ToastNotification).Get(), &_notificationFactory))) {
		return false;
	}
	QDir().mkpath(cWorkingDir() + qsl("tdata/temp"));
	return true;
}

} // namespace

void start() {
	_supported = init();
}

bool supported() {
	return _supported;
}

uint64 clearImages(uint64 ms) {
	uint64 result = 0;
	for (auto i = _images.begin(); i != _images.end();) {
		if (!i->until) {
			++i;
			continue;
		}
		if (i->until <= ms) {
			QFile(i->path).remove();
			i = _images.erase(i);
		} else {
			if (!result) {
				result = i->until;
			} else {
				accumulate_min(result, i->until);
			}
			++i;
		}
	}
	return result;
}

void clearNotifies(PeerId peerId) {
	if (!_notifier) return;

	if (peerId) {
		auto i = _notifications.find(peerId);
		if (i != _notifications.cend()) {
			auto temp = createAndSwap(i.value());
			_notifications.erase(i);

			for (auto j = temp.cbegin(), e = temp.cend(); j != e; ++j) {
				_notifier->Hide(j->p.Get());
			}
		}
	} else {
		auto temp = createAndSwap(_notifications);
		for_const (auto &notifications, temp) {
			for_const (auto &notification, notifications) {
				_notifier->Hide(notification.p.Get());
			}
		}
	}
}

void finish() {
	_notifications.clear();
	if (_notificationManager) _notificationManager.Reset();
	if (_notifier) _notifier.Reset();
	if (_notificationFactory) _notificationFactory.Reset();

	if (_imageSavedFlag) {
		psDeleteDir(cWorkingDir() + qsl("tdata/temp"));
	}
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

class ToastEventHandler : public Implements<DesktopToastActivatedEventHandler, DesktopToastDismissedEventHandler, DesktopToastFailedEventHandler> {
public:

	ToastEventHandler::ToastEventHandler(const PeerId &peer, MsgId msg) : _ref(1), _peerId(peer), _msgId(msg) {
	}
	~ToastEventHandler() {
	}

	// DesktopToastActivatedEventHandler
	IFACEMETHODIMP Invoke(_In_ IToastNotification *sender, _In_ IInspectable* args) {
		auto i = _notifications.find(_peerId);
		if (i != _notifications.cend()) {
			i.value().remove(_msgId);
			if (i.value().isEmpty()) {
				_notifications.erase(i);
			}
		}
		if (App::wnd()) {
			History *history = App::history(_peerId);

			App::wnd()->showFromTray();
			if (App::passcoded()) {
				App::wnd()->setInnerFocus();
				App::wnd()->notifyClear();
			} else {
				App::wnd()->hideSettings();
				bool tomsg = !history->peer->isUser() && (_msgId > 0);
				if (tomsg) {
					HistoryItem *item = App::histItemById(peerToChannel(_peerId), _msgId);
					if (!item || !item->mentionsMe()) {
						tomsg = false;
					}
				}
				Ui::showPeerHistory(history, tomsg ? _msgId : ShowAtUnreadMsgId);
				App::wnd()->notifyClear(history);
			}
			SetForegroundWindow(App::wnd()->psHwnd());
		}
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
			auto i = _notifications.find(_peerId);
			if (i != _notifications.cend()) {
				i.value().remove(_msgId);
				if (i.value().isEmpty()) {
					_notifications.erase(i);
				}
			}
			break;
			}
		}
		return S_OK;
	}

	// DesktopToastFailedEventHandler
	IFACEMETHODIMP Invoke(_In_ IToastNotification *sender, _In_ IToastFailedEventArgs *e) {
		auto i = _notifications.find(_peerId);
		if (i != _notifications.cend()) {
			i.value().remove(_msgId);
			if (i.value().isEmpty()) {
				_notifications.erase(i);
			}
		}
		return S_OK;
	}

	// IUnknown
	IFACEMETHODIMP_(ULONG) AddRef() {
		return InterlockedIncrement(&_ref);
	}

	IFACEMETHODIMP_(ULONG) Release() {
		ULONG l = InterlockedDecrement(&_ref);
		if (l == 0) delete this;
		return l;
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

	ULONG _ref;
	PeerId _peerId;
	MsgId _msgId;
};

QString getImage(const StorageKey &key, PeerData *peer) {
	uint64 ms = getms(true);
	auto i = _images.find(key);
	if (i != _images.cend()) {
		if (i->until) {
			i->until = ms + NotifyDeletePhotoAfter;
			if (App::wnd()) App::wnd()->psCleanNotifyPhotosIn(-NotifyDeletePhotoAfter);
		}
	} else {
		Image v;
		if (key.first) {
			v.until = ms + NotifyDeletePhotoAfter;
			if (App::wnd()) App::wnd()->psCleanNotifyPhotosIn(-NotifyDeletePhotoAfter);
		} else {
			v.until = 0;
		}
		v.path = cWorkingDir() + qsl("tdata/temp/") + QString::number(rand_value<uint64>(), 16) + qsl(".png");
		if (key.first || key.second) {
			peer->saveUserpic(v.path, st::notifyMacPhotoSize);
		} else {
			App::wnd()->iconLarge().save(v.path, "PNG");
		}
		i = _images.insert(key, v);
		_imageSavedFlag = true;
	}
	return i->path;
}

bool create(PeerData *peer, int32 msgId, bool showpix, const QString &title, const QString &subtitle, const QString &msg) {
	if (!supported() || !_notificationManager || !_notifier || !_notificationFactory) return false;

	ComPtr<IXmlDocument> toastXml;
	bool withSubtitle = !subtitle.isEmpty();

	HRESULT hr = _notificationManager->GetTemplateContent(withSubtitle ? ToastTemplateType_ToastImageAndText04 : ToastTemplateType_ToastImageAndText02, &toastXml);
	if (!SUCCEEDED(hr)) return false;

	hr = SetAudioSilent(toastXml.Get());
	if (!SUCCEEDED(hr)) return false;

	StorageKey key;
	QString imagePath;
	if (showpix) {
		key = peer->userpicUniqueKey();
	} else {
		key = StorageKey(0, 0);
	}
	QString image = getImage(key, peer);
	std::wstring wimage = QDir::toNativeSeparators(image).toStdWString();

	hr = SetImageSrc(wimage.c_str(), toastXml.Get());
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
	ComPtr<ToastEventHandler> eventHandler(new ToastEventHandler(peer->id, msgId));

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

} // namespace Toasts
} // namespace Platform
