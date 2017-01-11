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
#include "stdafx.h"
#include "pspecific.h"

#include "lang.h"
#include "application.h"
#include "mainwidget.h"

#include "localstorage.h"

#include "passcodewidget.h"

//#include <Shobjidl.h>
#include <shellapi.h>

//#include <roapi.h>
//#include <wrl\client.h>
//#include <wrl\implements.h>
//#include <windows.ui.notifications.h>

//#pragma warning(push)
//#pragma warning(disable:4091)
//#include <dbghelp.h>
//#include <shlobj.h>
//#pragma warning(pop)

//#include <Shlwapi.h>
//#include <Strsafe.h>
//#include <Windowsx.h>
//#include <WtsApi32.h>

//#include <SDKDDKVer.h>

//#include <sal.h>
//#include <Psapi.h>
//#include <strsafe.h>
//#include <ObjBase.h>
//#include <propvarutil.h>
//#include <functiondiscoverykeys.h>
//#include <intsafe.h>
//#include <guiddef.h>

#include <qpa/qplatformnativeinterface.h>

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) < (b) ? (b) : (a))

#include <gdiplus.h>

#ifndef DCX_USESTYLE
#define DCX_USESTYLE 0x00010000
#endif

#ifndef WM_NCPOINTERUPDATE
#define WM_NCPOINTERUPDATE              0x0241
#define WM_NCPOINTERDOWN                0x0242
#define WM_NCPOINTERUP                  0x0243
#endif

const WCHAR AppUserModelIdRelease[] = L"Telegram.TelegramDesktop";
const WCHAR AppUserModelIdBeta[] = L"Telegram.TelegramDesktop.Beta";

const WCHAR *AppUserModelId() {
	return cBetaVersion() ? AppUserModelIdBeta : AppUserModelIdRelease;
}

static const PROPERTYKEY pkey_AppUserModel_ID = { { 0x9F4C2855, 0x9F79, 0x4B39, { 0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3 } }, 5 };
static const PROPERTYKEY pkey_AppUserModel_StartPinOption = { { 0x9F4C2855, 0x9F79, 0x4B39, { 0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3 } }, 12 };

//using namespace Microsoft::WRL;
//using namespace ABI::Windows::UI::Notifications;
//using namespace ABI::Windows::Data::Xml::Dom;
//using namespace Windows::Foundation;

namespace {
    QStringList _initLogs;

	bool frameless = true;
	bool useTheme = false;
	bool useOpenWith = false;
	bool useOpenAs = false;
	bool useWtsapi = false;
	bool useShellapi = false;
	bool useToast = false;
	bool themeInited = false;
	bool finished = true;
	int menuShown = 0, menuHidden = 0;
	int dleft = 0, dtop = 0;
	QMargins simpleMargins, margins;
	HICON bigIcon = 0, smallIcon = 0, overlayIcon = 0;
	bool sessionLoggedOff = false;

	UINT tbCreatedMsgId = 0;

	//ComPtr<ITaskbarList3> taskbarList;

	//ComPtr<IToastNotificationManagerStatics> toastNotificationManager;
	//ComPtr<IToastNotifier> toastNotifier;
	//ComPtr<IToastNotificationFactory> toastNotificationFactory;
	//struct ToastNotificationPtr {
	//	ToastNotificationPtr() {
	//	}
	//	ToastNotificationPtr(const ComPtr<IToastNotification> &ptr) : p(ptr) {
	//	}
	//	ComPtr<IToastNotification> p;
	//};
	//typedef QMap<PeerId, QMap<MsgId, ToastNotificationPtr> > ToastNotifications;
	//ToastNotifications toastNotifications;
	//struct ToastImage {
	//	uint64 until;
	//	QString path;
	//};
	//typedef QMap<StorageKey, ToastImage> ToastImages;
	//ToastImages toastImages;
	//bool ToastImageSavedFlag = false;

	//HWND createTaskbarHider() {
	//	HINSTANCE appinst = (HINSTANCE)GetModuleHandle(0);
	//	HWND hWnd = 0;

	//	QString cn = QString("TelegramTaskbarHider");
	//	LPCWSTR _cn = (LPCWSTR)cn.utf16();
	//	WNDCLASSEX wc;

	//	wc.cbSize        = sizeof(wc);
	//	wc.style         = 0;
	//	wc.lpfnWndProc   = DefWindowProc;
	//	wc.cbClsExtra    = 0;
	//	wc.cbWndExtra    = 0;
	//	wc.hInstance     = appinst;
	//	wc.hIcon         = 0;
	//	wc.hCursor       = 0;
	//	wc.hbrBackground = 0;
	//	wc.lpszMenuName  = NULL;
	//	wc.lpszClassName = _cn;
	//	wc.hIconSm       = 0;
	//	if (!RegisterClassEx(&wc)) {
	//		DEBUG_LOG(("Application Error: could not register taskbar hider window class, error: %1").arg(GetLastError()));
	//		return hWnd;
	//	}

	//	hWnd = CreateWindowEx(WS_EX_TOOLWINDOW, _cn, 0, WS_POPUP, 0, 0, 0, 0, 0, 0, appinst, 0);
	//	if (!hWnd) {
	//		DEBUG_LOG(("Application Error: could not create taskbar hider window class, error: %1").arg(GetLastError()));
	//		return hWnd;
	//	}
	//	return hWnd;
	//}

	//enum {
	//	_PsShadowMoved = 0x01,
	//	_PsShadowResized = 0x02,
	//	_PsShadowShown = 0x04,
	//	_PsShadowHidden = 0x08,
	//	_PsShadowActivate = 0x10,
	//};

	//enum {
	//	_PsInitHor = 0x01,
	//	_PsInitVer = 0x02,
	//};

	//int32 _psSize = 0;
	//class _PsShadowWindows {
	//public:

	//	_PsShadowWindows() : screenDC(0), max_w(0), max_h(0), _x(0), _y(0), _w(0), _h(0), hidden(true), r(0), g(0), b(0), noKeyColor(RGB(255, 255, 255)) {
	//		for (int i = 0; i < 4; ++i) {
	//			dcs[i] = 0;
	//			bitmaps[i] = 0;
	//			hwnds[i] = 0;
	//		}
	//	}

	//	void setColor(QColor c) {
	//		r = c.red();
	//		g = c.green();
	//		b = c.blue();

	//		if (!hwnds[0]) return;
	//		Gdiplus::SolidBrush brush(Gdiplus::Color(_alphas[0], r, g, b));
	//		for (int i = 0; i < 4; ++i) {
	//			Gdiplus::Graphics graphics(dcs[i]);
	//			graphics.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
	//			if ((i % 2) && _h || !(i % 2) && _w) {
	//				graphics.FillRectangle(&brush, 0, 0, (i % 2) ? _size : _w, (i % 2) ? _h : _size);
	//			}
	//		}
	//		initCorners();

	//		_x = _y = _w = _h = 0;
	//		update(_PsShadowMoved | _PsShadowResized);
	//	}

	//	bool init(QColor c) {
	//		style::rect topLeft = st::wndShadow;
	//		_fullsize = topLeft.width();
	//		_shift = st::wndShadowShift;
	//		QImage cornersImage(_fullsize, _fullsize, QImage::Format_ARGB32_Premultiplied);
	//		{
	//			QPainter p(&cornersImage);
	//			p.drawPixmap(QPoint(0, 0), App::sprite(), topLeft);
	//		}
	//		if (rtl()) cornersImage = cornersImage.mirrored(true, false);
	//		uchar *bits = cornersImage.bits();
	//		if (bits) {
	//			for (
	//				quint32 *p = (quint32*)bits, *end = (quint32*)(bits + cornersImage.byteCount());
	//				p < end;
	//			++p
	//				) {
	//				*p = (*p ^ 0x00ffffff) << 24;
	//			}
	//		}

	//		_metaSize = _fullsize + 2 * _shift;
	//		_alphas.reserve(_metaSize);
	//		_colors.reserve(_metaSize * _metaSize);
	//		for (int32 j = 0; j < _metaSize; ++j) {
	//			for (int32 i = 0; i < _metaSize; ++i) {
	//				_colors.push_back((i < 2 * _shift || j < 2 * _shift) ? 1 : qMax(BYTE(1), BYTE(cornersImage.pixel(QPoint(i - 2 * _shift, j - 2 * _shift)) >> 24)));
	//			}
	//		}
	//		uchar prev = 0;
	//		for (int32 i = 0; i < _metaSize; ++i) {
	//			uchar a = _colors[(_metaSize - 1) * _metaSize + i];
	//			if (a < prev) break;

	//			_alphas.push_back(a);
	//			prev = a;
	//		}
	//		_psSize = _size = _alphas.size() - 2 * _shift;

	//		setColor(c);

	//		Gdiplus::GdiplusStartupInput gdiplusStartupInput;
	//		ULONG_PTR gdiplusToken;
	//		Gdiplus::Status gdiRes = Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

	//		if (gdiRes != Gdiplus::Ok) {
	//			LOG(("Application Error: could not init GDI+, error: %1").arg((int)gdiRes));
	//			return false;
	//		}
	//		blend.AlphaFormat = AC_SRC_ALPHA;
	//		blend.SourceConstantAlpha = 255;
	//		blend.BlendFlags = 0;
	//		blend.BlendOp = AC_SRC_OVER;

	//		screenDC = GetDC(0);
	//		if (!screenDC) {
	//			LOG(("Application Error: could not GetDC(0), error: %2").arg(GetLastError()));
	//			return false;
	//		}

	//		QRect avail(Sandbox::availableGeometry());
	//		max_w = avail.width();
	//		if (max_w < st::wndMinWidth) max_w = st::wndMinWidth;
	//		max_h = avail.height();
	//		if (max_h < st::wndMinHeight) max_h = st::wndMinHeight;

	//		HINSTANCE appinst = (HINSTANCE)GetModuleHandle(0);
	//		HWND hwnd = App::wnd() ? App::wnd()->psHwnd() : 0;

	//		for (int i = 0; i < 4; ++i) {
	//			QString cn = QString("TelegramShadow%1").arg(i);
	//			LPCWSTR _cn = (LPCWSTR)cn.utf16();
	//			WNDCLASSEX wc;

	//			wc.cbSize        = sizeof(wc);
	//			wc.style         = 0;
	//			wc.lpfnWndProc   = wndProc;
	//			wc.cbClsExtra    = 0;
	//			wc.cbWndExtra    = 0;
	//			wc.hInstance     = appinst;
	//			wc.hIcon         = 0;
	//			wc.hCursor       = 0;
	//			wc.hbrBackground = 0;
	//			wc.lpszMenuName  = NULL;
	//			wc.lpszClassName = _cn;
	//			wc.hIconSm       = 0;
	//			if (!RegisterClassEx(&wc)) {
	//				LOG(("Application Error: could not register shadow window class %1, error: %2").arg(i).arg(GetLastError()));
	//				destroy();
	//				return false;
	//			}

	//			hwnds[i] = CreateWindowEx(WS_EX_LAYERED | WS_EX_TOOLWINDOW, _cn, 0, WS_POPUP, 0, 0, 0, 0, 0, 0, appinst, 0);
	//			if (!hwnds[i]) {
	//				LOG(("Application Error: could not create shadow window class %1, error: %2").arg(i).arg(GetLastError()));
	//				destroy();
	//				return false;
	//			}
	//			SetWindowLong(hwnds[i], GWL_HWNDPARENT, (LONG)hwnd);

	//			dcs[i] = CreateCompatibleDC(screenDC);
	//			if (!dcs[i]) {
	//				LOG(("Application Error: could not create dc for shadow window class %1, error: %2").arg(i).arg(GetLastError()));
	//				destroy();
	//				return false;
	//			}

	//			bitmaps[i] = CreateCompatibleBitmap(screenDC, (i % 2) ? _size : max_w, (i % 2) ? max_h : _size);
	//			if (!bitmaps[i]) {
	//				LOG(("Application Error: could not create bitmap for shadow window class %1, error: %2").arg(i).arg(GetLastError()));
	//				destroy();
	//				return false;
	//			}

	//			SelectObject(dcs[i], bitmaps[i]);
	//		}

	//		initCorners();
	//		return true;
	//	}

	//	void initCorners(int directions = (_PsInitHor | _PsInitVer)) {
	//		bool hor = (directions & _PsInitHor), ver = (directions & _PsInitVer);
	//		Gdiplus::Graphics graphics0(dcs[0]), graphics1(dcs[1]), graphics2(dcs[2]), graphics3(dcs[3]);
	//		graphics0.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
	//		graphics1.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
	//		graphics2.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
	//		graphics3.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);

	//		Gdiplus::SolidBrush brush(Gdiplus::Color(_alphas[0], r, g, b));
	//		if (hor) graphics0.FillRectangle(&brush, 0, 0, _fullsize - (_size - _shift), 2 * _shift);

	//		if (ver) {
	//			graphics1.FillRectangle(&brush, 0, 0, _size, 2 * _shift);
	//			graphics3.FillRectangle(&brush, 0, 0, _size, 2 * _shift);
	//			graphics1.FillRectangle(&brush, _size - _shift, 2 * _shift, _shift, _fullsize);
	//			graphics3.FillRectangle(&brush, 0, 2 * _shift, _shift, _fullsize);
	//		}

	//		if (hor) {
	//			for (int j = 2 * _shift; j < _size; ++j) {
	//				for (int k = 0; k < _fullsize - (_size - _shift); ++k) {
	//					brush.SetColor(Gdiplus::Color(_colors[j * _metaSize + k + (_size + _shift)], r, g, b));
	//					graphics0.FillRectangle(&brush, k, j, 1, 1);
	//					graphics2.FillRectangle(&brush, k, _size - (j - 2 * _shift) - 1, 1, 1);
	//				}
	//			}
	//			for (int j = _size; j < _size + 2 * _shift; ++j) {
	//				for (int k = 0; k < _fullsize - (_size - _shift); ++k) {
	//					brush.SetColor(Gdiplus::Color(_colors[j * _metaSize + k + (_size + _shift)], r, g, b));
	//					graphics2.FillRectangle(&brush, k, _size - (j - 2 * _shift) - 1, 1, 1);
	//				}
	//			}
	//		}
	//		if (ver) {
	//			for (int j = 2 * _shift; j < _fullsize + 2 * _shift; ++j) {
	//				for (int k = _shift; k < _size; ++k) {
	//					brush.SetColor(Gdiplus::Color(_colors[j * _metaSize + (k + _shift)], r, g, b));
	//					graphics1.FillRectangle(&brush, _size - k - 1, j, 1, 1);
	//					graphics3.FillRectangle(&brush, k, j, 1, 1);
	//				}
	//			}
	//		}
	//	}
	//	void verCorners(int h, Gdiplus::Graphics *pgraphics1, Gdiplus::Graphics *pgraphics3) {
	//		Gdiplus::SolidBrush brush(Gdiplus::Color(_alphas[0], r, g, b));
	//		pgraphics1->FillRectangle(&brush, _size - _shift, h - _fullsize, _shift, _fullsize);
	//		pgraphics3->FillRectangle(&brush, 0, h - _fullsize, _shift, _fullsize);
	//		for (int j = 0; j < _fullsize; ++j) {
	//			for (int k = _shift; k < _size; ++k) {
	//				brush.SetColor(Gdiplus::Color(_colors[(j + 2 * _shift) * _metaSize + k + _shift], r, g, b));
	//				pgraphics1->FillRectangle(&brush, _size - k - 1, h - j - 1, 1, 1);
	//				pgraphics3->FillRectangle(&brush, k, h - j - 1, 1, 1);
	//			}
	//		}
	//	}
	//	void horCorners(int w, Gdiplus::Graphics *pgraphics0, Gdiplus::Graphics *pgraphics2) {
	//		Gdiplus::SolidBrush brush(Gdiplus::Color(_alphas[0], r, g, b));
	//		pgraphics0->FillRectangle(&brush, w - 2 * _size - (_fullsize - (_size - _shift)), 0, _fullsize - (_size - _shift), 2 * _shift);
	//		for (int j = 2 * _shift; j < _size; ++j) {
	//			for (int k = 0; k < _fullsize - (_size - _shift); ++k) {
	//				brush.SetColor(Gdiplus::Color(_colors[j * _metaSize + k + (_size + _shift)], r, g, b));
	//				pgraphics0->FillRectangle(&brush, w - 2 * _size - k - 1, j, 1, 1);
	//				pgraphics2->FillRectangle(&brush, w - 2 * _size - k - 1, _size - (j - 2 * _shift) - 1, 1, 1);
	//			}
	//		}
	//		for (int j = _size; j < _size + 2 * _shift; ++j) {
	//			for (int k = 0; k < _fullsize - (_size - _shift); ++k) {
	//				brush.SetColor(Gdiplus::Color(_colors[j * _metaSize + k + (_size + _shift)], r, g, b));
	//				pgraphics2->FillRectangle(&brush, w - 2 * _size - k - 1, _size - (j - 2 * _shift) - 1, 1, 1);
	//			}
	//		}
	//	}

	//	void update(int changes, WINDOWPOS *pos = 0) {
	//		HWND hwnd = App::wnd() ? App::wnd()->psHwnd() : 0;
	//		if (!hwnd || !hwnds[0]) return;

	//		if (changes == _PsShadowActivate) {
	//			for (int i = 0; i < 4; ++i) {
	//				SetWindowPos(hwnds[i], hwnd, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	//			}
	//			return;
	//		}

	//		if (changes & _PsShadowHidden) {
	//			if (!hidden) {
	//				for (int i = 0; i < 4; ++i) {
	//					hidden = true;
	//					ShowWindow(hwnds[i], SW_HIDE);
	//				}
	//			}
	//			return;
	//		}
	//		if (!App::wnd()->psPosInited()) return;

	//		int x = _x, y = _y, w = _w, h = _h;
	//		if (pos && (!(pos->flags & SWP_NOMOVE) || !(pos->flags & SWP_NOSIZE) || !(pos->flags & SWP_NOREPOSITION))) {
	//			if (!(pos->flags & SWP_NOMOVE)) {
	//				x = pos->x - _size;
	//				y = pos->y - _size;
	//			} else if (pos->flags & SWP_NOSIZE) {
	//				for (int i = 0; i < 4; ++i) {
	//					SetWindowPos(hwnds[i], hwnd, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	//				}
	//				return;
	//			}
	//			if (!(pos->flags & SWP_NOSIZE)) {
	//				w = pos->cx + 2 * _size;
	//				h = pos->cy + 2 * _size;
	//			}
	//		} else {
	//			RECT r;
	//			GetWindowRect(hwnd, &r);
	//			x = r.left - _size;
	//			y = r.top - _size;
	//			w = r.right + _size - x;
	//			h = r.bottom + _size - y;
	//		}
	//		if (h < 2 * _fullsize + 2 * _shift) {
	//			h = 2 * _fullsize + 2 * _shift;
	//		}
	//		if (w < 2 * (_fullsize + _shift)) {
	//			w = 2 * (_fullsize + _shift);
	//		}

	//		if (w != _w) {
	//			int from = (_w > 2 * (_fullsize + _shift)) ? (_w - _size - _fullsize - _shift) : (_fullsize - (_size - _shift));
	//			int to = w - _size - _fullsize - _shift;
	//			if (w > max_w) {
	//				from = _fullsize - (_size - _shift);
	//				max_w *= 2;
	//				for (int i = 0; i < 4; i += 2) {
	//					DeleteObject(bitmaps[i]);
	//					bitmaps[i] = CreateCompatibleBitmap(screenDC, max_w, _size);
	//					SelectObject(dcs[i], bitmaps[i]);
	//				}
	//				initCorners(_PsInitHor);
	//			}
	//			Gdiplus::Graphics graphics0(dcs[0]), graphics2(dcs[2]);
	//			graphics0.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
	//			graphics2.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
	//			Gdiplus::SolidBrush brush(Gdiplus::Color(_alphas[0], r, g, b));
	//			if (to > from) {
	//				graphics0.FillRectangle(&brush, from, 0, to - from, 2 * _shift);
	//				for (int i = 2 * _shift; i < _size; ++i) {
	//					Gdiplus::Pen pen(Gdiplus::Color(_alphas[i], r, g, b));
	//					graphics0.DrawLine(&pen, from, i, to, i);
	//					graphics2.DrawLine(&pen, from, _size - (i - 2 * _shift) - 1, to, _size - (i - 2 * _shift) - 1);
	//				}
	//				for (int i = _size; i < _size + 2 * _shift; ++i) {
	//					Gdiplus::Pen pen(Gdiplus::Color(_alphas[i], r, g, b));
	//					graphics2.DrawLine(&pen, from, _size - (i - 2 * _shift) - 1, to, _size - (i - 2 * _shift) - 1);
	//				}
	//			}
	//			if (_w > w) {
	//				graphics0.FillRectangle(&brush, w - _size - _fullsize - _shift, 0, _fullsize - (_size - _shift), _size);
	//				graphics2.FillRectangle(&brush, w - _size - _fullsize - _shift, 0, _fullsize - (_size - _shift), _size);
	//			}
	//			horCorners(w, &graphics0, &graphics2);
	//			POINT p0 = { x + _size, y }, p2 = { x + _size, y + h - _size }, f = { 0, 0 };
	//			SIZE s = { w - 2 * _size, _size };
	//			updateWindow(0, &p0, &s);
	//			updateWindow(2, &p2, &s);
	//		} else if (x != _x || y != _y) {
	//			POINT p0 = { x + _size, y }, p2 = { x + _size, y + h - _size };
	//			updateWindow(0, &p0);
	//			updateWindow(2, &p2);
	//		} else if (h != _h) {
	//			POINT p2 = { x + _size, y + h - _size };
	//			updateWindow(2, &p2);
	//		}

	//		if (h != _h) {
	//			int from = (_h > 2 * _fullsize + 2 * _shift) ? (_h - _fullsize) : (_fullsize + 2 * _shift);
	//			int to = h - _fullsize;
	//			if (h > max_h) {
	//				from = (_fullsize + 2 * _shift);
	//				max_h *= 2;
	//				for (int i = 1; i < 4; i += 2) {
	//					DeleteObject(bitmaps[i]);
	//					bitmaps[i] = CreateCompatibleBitmap(dcs[i], _size, max_h);
	//					SelectObject(dcs[i], bitmaps[i]);
	//				}
	//				initCorners(_PsInitVer);
	//			}
	//			Gdiplus::Graphics graphics1(dcs[1]), graphics3(dcs[3]);
	//			graphics1.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
	//			graphics3.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);

	//			Gdiplus::SolidBrush brush(Gdiplus::Color(_alphas[0], r, g, b));
	//			if (to > from) {
	//				graphics1.FillRectangle(&brush, _size - _shift, from, _shift, to - from);
	//				graphics3.FillRectangle(&brush, 0, from, _shift, to - from);
	//				for (int i = 2 * _shift; i < _size + _shift; ++i) {
	//					Gdiplus::Pen pen(Gdiplus::Color(_alphas[i], r, g, b));
	//					graphics1.DrawLine(&pen, _size + _shift - i - 1, from, _size + _shift - i - 1, to);
	//					graphics3.DrawLine(&pen, i - _shift, from, i - _shift, to);
	//				}
	//			}
	//			if (_h > h) {
	//				graphics1.FillRectangle(&brush, 0, h - _fullsize, _size, _fullsize);
	//				graphics3.FillRectangle(&brush, 0, h - _fullsize, _size, _fullsize);
	//			}
	//			verCorners(h, &graphics1, &graphics3);

	//			POINT p1 = {x + w - _size, y}, p3 = {x, y}, f = {0, 0};
	//			SIZE s = { _size, h };
	//			updateWindow(1, &p1, &s);
	//			updateWindow(3, &p3, &s);
	//		} else if (x != _x || y != _y) {
	//			POINT p1 = { x + w - _size, y }, p3 = { x, y };
	//			updateWindow(1, &p1);
	//			updateWindow(3, &p3);
	//		} else if (w != _w) {
	//			POINT p1 = { x + w - _size, y };
	//			updateWindow(1, &p1);
	//		}
	//		_x = x;
	//		_y = y;
	//		_w = w;
	//		_h = h;

	//		if (hidden && (changes & _PsShadowShown)) {
	//			for (int i = 0; i < 4; ++i) {
	//				SetWindowPos(hwnds[i], hwnd, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);
	//			}
	//			hidden = false;
	//		}
	//	}

	//	void updateWindow(int i, POINT *p, SIZE *s = 0) {
	//		static POINT f = {0, 0};
	//		if (s) {
	//			UpdateLayeredWindow(hwnds[i], (s ? screenDC : 0), p, s, (s ? dcs[i] : 0), (s ? (&f) : 0), noKeyColor, &blend, ULW_ALPHA);
	//		} else {
	//			SetWindowPos(hwnds[i], 0, p->x, p->y, 0, 0, SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOZORDER);
	//		}
	//	}

	//	void destroy() {
	//		for (int i = 0; i < 4; ++i) {
	//			if (dcs[i]) DeleteDC(dcs[i]);
	//			if (bitmaps[i]) DeleteObject(bitmaps[i]);
	//			if (hwnds[i]) DestroyWindow(hwnds[i]);
	//			dcs[i] = 0;
	//			bitmaps[i] = 0;
	//			hwnds[i] = 0;
	//		}
	//		if (screenDC) ReleaseDC(0, screenDC);
	//	}

	//private:

	//	int _x, _y, _w, _h;
	//	int _metaSize, _fullsize, _size, _shift;
	//	QVector<BYTE> _alphas, _colors;

	//	bool hidden;

	//	HWND hwnds[4];
	//	HDC dcs[4], screenDC;
	//	HBITMAP bitmaps[4];
	//	int max_w, max_h;
	//	BLENDFUNCTION blend;

	//	BYTE r, g, b;
	//	COLORREF noKeyColor;

	//	static LRESULT CALLBACK _PsShadowWindows::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	//};
	//_PsShadowWindows _psShadowWindows;

	//LRESULT CALLBACK _PsShadowWindows::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	//	if (finished) return DefWindowProc(hwnd, msg, wParam, lParam);

	//	int i;
	//	for (i = 0; i < 4; ++i) {
	//		if (_psShadowWindows.hwnds[i] && hwnd == _psShadowWindows.hwnds[i]) {
	//			break;
	//		}
	//	}
	//	if (i == 4) return DefWindowProc(hwnd, msg, wParam, lParam);

	//	switch (msg) {
	//		case WM_CLOSE:
	//			App::wnd()->close();
	//		break;

	//		case WM_NCHITTEST: {
	//			int32 xPos = GET_X_LPARAM(lParam), yPos = GET_Y_LPARAM(lParam);
	//			switch (i) {
	//			case 0: return HTTOP;
	//			case 1: return (yPos < _psShadowWindows._y + _psSize) ? HTTOPRIGHT : ((yPos >= _psShadowWindows._y + _psShadowWindows._h - _psSize) ? HTBOTTOMRIGHT : HTRIGHT);
	//			case 2: return HTBOTTOM;
	//			case 3: return (yPos < _psShadowWindows._y + _psSize) ? HTTOPLEFT : ((yPos >= _psShadowWindows._y + _psShadowWindows._h - _psSize) ? HTBOTTOMLEFT : HTLEFT);
	//			}
	//			return HTTRANSPARENT;
	//		} break;

	//		case WM_NCACTIVATE: return DefWindowProc(hwnd, msg, wParam, lParam);
	//		case WM_NCLBUTTONDOWN:
	//		case WM_NCLBUTTONUP:
	//		case WM_NCLBUTTONDBLCLK:
	//		case WM_NCMBUTTONDOWN:
	//		case WM_NCMBUTTONUP:
	//		case WM_NCMBUTTONDBLCLK:
	//		case WM_NCRBUTTONDOWN:
	//		case WM_NCRBUTTONUP:
	//		case WM_NCRBUTTONDBLCLK:
	//		case WM_NCXBUTTONDOWN:
	//		case WM_NCXBUTTONUP:
	//		case WM_NCXBUTTONDBLCLK:
	//		case WM_NCMOUSEHOVER:
	//		case WM_NCMOUSELEAVE:
	//		case WM_NCMOUSEMOVE:
	//		case WM_NCPOINTERUPDATE:
	//		case WM_NCPOINTERDOWN:
	//		case WM_NCPOINTERUP:
	//			if (App::wnd() && App::wnd()->psHwnd()) {
	//				if (msg == WM_NCLBUTTONDOWN) {
	//					::SetForegroundWindow(App::wnd()->psHwnd());
	//				}
	//				LRESULT res = SendMessage(App::wnd()->psHwnd(), msg, wParam, lParam);
	//				return res;
	//			}
	//			return 0;
	//		break;
	//		case WM_ACTIVATE:
	//			if (App::wnd() && App::wnd()->psHwnd() && wParam == WA_ACTIVE) {
	//				if ((HWND)lParam != App::wnd()->psHwnd()) {
	//					::SetForegroundWindow(hwnd);
	//					::SetWindowPos(App::wnd()->psHwnd(), hwnd, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	//				}
	//			}
	//			return DefWindowProc(hwnd, msg, wParam, lParam);
	//		break;
	//		default:
	//			return DefWindowProc(hwnd, msg, wParam, lParam);
	//	}
	//	return 0;
	//}

	//QColor _shActive(0, 0, 0), _shInactive(0, 0, 0);

	//typedef HRESULT (FAR STDAPICALLTYPE *f_setWindowTheme)(HWND hWnd, LPCWSTR pszSubAppName, LPCWSTR pszSubIdList);
	//f_setWindowTheme setWindowTheme = 0;

	//typedef HRESULT (FAR STDAPICALLTYPE *f_openAs_RunDLL)(HWND hWnd, HINSTANCE hInstance, LPCWSTR lpszCmdLine, int nCmdShow);
	//f_openAs_RunDLL openAs_RunDLL = 0;

	//typedef HRESULT (FAR STDAPICALLTYPE *f_shOpenWithDialog)(HWND hwndParent, const OPENASINFO *poainfo);
	//f_shOpenWithDialog shOpenWithDialog = 0;

	//typedef HRESULT (FAR STDAPICALLTYPE *f_shAssocEnumHandlers)(PCWSTR pszExtra, ASSOC_FILTER afFilter, IEnumAssocHandlers **ppEnumHandler);
	//f_shAssocEnumHandlers shAssocEnumHandlers = 0;

	//typedef HRESULT (FAR STDAPICALLTYPE *f_shCreateItemFromParsingName)(PCWSTR pszPath, IBindCtx *pbc, REFIID riid, void **ppv);
	//f_shCreateItemFromParsingName shCreateItemFromParsingName = 0;

	//typedef BOOL (FAR STDAPICALLTYPE *f_wtsRegisterSessionNotification)(HWND hWnd, DWORD dwFlags);
	//f_wtsRegisterSessionNotification wtsRegisterSessionNotification = 0;

	//typedef BOOL (FAR STDAPICALLTYPE *f_wtsUnRegisterSessionNotification)(HWND hWnd);
	//f_wtsUnRegisterSessionNotification wtsUnRegisterSessionNotification = 0;

	//typedef HRESULT (FAR STDAPICALLTYPE *f_shQueryUserNotificationState)(QUERY_USER_NOTIFICATION_STATE *pquns);
	//f_shQueryUserNotificationState shQueryUserNotificationState = 0;

	//typedef HRESULT (FAR STDAPICALLTYPE *f_setCurrentProcessExplicitAppUserModelID)(__in PCWSTR AppID);
	//f_setCurrentProcessExplicitAppUserModelID setCurrentProcessExplicitAppUserModelID = 0;

	//typedef HRESULT (FAR STDAPICALLTYPE *f_roGetActivationFactory)(_In_ HSTRING activatableClassId,	_In_ REFIID iid, _COM_Outptr_ void ** factory);
	//f_roGetActivationFactory roGetActivationFactory = 0;

	//typedef HRESULT (FAR STDAPICALLTYPE *f_windowsCreateStringReference)(_In_reads_opt_(length + 1) PCWSTR sourceString, UINT32 length,	_Out_ HSTRING_HEADER * hstringHeader, _Outptr_result_maybenull_ _Result_nullonfailure_ HSTRING * string);
	//f_windowsCreateStringReference windowsCreateStringReference = 0;

	//typedef HRESULT (FAR STDAPICALLTYPE *f_windowsDeleteString)(_In_opt_ HSTRING string);
	//f_windowsDeleteString windowsDeleteString = 0;

	//typedef HRESULT (FAR STDAPICALLTYPE *f_propVariantToString)(_In_ REFPROPVARIANT propvar, _Out_writes_(cch) PWSTR psz, _In_ UINT cch);
	//f_propVariantToString propVariantToString = 0;

	template <typename TFunction>
	bool loadFunction(HINSTANCE dll, LPCSTR name, TFunction &func) {
		if (!dll) return false;

		func = (TFunction)GetProcAddress(dll, name);
		return !!func;
	}

	class _PsInitializer {
	public:
		_PsInitializer() {
			frameless = false;

			setupUx();
			setupShell();
			setupWtsapi();
			setupPropSys();
			setupCombase();

			//useTheme = !!setWindowTheme;
		}
		void setupUx() {
			//HINSTANCE procId = LoadLibrary(L"UXTHEME.DLL");

			//loadFunction(procId, "SetWindowTheme", setWindowTheme);
		}
		void setupShell() {
			//HINSTANCE procId = LoadLibrary(L"SHELL32.DLL");
			//setupOpenWith(procId);
			//setupOpenAs(procId);
			//setupShellapi(procId);
			//setupAppUserModel(procId);
		}
		void setupOpenWith(HINSTANCE procId) {
			//if (!loadFunction(procId, "SHAssocEnumHandlers", shAssocEnumHandlers)) return;
			//if (!loadFunction(procId, "SHCreateItemFromParsingName", shCreateItemFromParsingName)) return;
			//useOpenWith = true;
		}
		void setupOpenAs(HINSTANCE procId) {
			//if (!loadFunction(procId, "SHOpenWithDialog", shOpenWithDialog) && !loadFunction(procId, "OpenAs_RunDLLW", openAs_RunDLL)) return;
			//useOpenAs = true;
		}
		void setupShellapi(HINSTANCE procId) {
			//if (!loadFunction(procId, "SHQueryUserNotificationState", shQueryUserNotificationState)) return;
			//useShellapi = true;
		}
		void setupAppUserModel(HINSTANCE procId) {
			//if (!loadFunction(procId, "SetCurrentProcessExplicitAppUserModelID", setCurrentProcessExplicitAppUserModelID)) return;
		}
		void setupWtsapi() {
			//HINSTANCE procId = LoadLibrary(L"WTSAPI32.DLL");

			//if (!loadFunction(procId, "WTSRegisterSessionNotification", wtsRegisterSessionNotification)) return;
			//if (!loadFunction(procId, "WTSUnRegisterSessionNotification", wtsUnRegisterSessionNotification)) return;
			//useWtsapi = true;
		}
		void setupCombase() {
			//if (!setCurrentProcessExplicitAppUserModelID) return;

			//HINSTANCE procId = LoadLibrary(L"COMBASE.DLL");
			//setupToast(procId);
		}
		void setupPropSys() {
			//HINSTANCE procId = LoadLibrary(L"PROPSYS.DLL");
			//if (!loadFunction(procId, "PropVariantToString", propVariantToString)) return;
		}
		void setupToast(HINSTANCE procId) {
			//if (!propVariantToString) return;
			//if (QSysInfo::windowsVersion() < QSysInfo::WV_WINDOWS8) return;
			//if (!loadFunction(procId, "RoGetActivationFactory", roGetActivationFactory)) return;

			//HINSTANCE otherProcId = LoadLibrary(L"api-ms-win-core-winrt-string-l1-1-0.dll");
			//if (!loadFunction(otherProcId, "WindowsCreateStringReference", windowsCreateStringReference)) return;
			//if (!loadFunction(otherProcId, "WindowsDeleteString", windowsDeleteString)) return;

			//useToast = true;
		}
	};
	_PsInitializer _psInitializer;

	class _PsEventFilter : public QAbstractNativeEventFilter {
	public:
		_PsEventFilter() {
		}

		bool nativeEventFilter(const QByteArray &eventType, void *message, long *result) {
			auto wnd = App::wnd();
			if (!wnd) return false;

			MSG *msg = (MSG*)message;
			if (msg->message == WM_ENDSESSION) {
				App::quit();
				return false;
			}
			if (msg->hwnd == wnd->psHwnd() || msg->hwnd && !wnd->psHwnd()) {
				return mainWindowEvent(msg->hwnd, msg->message, msg->wParam, msg->lParam, (LRESULT*)result);
			}
			return false;
		}

		bool mainWindowEvent(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT *result) {
			//if (tbCreatedMsgId && msg == tbCreatedMsgId) {
			//	HRESULT hr = CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&taskbarList));
			//	if (!SUCCEEDED(hr)) {
			//		taskbarList.Reset();
			//	}
			//}
			switch (msg) {

			case WM_TIMECHANGE: {
				App::wnd()->checkAutoLockIn(100);
			} return false;

			case WM_WTSSESSION_CHANGE: {
				if (wParam == WTS_SESSION_LOGOFF || wParam == WTS_SESSION_LOCK) {
					sessionLoggedOff = true;
				} else if (wParam == WTS_SESSION_LOGON || wParam == WTS_SESSION_UNLOCK) {
					sessionLoggedOff = false;
				}
			} return false;

			case WM_DESTROY: {
				App::quit();
			} return false;

			case WM_ACTIVATE: {
				if (LOWORD(wParam) == WA_CLICKACTIVE) {
					App::wnd()->inactivePress(true);
				}
				//if (LOWORD(wParam) != WA_INACTIVE) {
				//	_psShadowWindows.setColor(_shActive);
				//	_psShadowWindows.update(_PsShadowActivate);
				//} else {
				//	_psShadowWindows.setColor(_shInactive);
				//}
				if (Global::started()) {
					App::wnd()->update();
				}
			} return false;

			case WM_NCPAINT: if (QSysInfo::WindowsVersion >= QSysInfo::WV_WINDOWS8) return false; *result = 0; return true;

			//case WM_NCCALCSIZE: {
			//	WINDOWPLACEMENT wp;
			//	wp.length = sizeof(WINDOWPLACEMENT);
			//	if (GetWindowPlacement(hWnd, &wp) && wp.showCmd == SW_SHOWMAXIMIZED) {
			//		LPNCCALCSIZE_PARAMS params = (LPNCCALCSIZE_PARAMS)lParam;
			//		LPRECT r = (wParam == TRUE) ? &params->rgrc[0] : (LPRECT)lParam;
			//		HMONITOR hMonitor = MonitorFromPoint({ (r->left + r->right) / 2, (r->top + r->bottom) / 2 }, MONITOR_DEFAULTTONEAREST);
			//		if (hMonitor) {
			//			MONITORINFO mi;
			//			mi.cbSize = sizeof(mi);
			//			if (GetMonitorInfo(hMonitor, &mi)) {
			//				*r = mi.rcWork;
			//			}
			//		}
			//	}
			//	*result = 0;
			//	return true;
			//}

			//case WM_NCACTIVATE: {
			//	*result = DefWindowProc(hWnd, msg, wParam, -1);
			//} return true;

			//case WM_WINDOWPOSCHANGING:
			//case WM_WINDOWPOSCHANGED: {
			//	WINDOWPLACEMENT wp;
			//	wp.length = sizeof(WINDOWPLACEMENT);
			//	if (GetWindowPlacement(hWnd, &wp) && (wp.showCmd == SW_SHOWMAXIMIZED || wp.showCmd == SW_SHOWMINIMIZED)) {
			//		_psShadowWindows.update(_PsShadowHidden);
			//	} else {
			//		_psShadowWindows.update(_PsShadowMoved | _PsShadowResized, (WINDOWPOS*)lParam);
			//	}
			//} return false;

			case WM_SIZE: {
				if (App::wnd()) {
					if (wParam == SIZE_MAXIMIZED || wParam == SIZE_RESTORED || wParam == SIZE_MINIMIZED) {
						if (wParam != SIZE_RESTORED || App::wnd()->windowState() != Qt::WindowNoState) {
							Qt::WindowState state = Qt::WindowNoState;
							if (wParam == SIZE_MAXIMIZED) {
								state = Qt::WindowMaximized;
							} else if (wParam == SIZE_MINIMIZED) {
								state = Qt::WindowMinimized;
							}
							emit App::wnd()->windowHandle()->windowStateChanged(state);
						} else {
							App::wnd()->psUpdatedPosition();
						}
						App::wnd()->psUpdateMargins();
						//int changes = (wParam == SIZE_MINIMIZED || wParam == SIZE_MAXIMIZED) ? _PsShadowHidden : (_PsShadowResized | _PsShadowShown);
						//_psShadowWindows.update(changes);
					}
				}
			} return false;

			//case WM_SHOWWINDOW: {
			//	LONG style = GetWindowLong(hWnd, GWL_STYLE);
			//	int changes = _PsShadowResized | ((wParam && !(style & (WS_MAXIMIZE | WS_MINIMIZE))) ? _PsShadowShown : _PsShadowHidden);
			//	_psShadowWindows.update(changes);
			//} return false;

			case WM_MOVE: {
				//_psShadowWindows.update(_PsShadowMoved);
				App::wnd()->psUpdatedPosition();
			} return false;

			//case WM_NCHITTEST: {
			//	POINTS p = MAKEPOINTS(lParam);
			//	RECT r;
			//	GetWindowRect(hWnd, &r);
			//	HitTestType res = App::wnd()->hitTest(QPoint(p.x - r.left + dleft, p.y - r.top + dtop));
			//	switch (res) {
			//		case HitTestClient:
			//		case HitTestSysButton:   *result = HTCLIENT; break;
			//		case HitTestIcon:        *result = HTCAPTION; break;
			//		case HitTestCaption:     *result = HTCAPTION; break;
			//		case HitTestTop:         *result = HTTOP; break;
			//		case HitTestTopRight:    *result = HTTOPRIGHT; break;
			//		case HitTestRight:       *result = HTRIGHT; break;
			//		case HitTestBottomRight: *result = HTBOTTOMRIGHT; break;
			//		case HitTestBottom:      *result = HTBOTTOM; break;
			//		case HitTestBottomLeft:  *result = HTBOTTOMLEFT; break;
			//		case HitTestLeft:        *result = HTLEFT; break;
			//		case HitTestTopLeft:     *result = HTTOPLEFT; break;
			//		case HitTestNone:
			//		default:                 *result = HTTRANSPARENT; break;
			//	};
			//} return true;

			//case WM_NCRBUTTONUP: {
			//	SendMessage(hWnd, WM_SYSCOMMAND, SC_MOUSEMENU, lParam);
			//} return true;

			//case WM_NCLBUTTONDOWN: {
			//	POINTS p = MAKEPOINTS(lParam);
			//	RECT r;
			//	GetWindowRect(hWnd, &r);
			//	HitTestType res = App::wnd()->hitTest(QPoint(p.x - r.left + dleft, p.y - r.top + dtop));
			//	switch (res) {
			//		case HitTestIcon:
			//			if (menuHidden && getms() < menuHidden + 10) {
			//				menuHidden = 0;
			//				if (getms() < menuShown + GetDoubleClickTime()) {
			//					App::wnd()->close();
			//				}
			//			} else {
			//				QRect icon = App::wnd()->iconRect();
			//				p.x = r.left - dleft + icon.left();
			//				p.y = r.top - dtop + icon.top() + icon.height();
			//				App::wnd()->psUpdateSysMenu(App::wnd()->windowHandle()->windowState());
			//				menuShown = getms();
			//				menuHidden = 0;
			//				TrackPopupMenu(App::wnd()->psMenu(), TPM_LEFTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON, p.x, p.y, 0, hWnd, 0);
			//				menuHidden = getms();
			//			}
			//		return true;
			//	};
			//} return false;

			//case WM_NCLBUTTONDBLCLK: {
			//	POINTS p = MAKEPOINTS(lParam);
			//	RECT r;
			//	GetWindowRect(hWnd, &r);
			//	HitTestType res = App::wnd()->hitTest(QPoint(p.x - r.left + dleft, p.y - r.top + dtop));
			//	switch (res) {
			//		case HitTestIcon: App::wnd()->close(); return true;
			//	};
			//} return false;

			//case WM_SYSCOMMAND: {
			//	if (wParam == SC_MOUSEMENU) {
			//		POINTS p = MAKEPOINTS(lParam);
			//		App::wnd()->psUpdateSysMenu(App::wnd()->windowHandle()->windowState());
			//		TrackPopupMenu(App::wnd()->psMenu(), TPM_LEFTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON, p.x, p.y, 0, hWnd, 0);
			//	}
			//} return false;

			case WM_COMMAND: {
				if (HIWORD(wParam)) return false;
				int cmd = LOWORD(wParam);
				switch (cmd) {
					case SC_CLOSE: App::wnd()->close(); return true;
					case SC_MINIMIZE: App::wnd()->setWindowState(Qt::WindowMinimized); return true;
					case SC_MAXIMIZE: App::wnd()->setWindowState(Qt::WindowMaximized); return true;
					case SC_RESTORE: App::wnd()->setWindowState(Qt::WindowNoState); return true;
				}
			} return true;

			}
			return false;
		}
	};
	_PsEventFilter *_psEventFilter = 0;

};

PsMainWindow::PsMainWindow(QWidget *parent) : QMainWindow(parent)
, ps_hWnd(0)
, ps_menu(0)
, icon256(qsl(":/gui/art/icon256.png"))
, iconbig256(qsl(":/gui/art/iconbig256.png"))
, wndIcon(QPixmap::fromImage(icon256, Qt::ColorOnly))
, ps_iconBig(0)
, ps_iconSmall(0)
, ps_iconOverlay(0)
, trayIcon(0)
, trayIconMenu(0)
, posInited(false)
//, ps_tbHider_hWnd(createTaskbarHider())
{
	//tbCreatedMsgId = RegisterWindowMessage(L"TaskbarButtonCreated");
	connect(&ps_cleanNotifyPhotosTimer, SIGNAL(timeout()), this, SLOT(psCleanNotifyPhotos()));
}

void PsMainWindow::psShowTrayMenu() {
	trayIconMenu->popup(QCursor::pos());
}

void PsMainWindow::psCleanNotifyPhotosIn(int32 dt) {
	if (dt < 0) {
		if (ps_cleanNotifyPhotosTimer.isActive() && ps_cleanNotifyPhotosTimer.remainingTime() <= -dt) return;
		dt = -dt;
	}
	ps_cleanNotifyPhotosTimer.start(dt);
}

void PsMainWindow::psCleanNotifyPhotos() {
	//uint64 ms = getms(true), minuntil = 0;
	//for (ToastImages::iterator i = toastImages.begin(); i != toastImages.end();) {
	//	if (!i->until) {
	//		++i;
	//		continue;
	//	}
	//	if (i->until <= ms) {
	//		QFile(i->path).remove();
	//		i = toastImages.erase(i);
	//	} else {
	//		if (!minuntil || minuntil > i->until) {
	//			minuntil = i->until;
	//		}
	//		++i;
	//	}
	//}
	//if (minuntil) psCleanNotifyPhotosIn(int32(minuntil - ms));
}

void PsMainWindow::psRefreshTaskbarIcon() {
	QWidget *w = new QWidget(this);
	w->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
	w->setGeometry(x() + 1, y() + 1, 1, 1);
	QPalette p(w->palette());
	p.setColor(QPalette::Background, st::titleBG->c);
	QWindow *wnd = w->windowHandle();
	w->setPalette(p);
	w->show();
	w->activateWindow();
	delete w;
}

void PsMainWindow::psTrayMenuUpdated() {
}

void PsMainWindow::psSetupTrayIcon() {
    if (!trayIcon) {
        trayIcon = new QSystemTrayIcon(this);

        QIcon icon(QPixmap::fromImage(App::wnd()->iconLarge(), Qt::ColorOnly));

        trayIcon->setIcon(icon);
        trayIcon->setToolTip(str_const_toString(AppName));
        connect(trayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)), this, SLOT(toggleTray(QSystemTrayIcon::ActivationReason)), Qt::UniqueConnection);
        connect(trayIcon, SIGNAL(messageClicked()), this, SLOT(showFromTray()));
        App::wnd()->updateTrayMenu();
    }
    psUpdateCounter();

    trayIcon->show();
    psUpdateDelegate();
}

void PsMainWindow::psUpdateWorkmode() {
	switch (cWorkMode()) {
	case dbiwmWindowAndTray: {
        psSetupTrayIcon();
		//HWND psOwner = (HWND)GetWindowLong(ps_hWnd, GWL_HWNDPARENT);
		//if (psOwner) {
		//	SetWindowLong(ps_hWnd, GWL_HWNDPARENT, 0);
		//	psRefreshTaskbarIcon();
		//}
	} break;

	case dbiwmTrayOnly: {
        psSetupTrayIcon();
		//HWND psOwner = (HWND)GetWindowLong(ps_hWnd, GWL_HWNDPARENT);
		//if (!psOwner) {
		//	SetWindowLong(ps_hWnd, GWL_HWNDPARENT, (LONG)ps_tbHider_hWnd);
		//}
	} break;

	case dbiwmWindowOnly: {
		if (trayIcon) {
			trayIcon->setContextMenu(0);
			trayIcon->deleteLater();
		}
		trayIcon = 0;

		//HWND psOwner = (HWND)GetWindowLong(ps_hWnd, GWL_HWNDPARENT);
		//if (psOwner) {
		//	SetWindowLong(ps_hWnd, GWL_HWNDPARENT, 0);
		//	psRefreshTaskbarIcon();
		//}
	} break;
	}
}

HICON qt_pixmapToWinHICON(const QPixmap &);
HBITMAP qt_pixmapToWinHBITMAP(const QPixmap &, int hbitmapFormat);

static HICON _qt_createHIcon(const QIcon &icon, int xSize, int ySize) {
    if (!icon.isNull()) {
        const QPixmap pm = icon.pixmap(icon.actualSize(QSize(xSize, ySize)));
        if (!pm.isNull())
            return qt_pixmapToWinHICON(pm);
    }
    return 0;
}

void PsMainWindow::psUpdateCounter() {
	int32 counter = App::histories().unreadBadge();
	bool muted = App::histories().unreadOnlyMuted();

	style::color bg = muted ? st::counterMuteBG : st::counterBG;
	QIcon iconSmall, iconBig;
	iconSmall.addPixmap(QPixmap::fromImage(iconWithCounter(16, counter, bg, true), Qt::ColorOnly));
	iconSmall.addPixmap(QPixmap::fromImage(iconWithCounter(32, counter, bg, true), Qt::ColorOnly));
	//iconBig.addPixmap(QPixmap::fromImage(iconWithCounter(32, taskbarList.Get() ? 0 : counter, bg, false), Qt::ColorOnly));
	//iconBig.addPixmap(QPixmap::fromImage(iconWithCounter(64, taskbarList.Get() ? 0 : counter, bg, false), Qt::ColorOnly));
	if (trayIcon) {
		trayIcon->setIcon(iconSmall);
	}

	setWindowTitle((counter > 0) ? qsl("Telegram (%1)").arg(counter) : qsl("Telegram"));
	//psDestroyIcons();
	//ps_iconSmall = _qt_createHIcon(iconSmall, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
	//ps_iconBig = _qt_createHIcon(iconBig, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
	//SendMessage(ps_hWnd, WM_SETICON, 0, (LPARAM)ps_iconSmall);
	//SendMessage(ps_hWnd, WM_SETICON, 1, (LPARAM)(ps_iconBig ? ps_iconBig : ps_iconSmall));
	//if (taskbarList.Get()) {
	//	if (counter > 0) {
	//		QIcon iconOverlay;
	//		iconOverlay.addPixmap(QPixmap::fromImage(iconWithCounter(-16, counter, bg, false), Qt::ColorOnly));
	//		iconOverlay.addPixmap(QPixmap::fromImage(iconWithCounter(-32, counter, bg, false), Qt::ColorOnly));
	//		ps_iconOverlay = _qt_createHIcon(iconOverlay, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
	//	}
	//	QString description = counter > 0 ? QString("%1 unread messages").arg(counter) : qsl("No unread messages");
	//	taskbarList->SetOverlayIcon(ps_hWnd, ps_iconOverlay, description.toStdWString().c_str());
	//}
	//SetWindowPos(ps_hWnd, 0, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void PsMainWindow::psUpdateDelegate() {
}

namespace {
	HMONITOR enumMonitor = 0;
	RECT enumMonitorWork;

	BOOL CALLBACK _monitorEnumProc(
	  _In_  HMONITOR hMonitor,
	  _In_  HDC hdcMonitor,
	  _In_  LPRECT lprcMonitor,
	  _In_  LPARAM dwData
	) {
		//MONITORINFOEX info;
		//info.cbSize = sizeof(info);
		//GetMonitorInfo(hMonitor, &info);
		//if (dwData == hashCrc32(info.szDevice, sizeof(info.szDevice))) {
		//	enumMonitor = hMonitor;
		//	enumMonitorWork = info.rcWork;
		//	return FALSE;
		//}
		return TRUE;
	}
}

void PsMainWindow::psInitSize() {
	setMinimumWidth(st::wndMinWidth);
	setMinimumHeight(st::wndMinHeight);

	TWindowPos pos(cWindowPos());
	QRect avail(Sandbox::availableGeometry());
	bool maximized = false;
	QRect geom(avail.x() + (avail.width() - st::wndDefWidth) / 2, avail.y() + (avail.height() - st::wndDefHeight) / 2, st::wndDefWidth, st::wndDefHeight);
	if (pos.w && pos.h) {
		if (pos.y < 0) pos.y = 0;
		enumMonitor = 0;
		//EnumDisplayMonitors(0, 0, &_monitorEnumProc, pos.moncrc);
		if (enumMonitor) {
			int32 w = enumMonitorWork.right - enumMonitorWork.left, h = enumMonitorWork.bottom - enumMonitorWork.top;
			if (w >= st::wndMinWidth && h >= st::wndMinHeight) {
				if (pos.w > w) pos.w = w;
				if (pos.h > h) pos.h = h;
				pos.x += enumMonitorWork.left;
				pos.y += enumMonitorWork.top;
				if (pos.x < enumMonitorWork.right - 10 && pos.y < enumMonitorWork.bottom - 10) {
					geom = QRect(pos.x, pos.y, pos.w, pos.h);
				}
			}
		}
		maximized = pos.maximized;
	}
	setGeometry(geom);
}

bool InitToastManager();
//bool CreateToast(PeerData *peer, int32 msgId, bool showpix, const QString &title, const QString &subtitle, const QString &msg);
void CheckPinnedAppUserModelId();
void CleanupAppUserModelIdShortcut();

void PsMainWindow::psInitFrameless() {
	psUpdatedPositionTimer.setSingleShot(true);
	connect(&psUpdatedPositionTimer, SIGNAL(timeout()), this, SLOT(psSavePosition()));

	QPlatformNativeInterface *i = QGuiApplication::platformNativeInterface();
    ps_hWnd = static_cast<HWND>(i->nativeResourceForWindow(QByteArrayLiteral("handle"), windowHandle()));

	if (!ps_hWnd) return;

	//if (useWtsapi) wtsRegisterSessionNotification(ps_hWnd, NOTIFY_FOR_THIS_SESSION);

	if (frameless) {
		setWindowFlags(Qt::FramelessWindowHint);
	}

	if (!InitToastManager()) {
		useToast = false;
	}

	psInitSysMenu();
}

void PsMainWindow::psSavePosition(Qt::WindowState state) {
	if (state == Qt::WindowActive) state = windowHandle()->windowState();
	if (state == Qt::WindowMinimized || !posInited) return;

	TWindowPos pos(cWindowPos()), curPos = pos;

	if (state == Qt::WindowMaximized) {
		curPos.maximized = 1;
	} else {
		//RECT w;
		//GetWindowRect(ps_hWnd, &w);
		//curPos.x = w.left;
		//curPos.y = w.top;
		//curPos.w = w.right - w.left;
		//curPos.h = w.bottom - w.top;
		curPos.maximized = 0;
	}

	//HMONITOR hMonitor = MonitorFromWindow(ps_hWnd, MONITOR_DEFAULTTONEAREST);
	//if (hMonitor) {
	//	MONITORINFOEX info;
	//	info.cbSize = sizeof(info);
	//	GetMonitorInfo(hMonitor, &info);
	//	if (!curPos.maximized) {
	//		curPos.x -= info.rcWork.left;
	//		curPos.y -= info.rcWork.top;
	//	}
	//	curPos.moncrc = hashCrc32(info.szDevice, sizeof(info.szDevice));
	//}

	if (curPos.w >= st::wndMinWidth && curPos.h >= st::wndMinHeight) {
		if (curPos.x != pos.x || curPos.y != pos.y || curPos.w != pos.w || curPos.h != pos.h || curPos.moncrc != pos.moncrc || curPos.maximized != pos.maximized) {
			cSetWindowPos(curPos);
			Local::writeSettings();
		}
	}
}

void PsMainWindow::psUpdatedPosition() {
	psUpdatedPositionTimer.start(SaveWindowPositionTimeout);
}

bool PsMainWindow::psHasNativeNotifications() {
	return useToast;
}

Q_DECLARE_METATYPE(QMargins);
void PsMainWindow::psFirstShow() {
	if (useToast) {
		cSetCustomNotifies(!cWindowsNotifications());
	} else {
		cSetCustomNotifies(true);
	}

	//_psShadowWindows.init(_shActive);
	finished = false;

	psUpdateMargins();

	//_psShadowWindows.update(_PsShadowHidden);
	bool showShadows = true;

	show();
	if (cWindowPos().maximized) {
		setWindowState(Qt::WindowMaximized);
	}

	if ((cLaunchMode() == LaunchModeAutoStart && cStartMinimized()) || cStartInTray()) {
		setWindowState(Qt::WindowMinimized);
		if (cWorkMode() == dbiwmTrayOnly || cWorkMode() == dbiwmWindowAndTray) {
			hide();
		} else {
			show();
		}
		showShadows = false;
	} else {
		show();
	}

	posInited = true;
	if (showShadows) {
		//_psShadowWindows.update(_PsShadowMoved | _PsShadowResized | _PsShadowShown);
	}
}

bool PsMainWindow::psHandleTitle() {
	return true;
}

void PsMainWindow::psInitSysMenu() {
	Qt::WindowStates states = windowState();
	//ps_menu = GetSystemMenu(ps_hWnd, FALSE);
	psUpdateSysMenu(windowHandle()->windowState());
}

void PsMainWindow::psUpdateSysMenu(Qt::WindowState state) {
	if (!ps_menu) return;

	int menuToDisable = SC_RESTORE;
	if (state == Qt::WindowMaximized) {
		menuToDisable = SC_MAXIMIZE;
	} else if (state == Qt::WindowMinimized) {
		menuToDisable = SC_MINIMIZE;
	}
	//int itemCount = GetMenuItemCount(ps_menu);
	//for (int i = 0; i < itemCount; ++i) {
	//	MENUITEMINFO itemInfo = {0};
	//	itemInfo.cbSize = sizeof(itemInfo);
	//	itemInfo.fMask = MIIM_TYPE | MIIM_STATE | MIIM_ID;
	//	if (GetMenuItemInfo(ps_menu, i, TRUE, &itemInfo)) {
	//		if (itemInfo.fType & MFT_SEPARATOR) {
	//			continue;
	//		}
	//		if (itemInfo.wID && !(itemInfo.fState & MFS_DEFAULT)) {
	//			UINT fOldState = itemInfo.fState, fState = itemInfo.fState & ~MFS_DISABLED;
	//			if (itemInfo.wID == SC_CLOSE) {
	//				fState |= MFS_DEFAULT;
	//			} else if (itemInfo.wID == menuToDisable || (itemInfo.wID != SC_MINIMIZE && itemInfo.wID != SC_MAXIMIZE && itemInfo.wID != SC_RESTORE)) {
	//				fState |= MFS_DISABLED;
	//			}
	//			itemInfo.fMask = MIIM_STATE;
	//			itemInfo.fState = fState;
	//			if (!SetMenuItemInfo(ps_menu, i, TRUE, &itemInfo)) {
	//				DEBUG_LOG(("PS Error: could not set state %1 to menu item %2, old state %3, error %4").arg(fState).arg(itemInfo.wID).arg(fOldState).arg(GetLastError()));
	//				DestroyMenu(ps_menu);
	//				ps_menu = 0;
	//				break;
	//			}
	//		}
	//	} else {
	//		DEBUG_LOG(("PS Error: could not get state, menu item %1 of %2, error %3").arg(i).arg(itemCount).arg(GetLastError()));
	//		DestroyMenu(ps_menu);
	//		ps_menu = 0;
	//		break;
	//	}
	//}
}

void PsMainWindow::psUpdateMargins() {
	if (!ps_hWnd) return;

	//RECT r, a;

	//GetClientRect(ps_hWnd, &r);
	//a = r;

	//LONG style = GetWindowLong(ps_hWnd, GWL_STYLE), styleEx = GetWindowLong(ps_hWnd, GWL_EXSTYLE);
	//AdjustWindowRectEx(&a, style, false, styleEx);
	//QMargins margins = QMargins(a.left - r.left, a.top - r.top, r.right - a.right, r.bottom - a.bottom);
	//if (style & WS_MAXIMIZE) {
	//	RECT w, m;
	//	GetWindowRect(ps_hWnd, &w);
	//	m = w;

	//	HMONITOR hMonitor = MonitorFromRect(&w, MONITOR_DEFAULTTONEAREST);
	//	if (hMonitor) {
	//		MONITORINFO mi;
	//		mi.cbSize = sizeof(mi);
	//		GetMonitorInfo(hMonitor, &mi);
	//		m = mi.rcWork;
	//	}

	//	dleft = w.left - m.left;
	//	dtop = w.top - m.top;

	//	margins.setLeft(margins.left() - w.left + m.left);
	//	margins.setRight(margins.right() - m.right + w.right);
	//	margins.setBottom(margins.bottom() - m.bottom + w.bottom);
	//	margins.setTop(margins.top() - w.top + m.top);
	//} else {
	//	dleft = dtop = 0;
	//}

	//QPlatformNativeInterface *i = QGuiApplication::platformNativeInterface();
	//i->setWindowProperty(windowHandle()->handle(), qsl("WindowsCustomMargins"), QVariant::fromValue<QMargins>(margins));
	//if (!themeInited) {
	//	themeInited = true;
	//	if (useTheme) {
	//		if (QSysInfo::WindowsVersion < QSysInfo::WV_WINDOWS8) {
	//			setWindowTheme(ps_hWnd, L" ", L" ");
	//			QApplication::setStyle(QStyleFactory::create(qsl("Windows")));
	//		}
	//	}
	//}
}

void PsMainWindow::psFlash() {
	//if (GetForegroundWindow() == ps_hWnd) return;

	//FLASHWINFO info;
	//info.cbSize = sizeof(info);
	//info.hwnd = ps_hWnd;
	//info.dwFlags = FLASHW_ALL;
	//info.dwTimeout = 0;
	//info.uCount = 1;
	//FlashWindowEx(&info);
}

HWND PsMainWindow::psHwnd() const {
	return ps_hWnd;
}

HMENU PsMainWindow::psMenu() const {
	return ps_menu;
}

void PsMainWindow::psDestroyIcons() {
	//if (ps_iconBig) {
 //       DestroyIcon(ps_iconBig);
 //       ps_iconBig = 0;
 //   }
 //   if (ps_iconSmall) {
 //       DestroyIcon(ps_iconSmall);
 //       ps_iconSmall = 0;
 //   }
	//if (ps_iconOverlay) {
	//	DestroyIcon(ps_iconOverlay);
	//	ps_iconOverlay = 0;
	//}
}

PsMainWindow::~PsMainWindow() {
	//if (useWtsapi) {
	//	QPlatformNativeInterface *i = QGuiApplication::platformNativeInterface();
	//	if (HWND hWnd = static_cast<HWND>(i->nativeResourceForWindow(QByteArrayLiteral("handle"), windowHandle()))) {
	//		wtsUnRegisterSessionNotification(hWnd);
	//	}
	//}

	//if (taskbarList) taskbarList.Reset();

	//toastNotifications.clear();
	//if (toastNotificationManager) toastNotificationManager.Reset();
	//if (toastNotifier) toastNotifier.Reset();
	//if (toastNotificationFactory) toastNotificationFactory.Reset();

	finished = true;
	//if (ps_menu) DestroyMenu(ps_menu);
	psDestroyIcons();
	//_psShadowWindows.destroy();
	//if (ps_tbHider_hWnd) DestroyWindow(ps_tbHider_hWnd);
}

namespace {
	QRect _monitorRect;
	uint64 _monitorLastGot = 0;
}

QRect psDesktopRect() {
	uint64 tnow = getms();
	if (tnow > _monitorLastGot + 1000 || tnow < _monitorLastGot) {
		_monitorLastGot = tnow;
		//HMONITOR hMonitor = MonitorFromWindow(App::wnd()->psHwnd(), MONITOR_DEFAULTTONEAREST);
		//if (hMonitor) {
		//	MONITORINFOEX info;
		//	info.cbSize = sizeof(info);
		//	GetMonitorInfo(hMonitor, &info);
		//	_monitorRect = QRect(info.rcWork.left, info.rcWork.top, info.rcWork.right - info.rcWork.left, info.rcWork.bottom - info.rcWork.top);
		//} else {
			_monitorRect = QApplication::desktop()->availableGeometry(App::wnd());
		//}
	}
	return _monitorRect;
}

void psShowOverAll(QWidget *w, bool canFocus) {
}

void psBringToBack(QWidget *w) {
}

void PsMainWindow::psActivateNotify(NotifyWindow *w) {
}

void PsMainWindow::psClearNotifies(PeerId peerId) {
	//if (!toastNotifier) return;

	//if (peerId) {
	//	ToastNotifications::iterator i = toastNotifications.find(peerId);
	//	if (i != toastNotifications.cend()) {
	//		QMap<MsgId, ToastNotificationPtr> temp = i.value();
	//		toastNotifications.erase(i);

	//		for (QMap<MsgId, ToastNotificationPtr>::const_iterator j = temp.cbegin(), e = temp.cend(); j != e; ++j) {
	//			toastNotifier->Hide(j->p.Get());
	//		}
	//	}
	//} else {
	//	ToastNotifications temp = toastNotifications;
	//	toastNotifications.clear();

	//	for (ToastNotifications::const_iterator i = temp.cbegin(), end = temp.cend(); i != end; ++i) {
	//		for (QMap<MsgId, ToastNotificationPtr>::const_iterator j = i->cbegin(), e = i->cend(); j != e; ++j) {
	//			toastNotifier->Hide(j->p.Get());
	//		}
	//	}
	//}
}

void PsMainWindow::psNotifyShown(NotifyWindow *w) {
}

void PsMainWindow::psPlatformNotify(HistoryItem *item, int32 fwdCount) {
	QString title = (!App::passcoded() && cNotifyView() <= dbinvShowName) ? item->history()->peer->name : qsl("Telegram Desktop");
	QString subtitle = (!App::passcoded() && cNotifyView() <= dbinvShowName) ? item->notificationHeader() : QString();
	bool showpix = (!App::passcoded() && cNotifyView() <= dbinvShowName);
	QString msg = (!App::passcoded() && cNotifyView() <= dbinvShowPreview) ? (fwdCount < 2 ? item->notificationText() : lng_forward_messages(lt_count, fwdCount)) : lang(lng_notification_preview);

//	CreateToast(item->history()->peer, item->id, showpix, title, subtitle, msg);
}

QAbstractNativeEventFilter *psNativeEventFilter() {
	delete _psEventFilter;
	_psEventFilter = new _PsEventFilter();
	return _psEventFilter;
}

void psDeleteDir(const QString &dir) {
	std::wstring wDir = QDir::toNativeSeparators(dir).toStdWString();
	WCHAR path[4096];
	memcpy(path, wDir.c_str(), (wDir.size() + 1) * sizeof(WCHAR));
	path[wDir.size() + 1] = 0;
	//SHFILEOPSTRUCT file_op = {
	//	NULL,
	//	FO_DELETE,
	//	path,
	//	L"",
	//	FOF_NOCONFIRMATION |
	//	FOF_NOERRORUI |
	//	FOF_SILENT,
	//	false,
	//	0,
	//	L""
	//};
	//int res = SHFileOperation(&file_op);
}

namespace {
	BOOL CALLBACK _ActivateProcess(HWND hWnd, LPARAM lParam) {
		//uint64 &processId(*(uint64*)lParam);

		//DWORD dwProcessId;
		//::GetWindowThreadProcessId(hWnd, &dwProcessId);

		//if ((uint64)dwProcessId == processId) { // found top-level window
		//	static const int32 nameBufSize = 1024;
		//	WCHAR nameBuf[nameBufSize];
		//	int32 len = GetWindowText(hWnd, nameBuf, nameBufSize);
		//	if (len && len < nameBufSize) {
		//		if (QRegularExpression(qsl("^Telegram(\\s*\\(\\d+\\))?$")).match(QString::fromStdWString(nameBuf)).hasMatch()) {
		//			BOOL res = ::SetForegroundWindow(hWnd);
		//			::SetFocus(hWnd);
		//			return FALSE;
		//		}
		//	}
		//}
		return TRUE;
	}
}

namespace {
	uint64 _lastUserAction = 0;
}

void psUserActionDone() {
	_lastUserAction = getms(true);
	if (sessionLoggedOff) sessionLoggedOff = false;
}

bool psIdleSupported() {
	//LASTINPUTINFO lii;
	//lii.cbSize = sizeof(LASTINPUTINFO);
	//return GetLastInputInfo(&lii);
	return false;
}

uint64 psIdleTime() {
	//LASTINPUTINFO lii;
	//lii.cbSize = sizeof(LASTINPUTINFO);
	//return GetLastInputInfo(&lii) ? (GetTickCount() - lii.dwTime) : (getms(true) - _lastUserAction);
	return (getms(true) - _lastUserAction);
}

bool psSkipAudioNotify() {
	//QUERY_USER_NOTIFICATION_STATE state;
	//if (useShellapi && SUCCEEDED(shQueryUserNotificationState(&state))) {
	//	if (state == QUNS_NOT_PRESENT || state == QUNS_PRESENTATION_MODE) return true;
	//}
	return sessionLoggedOff;
}

bool psSkipDesktopNotify() {
	//QUERY_USER_NOTIFICATION_STATE state;
	//if (useShellapi && SUCCEEDED(shQueryUserNotificationState(&state))) {
	//	if (state == QUNS_PRESENTATION_MODE || state == QUNS_RUNNING_D3D_FULL_SCREEN/* || state == QUNS_BUSY*/) return true;
	//}
	return false;
}

QStringList psInitLogs() {
    return _initLogs;
}

void psClearInitLogs() {
    _initLogs = QStringList();
}

void psActivateProcess(uint64 pid) {
	if (pid) {
		//::EnumWindows((WNDENUMPROC)_ActivateProcess, (LPARAM)&pid);
	}
}

QString psCurrentCountry() {
	//int chCount = GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SISO3166CTRYNAME, 0, 0);
	//if (chCount && chCount < 128) {
	//	WCHAR wstrCountry[128];
	//	int len = GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SISO3166CTRYNAME, wstrCountry, chCount);
	//	return len ? QString::fromStdWString(std::wstring(wstrCountry)) : QString::fromLatin1(DefaultCountry);
	//}
	return QString::fromLatin1(DefaultCountry);
}

namespace {
	QString langById(int lngId) {
		int primary = lngId & 0xFF;
		switch (primary) {
			case 0x36: return qsl("af");
			case 0x1C: return qsl("sq");
			case 0x5E: return qsl("am");
			case 0x01: return qsl("ar");
			case 0x2B: return qsl("hy");
			case 0x4D: return qsl("as");
			case 0x2C: return qsl("az");
			case 0x45: return qsl("bn");
			case 0x6D: return qsl("ba");
			case 0x2D: return qsl("eu");
			case 0x23: return qsl("be");
			case 0x1A:
				if (lngId == LANG_CROATIAN) {
					return qsl("hr");
				} else if (lngId == LANG_BOSNIAN_NEUTRAL || lngId == LANG_BOSNIAN) {
					return qsl("bs");
				}
				return qsl("sr");
			break;
			case 0x7E: return qsl("br");
			case 0x02: return qsl("bg");
			case 0x92: return qsl("ku");
			case 0x03: return qsl("ca");
			case 0x04: return qsl("zh");
			case 0x83: return qsl("co");
			case 0x05: return qsl("cs");
			case 0x06: return qsl("da");
			case 0x65: return qsl("dv");
			case 0x13: return qsl("nl");
			case 0x09: return qsl("en");
			case 0x25: return qsl("et");
			case 0x38: return qsl("fo");
			case 0x0B: return qsl("fi");
			case 0x0c: return qsl("fr");
			case 0x62: return qsl("fy");
			case 0x56: return qsl("gl");
			case 0x37: return qsl("ka");
			case 0x07: return qsl("de");
			case 0x08: return qsl("el");
			case 0x6F: return qsl("kl");
			case 0x47: return qsl("gu");
			case 0x68: return qsl("ha");
			case 0x0D: return qsl("he");
			case 0x39: return qsl("hi");
			case 0x0E: return qsl("hu");
			case 0x0F: return qsl("is");
			case 0x70: return qsl("ig");
			case 0x21: return qsl("id");
			case 0x5D: return qsl("iu");
			case 0x3C: return qsl("ga");
			case 0x34: return qsl("xh");
			case 0x35: return qsl("zu");
			case 0x10: return qsl("it");
			case 0x11: return qsl("ja");
			case 0x4B: return qsl("kn");
			case 0x3F: return qsl("kk");
			case 0x53: return qsl("kh");
			case 0x87: return qsl("rw");
			case 0x12: return qsl("ko");
			case 0x40: return qsl("ky");
			case 0x54: return qsl("lo");
			case 0x26: return qsl("lv");
			case 0x27: return qsl("lt");
			case 0x6E: return qsl("lb");
			case 0x2F: return qsl("mk");
			case 0x3E: return qsl("ms");
			case 0x4C: return qsl("ml");
			case 0x3A: return qsl("mt");
			case 0x81: return qsl("mi");
			case 0x4E: return qsl("mr");
			case 0x50: return qsl("mn");
			case 0x61: return qsl("ne");
			case 0x14: return qsl("no");
			case 0x82: return qsl("oc");
			case 0x48: return qsl("or");
			case 0x63: return qsl("ps");
			case 0x29: return qsl("fa");
			case 0x15: return qsl("pl");
			case 0x16: return qsl("pt");
			case 0x67: return qsl("ff");
			case 0x46: return qsl("pa");
			case 0x18: return qsl("ro");
			case 0x17: return qsl("rm");
			case 0x19: return qsl("ru");
			case 0x3B: return qsl("se");
			case 0x4F: return qsl("sa");
			case 0x32: return qsl("tn");
			case 0x59: return qsl("sd");
			case 0x5B: return qsl("si");
			case 0x1B: return qsl("sk");
			case 0x24: return qsl("sl");
			case 0x0A: return qsl("es");
			case 0x41: return qsl("sw");
			case 0x1D: return qsl("sv");
			case 0x28: return qsl("tg");
			case 0x49: return qsl("ta");
			case 0x44: return qsl("tt");
			case 0x4A: return qsl("te");
			case 0x1E: return qsl("th");
			case 0x51: return qsl("bo");
			case 0x73: return qsl("ti");
			case 0x1F: return qsl("tr");
			case 0x42: return qsl("tk");
			case 0x22: return qsl("uk");
			case 0x20: return qsl("ur");
			case 0x80: return qsl("ug");
			case 0x43: return qsl("uz");
			case 0x2A: return qsl("vi");
			case 0x52: return qsl("cy");
			case 0x88: return qsl("wo");
			case 0x78: return qsl("ii");
			case 0x6A: return qsl("yo");
		}
		return QString::fromLatin1(DefaultLanguage);
	}
}

QString psCurrentLanguage() {
	//int chCount = GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SNAME, 0, 0);
	//if (chCount && chCount < 128) {
	//	WCHAR wstrLocale[128];
	//	int len = GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SNAME, wstrLocale, chCount);
	//	if (!len) return QString::fromLatin1(DefaultLanguage);
	//	QString locale = QString::fromStdWString(std::wstring(wstrLocale));
	//	QRegularExpressionMatch m = QRegularExpression("(^|[^a-z])([a-z]{2})-").match(locale);
	//	if (m.hasMatch()) {
	//		return m.captured(2);
	//	}
	//}
	//chCount = GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_ILANGUAGE, 0, 0);
	//if (chCount && chCount < 128) {
	//	WCHAR wstrLocale[128];
	//	int len = GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_ILANGUAGE, wstrLocale, chCount), lngId = 0;
	//	if (len < 5) return QString::fromLatin1(DefaultLanguage);

	//	for (int i = 0; i < 4; ++i) {
	//		WCHAR ch = wstrLocale[i];
	//		lngId *= 16;
	//		if (ch >= WCHAR('0') && ch <= WCHAR('9')) {
	//			lngId += (ch - WCHAR('0'));
	//		} else if (ch >= WCHAR('A') && ch <= WCHAR('F')) {
	//			lngId += (10 + ch - WCHAR('A'));
	//		} else {
	//			return QString::fromLatin1(DefaultLanguage);
	//		}
	//	}
	//	return langById(lngId);
	//}
	return QString::fromLatin1(DefaultLanguage);
}

QString psAppDataPath() {
	static const int maxFileLen = MAX_PATH * 10;
	//WCHAR wstrPath[maxFileLen];
	//if (GetEnvironmentVariable(L"APPDATA", wstrPath, maxFileLen)) {
	//	QDir appData(QString::fromStdWString(std::wstring(wstrPath)));
	//	return appData.absolutePath() + '/' + str_const_toString(AppName) + '/';
	//}
	return QString();
}

QString psAppDataPathOld() {
	static const int maxFileLen = MAX_PATH * 10;
	//WCHAR wstrPath[maxFileLen];
	//if (GetEnvironmentVariable(L"APPDATA", wstrPath, maxFileLen)) {
	//	QDir appData(QString::fromStdWString(std::wstring(wstrPath)));
	//	return appData.absolutePath() + '/' + str_const_toString(AppNameOld) + '/';
	//}
	return QString();
}

QString psDownloadPath() {
	return QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + '/' + str_const_toString(AppName) + '/';
}

QString psCurrentExeDirectory(int argc, char *argv[]) {
	//LPWSTR *args;
	//int argsCount;
	//args = CommandLineToArgvW(GetCommandLine(), &argsCount);
	//if (args) {
	//	QFileInfo info(QDir::fromNativeSeparators(QString::fromWCharArray(args[0])));
	//	if (info.isFile()) {
	//		return info.absoluteDir().absolutePath() + '/';
	//	}
	//	LocalFree(args);
	//}
	return QString();
}

QString psCurrentExeName(int argc, char *argv[]) {
	//LPWSTR *args;
	//int argsCount;
	//args = CommandLineToArgvW(GetCommandLine(), &argsCount);
	//if (args) {
	//	QFileInfo info(QDir::fromNativeSeparators(QString::fromWCharArray(args[0])));
	//	if (info.isFile()) {
	//		return info.fileName();
	//	}
	//	LocalFree(args);
	//}
	return QString();
}

void psDoCleanup() {
	try {
		psAutoStart(false, true);
		psSendToMenu(false, true);
		CleanupAppUserModelIdShortcut();
	} catch (...) {
	}
}

int psCleanup() {
	__try
	{
		psDoCleanup();
	}
	__except(EXCEPTION_EXECUTE_HANDLER)
	{
		return 0;
	}
	return 0;
}

void psDoFixPrevious() {
	try {
		//static const int bufSize = 4096;
		//DWORD checkType, checkSize = bufSize * 2;
		//WCHAR checkStr[bufSize];

		//QString appId = str_const_toString(AppId);
		//QString newKeyStr1 = QString("Software\\Wow6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\%1_is1").arg(appId);
		//QString newKeyStr2 = QString("Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\%1_is1").arg(appId);
		//QString oldKeyStr1 = QString("SOFTWARE\\Wow6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\%1_is1").arg(appId);
		//QString oldKeyStr2 = QString("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\%1_is1").arg(appId);
		//HKEY newKey1, newKey2, oldKey1, oldKey2;
		//LSTATUS newKeyRes1 = RegOpenKeyEx(HKEY_CURRENT_USER, newKeyStr1.toStdWString().c_str(), 0, KEY_READ, &newKey1);
		//LSTATUS newKeyRes2 = RegOpenKeyEx(HKEY_CURRENT_USER, newKeyStr2.toStdWString().c_str(), 0, KEY_READ, &newKey2);
		//LSTATUS oldKeyRes1 = RegOpenKeyEx(HKEY_LOCAL_MACHINE, oldKeyStr1.toStdWString().c_str(), 0, KEY_READ, &oldKey1);
		//LSTATUS oldKeyRes2 = RegOpenKeyEx(HKEY_LOCAL_MACHINE, oldKeyStr2.toStdWString().c_str(), 0, KEY_READ, &oldKey2);

		//bool existNew1 = (newKeyRes1 == ERROR_SUCCESS) && (RegQueryValueEx(newKey1, L"InstallDate", 0, &checkType, (BYTE*)checkStr, &checkSize) == ERROR_SUCCESS); checkSize = bufSize * 2;
		//bool existNew2 = (newKeyRes2 == ERROR_SUCCESS) && (RegQueryValueEx(newKey2, L"InstallDate", 0, &checkType, (BYTE*)checkStr, &checkSize) == ERROR_SUCCESS); checkSize = bufSize * 2;
		//bool existOld1 = (oldKeyRes1 == ERROR_SUCCESS) && (RegQueryValueEx(oldKey1, L"InstallDate", 0, &checkType, (BYTE*)checkStr, &checkSize) == ERROR_SUCCESS); checkSize = bufSize * 2;
		//bool existOld2 = (oldKeyRes2 == ERROR_SUCCESS) && (RegQueryValueEx(oldKey2, L"InstallDate", 0, &checkType, (BYTE*)checkStr, &checkSize) == ERROR_SUCCESS); checkSize = bufSize * 2;

		//if (newKeyRes1 == ERROR_SUCCESS) RegCloseKey(newKey1);
		//if (newKeyRes2 == ERROR_SUCCESS) RegCloseKey(newKey2);
		//if (oldKeyRes1 == ERROR_SUCCESS) RegCloseKey(oldKey1);
		//if (oldKeyRes2 == ERROR_SUCCESS) RegCloseKey(oldKey2);

		//if (existNew1 || existNew2) {
		//	oldKeyRes1 = existOld1 ? RegDeleteKey(HKEY_LOCAL_MACHINE, oldKeyStr1.toStdWString().c_str()) : ERROR_SUCCESS;
		//	oldKeyRes2 = existOld2 ? RegDeleteKey(HKEY_LOCAL_MACHINE, oldKeyStr2.toStdWString().c_str()) : ERROR_SUCCESS;
		//}

		//QString userDesktopLnk, commonDesktopLnk;
		//WCHAR userDesktopFolder[MAX_PATH], commonDesktopFolder[MAX_PATH];
		//HRESULT userDesktopRes = SHGetFolderPath(0, CSIDL_DESKTOPDIRECTORY, 0, SHGFP_TYPE_CURRENT, userDesktopFolder);
		//HRESULT commonDesktopRes = SHGetFolderPath(0, CSIDL_COMMON_DESKTOPDIRECTORY, 0, SHGFP_TYPE_CURRENT, commonDesktopFolder);
		//if (SUCCEEDED(userDesktopRes)) {
		//	userDesktopLnk = QString::fromWCharArray(userDesktopFolder) + "\\Telegram.lnk";
		//}
		//if (SUCCEEDED(commonDesktopRes)) {
		//	commonDesktopLnk = QString::fromWCharArray(commonDesktopFolder) + "\\Telegram.lnk";
		//}
		//QFile userDesktopFile(userDesktopLnk), commonDesktopFile(commonDesktopLnk);
		//if (QFile::exists(userDesktopLnk) && QFile::exists(commonDesktopLnk) && userDesktopLnk != commonDesktopLnk) {
		//	bool removed = QFile::remove(commonDesktopLnk);
		//}
	} catch (...) {
	}
}

int psFixPrevious() {
	__try
	{
		psDoFixPrevious();
	}
	__except(EXCEPTION_EXECUTE_HANDLER)
	{
		return 0;
	}
	return 0;
}

void psPostprocessFile(const QString &name) {
	//std::wstring zoneFile = QDir::toNativeSeparators(name).toStdWString() + L":Zone.Identifier";
	//HANDLE f = CreateFile(zoneFile.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
	//if (f == INVALID_HANDLE_VALUE) { // :(
	//	return;
	//}

	//const char data[] = "[ZoneTransfer]\r\nZoneId=3\r\n";

	//DWORD written = 0;
	//BOOL result = WriteFile(f, data, sizeof(data), &written, NULL);
	//CloseHandle(f);

	//if (!result || written != sizeof(data)) { // :(
	//	return;
	//}
}

namespace {
	//struct OpenWithApp {
	//	OpenWithApp(const QString &name, HBITMAP icon, IAssocHandler *handler) : name(name), icon(icon), handler(handler) {
	//	}
	//	OpenWithApp(const QString &name, IAssocHandler *handler) : name(name), icon(0), handler(handler) {
	//	}
	//	void destroy() {
	//		if (icon) DeleteBitmap(icon);
	//		if (handler) handler->Release();
	//	}
	//	QString name;
	//	HBITMAP icon;
	//	IAssocHandler *handler;
	//};

	//bool OpenWithAppLess(const OpenWithApp &a, const OpenWithApp &b) {
	//	return a.name < b.name;
	//}

	//HBITMAP _iconToBitmap(LPWSTR icon, int iconindex) {
	//	if (!icon) return 0;
	//	WCHAR tmpIcon[4096];
	//	if (icon[0] == L'@' && SUCCEEDED(SHLoadIndirectString(icon, tmpIcon, 4096, 0))) {
	//		icon = tmpIcon;
	//	}
	//	int32 w = GetSystemMetrics(SM_CXSMICON), h = GetSystemMetrics(SM_CYSMICON);

	//	HICON ico = ExtractIcon(0, icon, iconindex);
	//	if (!ico) {
	//		if (!iconindex) { // try to read image
	//			QImage img(QString::fromWCharArray(icon));
	//			if (!img.isNull()) {
	//				return qt_pixmapToWinHBITMAP(QPixmap::fromImage(img.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)), /* HBitmapAlpha */ 2);
	//			}
	//		}
	//		return 0;
	//	}

	//	HDC screenDC = GetDC(0), hdc = CreateCompatibleDC(screenDC);
	//	HBITMAP result = CreateCompatibleBitmap(screenDC, w, h);
	//	HGDIOBJ was = SelectObject(hdc, result);
	//	DrawIconEx(hdc, 0, 0, ico, w, h, 0, NULL, DI_NORMAL);
	//	SelectObject(hdc, was);
	//	DeleteDC(hdc);
	//	ReleaseDC(0, screenDC);

	//	DestroyIcon(ico);

	//	return (HBITMAP)CopyImage(result, IMAGE_BITMAP, 0, 0, LR_DEFAULTSIZE | LR_CREATEDIBSECTION);
	//}
}

bool psShowOpenWithMenu(int x, int y, const QString &file) {
	if (!useOpenWith || !App::wnd()) return false;

	//bool result = false;
	//QList<OpenWithApp> handlers;
	//IShellItem* pItem = nullptr;
	//if (SUCCEEDED(shCreateItemFromParsingName(QDir::toNativeSeparators(file).toStdWString().c_str(), nullptr, IID_PPV_ARGS(&pItem)))) {
	//	IEnumAssocHandlers *assocHandlers = 0;
	//	if (SUCCEEDED(pItem->BindToHandler(nullptr, BHID_EnumAssocHandlers, IID_PPV_ARGS(&assocHandlers)))) {
	//		HRESULT hr = S_FALSE;
	//		do
	//		{
	//			IAssocHandler *handler = 0;
	//			ULONG ulFetched = 0;
	//			hr = assocHandlers->Next(1, &handler, &ulFetched);
	//			if (FAILED(hr) || hr == S_FALSE || !ulFetched) break;

	//			LPWSTR name = 0;
	//			if (SUCCEEDED(handler->GetUIName(&name))) {
	//				LPWSTR icon = 0;
	//				int iconindex = 0;
	//				if (SUCCEEDED(handler->GetIconLocation(&icon, &iconindex)) && icon) {
	//					handlers.push_back(OpenWithApp(QString::fromWCharArray(name), _iconToBitmap(icon, iconindex), handler));
	//					CoTaskMemFree(icon);
	//				} else {
	//					handlers.push_back(OpenWithApp(QString::fromWCharArray(name), handler));
	//				}
	//				CoTaskMemFree(name);
	//			} else {
	//				handler->Release();
	//			}
	//		} while (hr != S_FALSE);
	//		assocHandlers->Release();
	//	}

	//	if (!handlers.isEmpty()) {
	//		HMENU menu = CreatePopupMenu();
	//		std::sort(handlers.begin(), handlers.end(), OpenWithAppLess);
	//		for (int32 i = 0, l = handlers.size(); i < l; ++i) {
	//			MENUITEMINFO menuInfo = { 0 };
	//			menuInfo.cbSize = sizeof(menuInfo);
	//			menuInfo.fMask = MIIM_STRING | MIIM_DATA | MIIM_ID;
	//			menuInfo.fType = MFT_STRING;
	//			menuInfo.wID = i + 1;
	//			if (handlers.at(i).icon) {
	//				menuInfo.fMask |= MIIM_BITMAP;
	//				menuInfo.hbmpItem = handlers.at(i).icon;
	//			}

	//			QString name = handlers.at(i).name;
	//			if (name.size() > 512) name = name.mid(0, 512);
	//			WCHAR nameArr[1024];
	//			name.toWCharArray(nameArr);
	//			nameArr[name.size()] = 0;
	//			menuInfo.dwTypeData = nameArr;
	//			InsertMenuItem(menu, GetMenuItemCount(menu), TRUE, &menuInfo);
	//		}
	//		MENUITEMINFO sepInfo = { 0 };
	//		sepInfo.cbSize = sizeof(sepInfo);
	//		sepInfo.fMask = MIIM_STRING | MIIM_DATA;
	//		sepInfo.fType = MFT_SEPARATOR;
	//		InsertMenuItem(menu, GetMenuItemCount(menu), true, &sepInfo);

	//		MENUITEMINFO menuInfo = { 0 };
	//		menuInfo.cbSize = sizeof(menuInfo);
	//		menuInfo.fMask = MIIM_STRING | MIIM_DATA | MIIM_ID;
	//		menuInfo.fType = MFT_STRING;
	//		menuInfo.wID = handlers.size() + 1;

	//		QString name = lang(lng_wnd_choose_program_menu);
	//		if (name.size() > 512) name = name.mid(0, 512);
	//		WCHAR nameArr[1024];
	//		name.toWCharArray(nameArr);
	//		nameArr[name.size()] = 0;
	//		menuInfo.dwTypeData = nameArr;
	//		InsertMenuItem(menu, GetMenuItemCount(menu), TRUE, &menuInfo);

	//		int sel = TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON | TPM_RETURNCMD, x, y, 0, App::wnd()->psHwnd(), 0);
	//		DestroyMenu(menu);

	//		if (sel > 0) {
	//			if (sel <= handlers.size()) {
	//				IDataObject *dataObj = 0;
	//				if (SUCCEEDED(pItem->BindToHandler(nullptr, BHID_DataObject, IID_PPV_ARGS(&dataObj))) && dataObj) {
	//					handlers.at(sel - 1).handler->Invoke(dataObj);
	//					dataObj->Release();
	//					result = true;
	//				}
	//			}
	//		} else {
	//			result = true;
	//		}
	//		for (int i = 0, l = handlers.size(); i < l; ++i) {
	//			handlers[i].destroy();
	//		}
	//	}

	//	pItem->Release();
	//}
	//return result;
	return false;
}

void psOpenFile(const QString &name, bool openWith) {
	bool mailtoScheme = name.startsWith(qstr("mailto:"));
	std::wstring wname = mailtoScheme ? name.toStdWString() : QDir::toNativeSeparators(name).toStdWString();

	if (openWith && useOpenAs) {
		//if (shOpenWithDialog) {
		//	OPENASINFO info;
		//	info.oaifInFlags = OAIF_ALLOW_REGISTRATION | OAIF_REGISTER_EXT | OAIF_EXEC;
		//	if (mailtoScheme) info.oaifInFlags |= OAIF_FILE_IS_URI | OAIF_URL_PROTOCOL;
		//	info.pcszClass = NULL;
		//	info.pcszFile = wname.c_str();
		//	shOpenWithDialog(0, &info);
		//} else {
		//	openAs_RunDLL(0, 0, wname.c_str(), SW_SHOWNORMAL);
		//}
	} else {
		//ShellExecute(0, L"open", wname.c_str(), 0, 0, SW_SHOWNORMAL);
	}
	QDesktopServices::openUrl(QUrl::fromLocalFile(name));
}

void psShowInFolder(const QString &name) {
	//QString nameEscaped = QDir::toNativeSeparators(name).replace('"', qsl("\"\""));
	//ShellExecute(0, 0, qsl("explorer").toStdWString().c_str(), (qsl("/select,") + nameEscaped).toStdWString().c_str(), 0, SW_SHOWNORMAL);
}

namespace PlatformSpecific {

	void start() {
	}

	void finish() {
		delete _psEventFilter;
		_psEventFilter = 0;

		//if (ToastImageSavedFlag) {
		//	psDeleteDir(cWorkingDir() + qsl("tdata/temp"));
		//}
	}

	namespace ThirdParty {
		void start() {
		}

		void finish() {
		}
	}

}

namespace {
	//void _psLogError(const char *str, LSTATUS code) {
	//	LPTSTR errorText = NULL, errorTextDefault = L"(Unknown error)";
	//	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&errorText, 0, 0);
	//	if (!errorText) {
	//		errorText = errorTextDefault;
	//	}
	//	LOG((str).arg(code).arg(QString::fromStdWString(errorText)));
	//	if (errorText != errorTextDefault) {
	//		LocalFree(errorText);
	//	}
	//}

	//bool _psOpenRegKey(LPCWSTR key, PHKEY rkey) {
	//	DEBUG_LOG(("App Info: opening reg key %1...").arg(QString::fromStdWString(key)));
	//	LSTATUS status = RegOpenKeyEx(HKEY_CURRENT_USER, key, 0, KEY_QUERY_VALUE | KEY_WRITE, rkey);
	//	if (status != ERROR_SUCCESS) {
	//		if (status == ERROR_FILE_NOT_FOUND) {
	//			status = RegCreateKeyEx(HKEY_CURRENT_USER, key, 0, 0, REG_OPTION_NON_VOLATILE, KEY_QUERY_VALUE | KEY_WRITE, 0, rkey, 0);
	//			if (status != ERROR_SUCCESS) {
	//				QString msg = qsl("App Error: could not create '%1' registry key, error %2").arg(QString::fromStdWString(key)).arg(qsl("%1: %2"));
	//				_psLogError(msg.toUtf8().constData(), status);
	//				return false;
	//			}
	//		} else {
	//			QString msg = qsl("App Error: could not open '%1' registry key, error %2").arg(QString::fromStdWString(key)).arg(qsl("%1: %2"));
	//			_psLogError(msg.toUtf8().constData(), status);
	//			return false;
	//		}
	//	}
	//	return true;
	//}

	//bool _psSetKeyValue(HKEY rkey, LPCWSTR value, QString v) {
	//	static const int bufSize = 4096;
	//	DWORD defaultType, defaultSize = bufSize * 2;
	//	WCHAR defaultStr[bufSize] = { 0 };
	//	if (RegQueryValueEx(rkey, value, 0, &defaultType, (BYTE*)defaultStr, &defaultSize) != ERROR_SUCCESS || defaultType != REG_SZ || defaultSize != (v.size() + 1) * 2 || QString::fromStdWString(defaultStr) != v) {
	//		WCHAR tmp[bufSize] = { 0 };
	//		if (!v.isEmpty()) wsprintf(tmp, v.replace(QChar('%'), qsl("%%")).toStdWString().c_str());
	//		LSTATUS status = RegSetValueEx(rkey, value, 0, REG_SZ, (BYTE*)tmp, (wcslen(tmp) + 1) * sizeof(WCHAR));
	//		if (status != ERROR_SUCCESS) {
	//			QString msg = qsl("App Error: could not set %1, error %2").arg(value ? ('\'' + QString::fromStdWString(value) + '\'') : qsl("(Default)")).arg("%1: %2");
	//			_psLogError(msg.toUtf8().constData(), status);
	//			return false;
	//		}
	//	}
	//	return true;
	//}
}

void RegisterCustomScheme() {
#ifndef TDESKTOP_DISABLE_REGISTER_CUSTOM_SCHEME
	DEBUG_LOG(("App Info: Checking custom scheme 'tg'..."));

	//HKEY rkey;
	//QString exe = QDir::toNativeSeparators(cExeDir() + cExeName());

	//if (!_psOpenRegKey(L"Software\\Classes\\tg", &rkey)) return;
	//if (!_psSetKeyValue(rkey, L"URL Protocol", QString())) return;
	//if (!_psSetKeyValue(rkey, 0, qsl("URL:Telegram Link"))) return;

	//if (!_psOpenRegKey(L"Software\\Classes\\tg\\DefaultIcon", &rkey)) return;
	//if (!_psSetKeyValue(rkey, 0, '"' + exe + qsl(",1\""))) return;

	//if (!_psOpenRegKey(L"Software\\Classes\\tg\\shell", &rkey)) return;
	//if (!_psOpenRegKey(L"Software\\Classes\\tg\\shell\\open", &rkey)) return;
	//if (!_psOpenRegKey(L"Software\\Classes\\tg\\shell\\open\\command", &rkey)) return;
	//if (!_psSetKeyValue(rkey, 0, '"' + exe + qsl("\" -workdir \"") + cWorkingDir() + qsl("\" -- \"%1\""))) return;
#endif // !TDESKTOP_DISABLE_REGISTER_CUSTOM_SCHEME
}

void psNewVersion() {
	RegisterCustomScheme();
	if (Local::oldSettingsVersion() < 8051) {
		CheckPinnedAppUserModelId();
	}
}

void psExecUpdater() {
	QString targs = qsl("-update");
	if (cLaunchMode() == LaunchModeAutoStart) targs += qsl(" -autostart");
	if (cDebug()) targs += qsl(" -debug");
	if (cStartInTray()) targs += qsl(" -startintray");
	if (cWriteProtected()) targs += qsl(" -writeprotected \"") + cExeDir() + '"';

	QString updaterPath = cWriteProtected() ? (cWorkingDir() + qsl("tupdates/temp/Updater.exe")) : (cExeDir() + qsl("Updater.exe"));

	QString updater(QDir::toNativeSeparators(updaterPath)), wdir(QDir::toNativeSeparators(cWorkingDir()));

	//DEBUG_LOG(("Application Info: executing %1 %2").arg(cExeDir() + "Updater.exe").arg(targs));
	//HINSTANCE r = ShellExecute(0, cWriteProtected() ? L"runas" : 0, updater.toStdWString().c_str(), targs.toStdWString().c_str(), wdir.isEmpty() ? 0 : wdir.toStdWString().c_str(), SW_SHOWNORMAL);
	//if (long(r) < 32) {
	//	DEBUG_LOG(("Application Error: failed to execute %1, working directory: '%2', result: %3").arg(updater).arg(wdir).arg(long(r)));
	//	psDeleteDir(cWorkingDir() + qsl("tupdates/temp"));
	//}
}

void psExecTelegram(const QString &crashreport) {
	QString targs = crashreport.isEmpty() ? qsl("-noupdate") : ('"' + crashreport + '"');
	if (crashreport.isEmpty()) {
		if (cRestartingToSettings()) targs += qsl(" -tosettings");
		if (cLaunchMode() == LaunchModeAutoStart) targs += qsl(" -autostart");
		if (cDebug()) targs += qsl(" -debug");
		if (cStartInTray()) targs += qsl(" -startintray");
		if (cTestMode()) targs += qsl(" -testmode");
		if (cDataFile() != qsl("data")) targs += qsl(" -key \"") + cDataFile() + '"';
	}
	QString telegram(QDir::toNativeSeparators(cExeDir() + cExeName())), wdir(QDir::toNativeSeparators(cWorkingDir()));

	DEBUG_LOG(("Application Info: executing %1 %2").arg(cExeDir() + cExeName()).arg(targs));
	Logs::closeMain();
	SignalHandlers::finish();
	//HINSTANCE r = ShellExecute(0, 0, telegram.toStdWString().c_str(), targs.toStdWString().c_str(), wdir.isEmpty() ? 0 : wdir.toStdWString().c_str(), SW_SHOWNORMAL);
	//if (long(r) < 32) {
	//	DEBUG_LOG(("Application Error: failed to execute %1, working directory: '%2', result: %3").arg(telegram).arg(wdir).arg(long(r)));
	//}
}

void _manageAppLnk(bool create, bool silent, int path_csidl, const wchar_t *args, const wchar_t *description) {
	//WCHAR startupFolder[MAX_PATH];
	//HRESULT hr = SHGetFolderPath(0, path_csidl, 0, SHGFP_TYPE_CURRENT, startupFolder);
	//if (SUCCEEDED(hr)) {
	//	QString lnk = QString::fromWCharArray(startupFolder) + '\\' + str_const_toString(AppFile) + qsl(".lnk");
	//	if (create) {
	//		ComPtr<IShellLink> shellLink;
	//		hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shellLink));
	//		if (SUCCEEDED(hr)) {
	//			ComPtr<IPersistFile> persistFile;

	//			QString exe = QDir::toNativeSeparators(cExeDir() + cExeName()), dir = QDir::toNativeSeparators(QDir(cWorkingDir()).absolutePath());
	//			shellLink->SetArguments(args);
	//			shellLink->SetPath(exe.toStdWString().c_str());
	//			shellLink->SetWorkingDirectory(dir.toStdWString().c_str());
	//			shellLink->SetDescription(description);

	//			ComPtr<IPropertyStore> propertyStore;
	//			hr = shellLink.As(&propertyStore);
	//			if (SUCCEEDED(hr)) {
	//				PROPVARIANT appIdPropVar;
	//				hr = InitPropVariantFromString(AppUserModelId(), &appIdPropVar);
	//				if (SUCCEEDED(hr)) {
	//					hr = propertyStore->SetValue(pkey_AppUserModel_ID, appIdPropVar);
	//					PropVariantClear(&appIdPropVar);
	//					if (SUCCEEDED(hr)) {
	//						hr = propertyStore->Commit();
	//					}
	//				}
	//			}

	//			hr = shellLink.As(&persistFile);
	//			if (SUCCEEDED(hr)) {
	//				hr = persistFile->Save(lnk.toStdWString().c_str(), TRUE);
	//			} else {
	//				if (!silent) LOG(("App Error: could not create interface IID_IPersistFile %1").arg(hr));
	//			}
	//		} else {
	//			if (!silent) LOG(("App Error: could not create instance of IID_IShellLink %1").arg(hr));
	//		}
	//	} else {
	//		QFile::remove(lnk);
	//	}
	//} else {
	//	if (!silent) LOG(("App Error: could not get CSIDL %1 folder %2").arg(path_csidl).arg(hr));
	//}
}

void psAutoStart(bool start, bool silent) {
	//_manageAppLnk(start, silent, CSIDL_STARTUP, L"-autostart", L"Telegram autorun link.\nYou can disable autorun in Telegram settings.");
}

void psSendToMenu(bool send, bool silent) {
	//_manageAppLnk(send, silent, CSIDL_SENDTO, L"-sendpath", L"Telegram send to link.\nYou can disable send to menu item in Telegram settings.");
}

void psUpdateOverlayed(TWidget *widget) {
	bool wm = widget->testAttribute(Qt::WA_Mapped), wv = widget->testAttribute(Qt::WA_WState_Visible);
	if (!wm) widget->setAttribute(Qt::WA_Mapped, true);
	if (!wv) widget->setAttribute(Qt::WA_WState_Visible, true);
	widget->update();
	QEvent e(QEvent::UpdateRequest);
	widget->event(&e);
	if (!wm) widget->setAttribute(Qt::WA_Mapped, false);
	if (!wv) widget->setAttribute(Qt::WA_WState_Visible, false);
}

void psWriteDump() {
}

//class StringReferenceWrapper {
//public:
//
//	StringReferenceWrapper(_In_reads_(length) PCWSTR stringRef, _In_ UINT32 length) throw()	{
//		HRESULT hr = windowsCreateStringReference(stringRef, length, &_header, &_hstring);
//		if (!SUCCEEDED(hr)) {
//			RaiseException(static_cast<DWORD>(STATUS_INVALID_PARAMETER), EXCEPTION_NONCONTINUABLE, 0, nullptr);
//		}
//	}
//
//	~StringReferenceWrapper() {
//		windowsDeleteString(_hstring);
//	}
//
//	template <size_t N>
//	StringReferenceWrapper(_In_reads_(N) wchar_t const (&stringRef)[N]) throw() {
//		UINT32 length = N - 1;
//		HRESULT hr = windowsCreateStringReference(stringRef, length, &_header, &_hstring);
//		if (!SUCCEEDED(hr))	{
//			RaiseException(static_cast<DWORD>(STATUS_INVALID_PARAMETER), EXCEPTION_NONCONTINUABLE, 0, nullptr);
//		}
//	}
//
//	template <size_t _>
//	StringReferenceWrapper(_In_reads_(_) wchar_t(&stringRef)[_]) throw() {
//		UINT32 length;
//		HRESULT hr = SizeTToUInt32(wcslen(stringRef), &length);
//		if (!SUCCEEDED(hr)) {
//			RaiseException(static_cast<DWORD>(STATUS_INVALID_PARAMETER), EXCEPTION_NONCONTINUABLE, 0, nullptr);
//		}
//
//		windowsCreateStringReference(stringRef, length, &_header, &_hstring);
//	}
//
//	HSTRING Get() const throw()	{
//		return _hstring;
//	}
//
//private:
//	HSTRING _hstring;
//	HSTRING_HEADER _header;
//
//};

//HRESULT SetNodeValueString(_In_ HSTRING inputString, _In_ IXmlNode *node, _In_ IXmlDocument *xml) {
//	ComPtr<IXmlText> inputText;
//
//	HRESULT hr = xml->CreateTextNode(inputString, &inputText);
//	if (!SUCCEEDED(hr)) return hr;
//	ComPtr<IXmlNode> inputTextNode;
//
//	hr = inputText.As(&inputTextNode);
//	if (!SUCCEEDED(hr)) return hr;
//
//	ComPtr<IXmlNode> pAppendedChild;
//	return node->AppendChild(inputTextNode.Get(), &pAppendedChild);
//}

//HRESULT SetAudioSilent(_In_ IXmlDocument *toastXml) {
//	ComPtr<IXmlNodeList> nodeList;
//	HRESULT hr = toastXml->GetElementsByTagName(StringReferenceWrapper(L"audio").Get(), &nodeList);
//	if (!SUCCEEDED(hr)) return hr;
//
//	ComPtr<IXmlNode> audioNode;
//	hr = nodeList->Item(0, &audioNode);
//	if (!SUCCEEDED(hr)) return hr;
//
//	if (audioNode) {
//		ComPtr<IXmlElement> audioElement;
//		hr = audioNode.As(&audioElement);
//		if (!SUCCEEDED(hr)) return hr;
//
//		hr = audioElement->SetAttribute(StringReferenceWrapper(L"silent").Get(), StringReferenceWrapper(L"true").Get());
//		if (!SUCCEEDED(hr)) return hr;
//	} else {
//		ComPtr<IXmlElement> audioElement;
//		hr = toastXml->CreateElement(StringReferenceWrapper(L"audio").Get(), &audioElement);
//		if (!SUCCEEDED(hr)) return hr;
//
//		hr = audioElement->SetAttribute(StringReferenceWrapper(L"silent").Get(), StringReferenceWrapper(L"true").Get());
//		if (!SUCCEEDED(hr)) return hr;
//
//		ComPtr<IXmlNode> audioNode;
//		hr = audioElement.As(&audioNode);
//		if (!SUCCEEDED(hr)) return hr;
//
//		ComPtr<IXmlNodeList> nodeList;
//		hr = toastXml->GetElementsByTagName(StringReferenceWrapper(L"toast").Get(), &nodeList);
//		if (!SUCCEEDED(hr)) return hr;
//
//		ComPtr<IXmlNode> toastNode;
//		hr = nodeList->Item(0, &toastNode);
//		if (!SUCCEEDED(hr)) return hr;
//
//		ComPtr<IXmlNode> appendedNode;
//		hr = toastNode->AppendChild(audioNode.Get(), &appendedNode);
//	}
//	return hr;
//}

//HRESULT SetImageSrc(_In_z_ const wchar_t *imagePath, _In_ IXmlDocument *toastXml) {
//	wchar_t imageSrc[MAX_PATH] = L"file:///";
//	HRESULT hr = StringCchCat(imageSrc, ARRAYSIZE(imageSrc), imagePath);
//	if (!SUCCEEDED(hr)) return hr;
//
//	ComPtr<IXmlNodeList> nodeList;
//	hr = toastXml->GetElementsByTagName(StringReferenceWrapper(L"image").Get(), &nodeList);
//	if (!SUCCEEDED(hr)) return hr;
//
//	ComPtr<IXmlNode> imageNode;
//	hr = nodeList->Item(0, &imageNode);
//	if (!SUCCEEDED(hr)) return hr;
//
//	ComPtr<IXmlNamedNodeMap> attributes;
//	hr = imageNode->get_Attributes(&attributes);
//	if (!SUCCEEDED(hr)) return hr;
//
//	ComPtr<IXmlNode> srcAttribute;
//	hr = attributes->GetNamedItem(StringReferenceWrapper(L"src").Get(), &srcAttribute);
//	if (!SUCCEEDED(hr)) return hr;
//
//	return SetNodeValueString(StringReferenceWrapper(imageSrc).Get(), srcAttribute.Get(), toastXml);
//}

//typedef ABI::Windows::Foundation::ITypedEventHandler<ToastNotification*, ::IInspectable *> DesktopToastActivatedEventHandler;
//typedef ABI::Windows::Foundation::ITypedEventHandler<ToastNotification*, ToastDismissedEventArgs*> DesktopToastDismissedEventHandler;
//typedef ABI::Windows::Foundation::ITypedEventHandler<ToastNotification*, ToastFailedEventArgs*> DesktopToastFailedEventHandler;

//class ToastEventHandler : public Implements<DesktopToastActivatedEventHandler, DesktopToastDismissedEventHandler, DesktopToastFailedEventHandler> {
//public:
//
//	ToastEventHandler::ToastEventHandler(const PeerId &peer, MsgId msg) : _ref(1), _peerId(peer), _msgId(msg) {
//	}
//	~ToastEventHandler() {
//	}
//
//	// DesktopToastActivatedEventHandler
//	IFACEMETHODIMP Invoke(_In_ IToastNotification *sender, _In_ IInspectable* args) {
//		ToastNotifications::iterator i = toastNotifications.find(_peerId);
//		if (i != toastNotifications.cend()) {
//			i.value().remove(_msgId);
//			if (i.value().isEmpty()) {
//				toastNotifications.erase(i);
//			}
//		}
//		if (App::wnd()) {
//			History *history = App::history(_peerId);
//
//			App::wnd()->showFromTray();
//			if (App::passcoded()) {
//				App::wnd()->setInnerFocus();
//				App::wnd()->notifyClear();
//			} else {
//				App::wnd()->hideSettings();
//				bool tomsg = !history->peer->isUser() && (_msgId > 0);
//				if (tomsg) {
//					HistoryItem *item = App::histItemById(peerToChannel(_peerId), _msgId);
//					if (!item || !item->mentionsMe()) {
//						tomsg = false;
//					}
//				}
//				Ui::showPeerHistory(history, tomsg ? _msgId : ShowAtUnreadMsgId);
//				App::wnd()->notifyClear(history);
//			}
//			//SetForegroundWindow(App::wnd()->psHwnd());
//		}
//		return S_OK;
//	}
//
//	// DesktopToastDismissedEventHandler
//	IFACEMETHODIMP Invoke(_In_ IToastNotification *sender, _In_ IToastDismissedEventArgs *e)  {
//		ToastDismissalReason tdr;
//		if (SUCCEEDED(e->get_Reason(&tdr))) {
//			switch (tdr) {
//			case ToastDismissalReason_ApplicationHidden:
//				break;
//			case ToastDismissalReason_UserCanceled:
//			case ToastDismissalReason_TimedOut:
//			default:
//				ToastNotifications::iterator i = toastNotifications.find(_peerId);
//				if (i != toastNotifications.cend()) {
//					i.value().remove(_msgId);
//					if (i.value().isEmpty()) {
//						toastNotifications.erase(i);
//					}
//				}
//				break;
//			}
//		}
//		return S_OK;
//	}
//
//	// DesktopToastFailedEventHandler
//	IFACEMETHODIMP Invoke(_In_ IToastNotification *sender, _In_ IToastFailedEventArgs *e) {
//		ToastNotifications::iterator i = toastNotifications.find(_peerId);
//		if (i != toastNotifications.cend()) {
//			i.value().remove(_msgId);
//			if (i.value().isEmpty()) {
//				toastNotifications.erase(i);
//			}
//		}
//		return S_OK;
//	}
//
//	// IUnknown
//	IFACEMETHODIMP_(ULONG) AddRef() {
//		return InterlockedIncrement(&_ref);
//	}
//
//	IFACEMETHODIMP_(ULONG) Release() {
//		ULONG l = InterlockedDecrement(&_ref);
//		if (l == 0) delete this;
//		return l;
//	}
//
//	IFACEMETHODIMP QueryInterface(_In_ REFIID riid, _COM_Outptr_ void **ppv) {
//		if (IsEqualIID(riid, IID_IUnknown))
//			*ppv = static_cast<IUnknown*>(static_cast<DesktopToastActivatedEventHandler*>(this));
//		else if (IsEqualIID(riid, __uuidof(DesktopToastActivatedEventHandler)))
//			*ppv = static_cast<DesktopToastActivatedEventHandler*>(this);
//		else if (IsEqualIID(riid, __uuidof(DesktopToastDismissedEventHandler)))
//			*ppv = static_cast<DesktopToastDismissedEventHandler*>(this);
//		else if (IsEqualIID(riid, __uuidof(DesktopToastFailedEventHandler)))
//			*ppv = static_cast<DesktopToastFailedEventHandler*>(this);
//		else *ppv = nullptr;
//
//		if (*ppv) {
//			reinterpret_cast<IUnknown*>(*ppv)->AddRef();
//			return S_OK;
//		}
//
//		return E_NOINTERFACE;
//	}
//
//private:
//
//	ULONG _ref;
//	PeerId _peerId;
//	MsgId _msgId;
//};
//
//template<class T>
//_Check_return_ __inline HRESULT _1_GetActivationFactory(_In_ HSTRING activatableClassId, _COM_Outptr_ T** factory) {
//	return roGetActivationFactory(activatableClassId, IID_INS_ARGS(factory));
//}
//
//template<typename T>
//inline HRESULT wrap_GetActivationFactory(_In_ HSTRING activatableClassId, _Inout_ Details::ComPtrRef<T> factory) throw() {
//	return _1_GetActivationFactory(activatableClassId, factory.ReleaseAndGetAddressOf());
//}

//QString toastImage(const StorageKey &key, PeerData *peer) {
//	uint64 ms = getms(true);
//	ToastImages::iterator i = toastImages.find(key);
//	if (i != toastImages.cend()) {
//		if (i->until) {
//			i->until = ms + NotifyDeletePhotoAfter;
//			if (App::wnd()) App::wnd()->psCleanNotifyPhotosIn(-NotifyDeletePhotoAfter);
//		}
//	} else {
//		ToastImage v;
//		if (key.first) {
//			v.until = ms + NotifyDeletePhotoAfter;
//			if (App::wnd()) App::wnd()->psCleanNotifyPhotosIn(-NotifyDeletePhotoAfter);
//		} else {
//			v.until = 0;
//		}
//		v.path = cWorkingDir() + qsl("tdata/temp/") + QString::number(rand_value<uint64>(), 16) + qsl(".png");
//		if (key.first || key.second) {
//			peer->saveUserpic(v.path);
//		} else {
//			App::wnd()->iconLarge().save(v.path, "PNG");
//		}
//		i = toastImages.insert(key, v);
//		ToastImageSavedFlag = true;
//	}
//	return i->path;
//}

//bool CreateToast(PeerData *peer, int32 msgId, bool showpix, const QString &title, const QString &subtitle, const QString &msg) {
//	if (!useToast || !toastNotificationManager || !toastNotifier || !toastNotificationFactory) return false;
//
//	ComPtr<IXmlDocument> toastXml;
//	bool withSubtitle = !subtitle.isEmpty();
//
//	HRESULT hr = toastNotificationManager->GetTemplateContent(withSubtitle ? ToastTemplateType_ToastImageAndText04 : ToastTemplateType_ToastImageAndText02, &toastXml);
//	if (!SUCCEEDED(hr)) return false;
//
//	//hr = SetAudioSilent(toastXml.Get());
//	//if (!SUCCEEDED(hr)) return false;
//
//	StorageKey key;
//	QString imagePath;
//	if (showpix) {
//		key = peer->userpicUniqueKey();
//	} else {
//		key = StorageKey(0, 0);
//	}
//	QString image = toastImage(key, peer);
//	std::wstring wimage = QDir::toNativeSeparators(image).toStdWString();
//
//	//hr = SetImageSrc(wimage.c_str(), toastXml.Get());
//	//if (!SUCCEEDED(hr)) return false;
//
//	ComPtr<IXmlNodeList> nodeList;
//	//hr = toastXml->GetElementsByTagName(StringReferenceWrapper(L"text").Get(), &nodeList);
//	//if (!SUCCEEDED(hr)) return false;
//
//	UINT32 nodeListLength;
//	hr = nodeList->get_Length(&nodeListLength);
//	if (!SUCCEEDED(hr)) return false;
//
//	if (nodeListLength < (withSubtitle ? 3U : 2U)) return false;
//
//	{
//		ComPtr<IXmlNode> textNode;
//		hr = nodeList->Item(0, &textNode);
//		if (!SUCCEEDED(hr)) return false;
//
//		std::wstring wtitle = title.toStdWString();
//		//hr = SetNodeValueString(StringReferenceWrapper(wtitle.data(), wtitle.size()).Get(), textNode.Get(), toastXml.Get());
//		//if (!SUCCEEDED(hr)) return false;
//	}
//	if (withSubtitle) {
//		ComPtr<IXmlNode> textNode;
//		hr = nodeList->Item(1, &textNode);
//		if (!SUCCEEDED(hr)) return false;
//
//		std::wstring wsubtitle = subtitle.toStdWString();
//		//hr = SetNodeValueString(StringReferenceWrapper(wsubtitle.data(), wsubtitle.size()).Get(), textNode.Get(), toastXml.Get());
//		//if (!SUCCEEDED(hr)) return false;
//	}
//	{
//		ComPtr<IXmlNode> textNode;
//		hr = nodeList->Item(withSubtitle ? 2 : 1, &textNode);
//		if (!SUCCEEDED(hr)) return false;
//
//		std::wstring wmsg = msg.toStdWString();
//		//hr = SetNodeValueString(StringReferenceWrapper(wmsg.data(), wmsg.size()).Get(), textNode.Get(), toastXml.Get());
//		//if (!SUCCEEDED(hr)) return false;
//	}
//
//	ComPtr<IToastNotification> toast;
//	hr = toastNotificationFactory->CreateToastNotification(toastXml.Get(), &toast);
//	if (!SUCCEEDED(hr)) return false;
//
//	//EventRegistrationToken activatedToken, dismissedToken, failedToken;
//	//ComPtr<ToastEventHandler> eventHandler(new ToastEventHandler(peer->id, msgId));
//
//	//hr = toast->add_Activated(eventHandler.Get(), &activatedToken);
//	//if (!SUCCEEDED(hr)) return false;
//
//	//hr = toast->add_Dismissed(eventHandler.Get(), &dismissedToken);
//	//if (!SUCCEEDED(hr)) return false;
//
//	//hr = toast->add_Failed(eventHandler.Get(), &failedToken);
//	//if (!SUCCEEDED(hr)) return false;
//
//	ToastNotifications::iterator i = toastNotifications.find(peer->id);
//	if (i != toastNotifications.cend()) {
//		QMap<MsgId, ToastNotificationPtr>::iterator j = i->find(msgId);
//		if (j != i->cend()) {
//			ComPtr<IToastNotification> notify = j->p;
//			i->erase(j);
//			toastNotifier->Hide(notify.Get());
//			i = toastNotifications.find(peer->id);
//		}
//	}
//	if (i == toastNotifications.cend()) {
//		i = toastNotifications.insert(peer->id, QMap<MsgId, ToastNotificationPtr>());
//	}
//	hr = toastNotifier->Show(toast.Get());
//	if (!SUCCEEDED(hr)) {
//		i = toastNotifications.find(peer->id);
//		if (i != toastNotifications.cend() && i->isEmpty()) toastNotifications.erase(i);
//		return false;
//	}
//	toastNotifications[peer->id].insert(msgId, toast);
//
//	return true;
//}

QString pinnedPath() {
	//static const int maxFileLen = MAX_PATH * 10;
	//WCHAR wstrPath[maxFileLen];
	//if (GetEnvironmentVariable(L"APPDATA", wstrPath, maxFileLen)) {
	//	QDir appData(QString::fromStdWString(std::wstring(wstrPath)));
	//	return appData.absolutePath() + qsl("/Microsoft/Internet Explo7rer/Quick Launch/User Pinned/TaskBar/");
	//}
	return QString();
}

void CheckPinnedAppUserModelId() {
	//if (!propVariantToString) return;

	//static const int maxFileLen = MAX_PATH * 10;

	//HRESULT hr = CoInitialize(0);
	//if (!SUCCEEDED(hr)) return;

	//QString path = pinnedPath();
	//std::wstring p = QDir::toNativeSeparators(path).toStdWString();

	//WCHAR src[MAX_PATH];
	//GetModuleFileName(GetModuleHandle(0), src, MAX_PATH);
	//BY_HANDLE_FILE_INFORMATION srcinfo = { 0 };
	//HANDLE srcfile = CreateFile(src, 0x00, 0x00, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	//if (srcfile == INVALID_HANDLE_VALUE) return;
	//BOOL srcres = GetFileInformationByHandle(srcfile, &srcinfo);
	//CloseHandle(srcfile);
	//if (!srcres) return;
	//LOG(("Checking..."));
	//WIN32_FIND_DATA findData;
	//HANDLE findHandle = FindFirstFileEx((p + L"*").c_str(), FindExInfoStandard, &findData, FindExSearchNameMatch, 0, 0);
	//if (findHandle == INVALID_HANDLE_VALUE) {
	//	LOG(("Init Error: could not find files in pinned folder"));
	//	return;
	//}
	//do {
	//	std::wstring fname = p + findData.cFileName;
	//	LOG(("Checking %1").arg(QString::fromStdWString(fname)));
	//	if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
	//		continue;
	//	} else {
	//		DWORD attributes = GetFileAttributes(fname.c_str());
	//		if (attributes >= 0xFFFFFFF) continue; // file does not exist

	//		ComPtr<IShellLink> shellLink;
	//		HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shellLink));
	//		if (!SUCCEEDED(hr)) continue;

	//		ComPtr<IPersistFile> persistFile;
	//		hr = shellLink.As(&persistFile);
	//		if (!SUCCEEDED(hr)) continue;

	//		hr = persistFile->Load(fname.c_str(), STGM_READWRITE);
	//		if (!SUCCEEDED(hr)) continue;

	//		WCHAR dst[MAX_PATH];
	//		hr = shellLink->GetPath(dst, MAX_PATH, 0, 0);
	//		if (!SUCCEEDED(hr)) continue;

	//		BY_HANDLE_FILE_INFORMATION dstinfo = { 0 };
	//		HANDLE dstfile = CreateFile(dst, 0x00, 0x00, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	//		if (dstfile == INVALID_HANDLE_VALUE) continue;
	//		BOOL dstres = GetFileInformationByHandle(dstfile, &dstinfo);
	//		CloseHandle(dstfile);
	//		if (!dstres) continue;

	//		if (srcinfo.dwVolumeSerialNumber == dstinfo.dwVolumeSerialNumber && srcinfo.nFileIndexLow == dstinfo.nFileIndexLow && srcinfo.nFileIndexHigh == dstinfo.nFileIndexHigh) {
	//			ComPtr<IPropertyStore> propertyStore;
	//			hr = shellLink.As(&propertyStore);
	//			if (!SUCCEEDED(hr)) return;

	//			PROPVARIANT appIdPropVar;
	//			hr = propertyStore->GetValue(pkey_AppUserModel_ID, &appIdPropVar);
	//			if (!SUCCEEDED(hr)) return;
	//			LOG(("Reading..."));
	//			WCHAR already[MAX_PATH];
	//			hr = propVariantToString(appIdPropVar, already, MAX_PATH);
	//			if (SUCCEEDED(hr)) {
	//				if (std::wstring(AppUserModelId()) == already) {
	//					LOG(("Already!"));
	//					PropVariantClear(&appIdPropVar);
	//					return;
	//				}
	//			}
	//			if (appIdPropVar.vt != VT_EMPTY) {
	//				PropVariantClear(&appIdPropVar);
	//				return;
	//			}
	//			PropVariantClear(&appIdPropVar);

	//			hr = InitPropVariantFromString(AppUserModelId(), &appIdPropVar);
	//			if (!SUCCEEDED(hr)) return;

	//			hr = propertyStore->SetValue(pkey_AppUserModel_ID, appIdPropVar);
	//			PropVariantClear(&appIdPropVar);
	//			if (!SUCCEEDED(hr)) return;

	//			hr = propertyStore->Commit();
	//			if (!SUCCEEDED(hr)) return;

	//			if (persistFile->IsDirty() == S_OK) {
	//				persistFile->Save(fname.c_str(), TRUE);
	//			}
	//			return;
	//		}
	//	}
	//} while (FindNextFile(findHandle, &findData));
	//DWORD errorCode = GetLastError();
	//if (errorCode && errorCode != ERROR_NO_MORE_FILES) { // everything is found
	//	LOG(("Init Error: could not find some files in pinned folder"));
	//	return;
	//}
	//FindClose(findHandle);
}

QString systemShortcutPath() {
	//static const int maxFileLen = MAX_PATH * 10;
	//WCHAR wstrPath[maxFileLen];
	//if (GetEnvironmentVariable(L"APPDATA", wstrPath, maxFileLen)) {
	//	QDir appData(QString::fromStdWString(std::wstring(wstrPath)));
	//	return appData.absolutePath() + qsl("/Microsoft/Windows/Start Menu/Programs/");
	//}
	return QString();
}

void CleanupAppUserModelIdShortcut() {
	static const int maxFileLen = MAX_PATH * 10;

	QString path = systemShortcutPath() + qsl("Telegram.lnk");
	std::wstring p = QDir::toNativeSeparators(path).toStdWString();

	//DWORD attributes = GetFileAttributes(p.c_str());
	//if (attributes >= 0xFFFFFFF) return; // file does not exist

	//ComPtr<IShellLink> shellLink;
	//HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shellLink));
	//if (!SUCCEEDED(hr)) return;

	//ComPtr<IPersistFile> persistFile;
	//hr = shellLink.As(&persistFile);
	//if (!SUCCEEDED(hr)) return;

	//hr = persistFile->Load(p.c_str(), STGM_READWRITE);
	//if (!SUCCEEDED(hr)) return;

	//WCHAR szGotPath[MAX_PATH];
	//WIN32_FIND_DATA wfd;
	//hr = shellLink->GetPath(szGotPath, MAX_PATH, (WIN32_FIND_DATA*)&wfd, SLGP_SHORTPATH);
	//if (!SUCCEEDED(hr)) return;

	//if (QDir::toNativeSeparators(cExeDir() + cExeName()).toStdWString() == szGotPath) {
	//	QFile().remove(path);
	//}
}

bool ValidateAppUserModelIdShortcutAt(const QString &path) {
	static const int maxFileLen = MAX_PATH * 10;

	std::wstring p = QDir::toNativeSeparators(path).toStdWString();

	//DWORD attributes = GetFileAttributes(p.c_str());
	//if (attributes >= 0xFFFFFFF) return false; // file does not exist

	//ComPtr<IShellLink> shellLink;
	//HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shellLink));
	//if (!SUCCEEDED(hr)) return false;

	//ComPtr<IPersistFile> persistFile;
	//hr = shellLink.As(&persistFile);
	//if (!SUCCEEDED(hr)) return false;

	//hr = persistFile->Load(p.c_str(), STGM_READWRITE);
	//if (!SUCCEEDED(hr)) return false;

	//ComPtr<IPropertyStore> propertyStore;
	//hr = shellLink.As(&propertyStore);
	//if (!SUCCEEDED(hr)) return false;

	//PROPVARIANT appIdPropVar;
	//hr = propertyStore->GetValue(pkey_AppUserModel_ID, &appIdPropVar);
	//if (!SUCCEEDED(hr)) return false;

	//WCHAR already[MAX_PATH];
	//hr = propVariantToString(appIdPropVar, already, MAX_PATH);
	//if (SUCCEEDED(hr)) {
	//	if (std::wstring(AppUserModelId()) == already) {
	//		PropVariantClear(&appIdPropVar);
	//		return true;
	//	}
	//}
	//if (appIdPropVar.vt != VT_EMPTY) {
	//	PropVariantClear(&appIdPropVar);
	//	return false;
	//}
	//PropVariantClear(&appIdPropVar);

	//hr = InitPropVariantFromString(AppUserModelId(), &appIdPropVar);
	//if (!SUCCEEDED(hr)) return false;

	//hr = propertyStore->SetValue(pkey_AppUserModel_ID, appIdPropVar);
	//PropVariantClear(&appIdPropVar);
	//if (!SUCCEEDED(hr)) return false;

	//hr = propertyStore->Commit();
	//if (!SUCCEEDED(hr)) return false;

	//if (persistFile->IsDirty() == S_OK) {
	//	persistFile->Save(p.c_str(), TRUE);
	//}

	return true;
}

bool ValidateAppUserModelIdShortcut() {
	if (!useToast) return false;

	QString path = systemShortcutPath();
	if (path.isEmpty()) return false;

	if (cBetaVersion()) {
		path += qsl("TelegramBeta.lnk");
		if (ValidateAppUserModelIdShortcutAt(path)) return true;
	} else {
		if (ValidateAppUserModelIdShortcutAt(path + qsl("Telegram Desktop/Telegram.lnk"))) return true;
		if (ValidateAppUserModelIdShortcutAt(path + qsl("Telegram Win (Unofficial)/Telegram.lnk"))) return true;

		path += qsl("Telegram.lnk");
		if (ValidateAppUserModelIdShortcutAt(path)) return true;
	}

	//ComPtr<IShellLink> shellLink;
	//HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shellLink));
	//if (!SUCCEEDED(hr)) return false;

	//hr = shellLink->SetPath(QDir::toNativeSeparators(cExeDir() + cExeName()).toStdWString().c_str());
	//if (!SUCCEEDED(hr)) return false;

	//hr = shellLink->SetArguments(L"");
	//if (!SUCCEEDED(hr)) return false;

	//hr = shellLink->SetWorkingDirectory(QDir::toNativeSeparators(QDir(cWorkingDir()).absolutePath()).toStdWString().c_str());
	//if (!SUCCEEDED(hr)) return false;

	//ComPtr<IPropertyStore> propertyStore;
	//hr = shellLink.As(&propertyStore);
	//if (!SUCCEEDED(hr)) return false;

	//PROPVARIANT appIdPropVar;
	//hr = InitPropVariantFromString(AppUserModelId(), &appIdPropVar);
	//if (!SUCCEEDED(hr)) return false;

	//hr = propertyStore->SetValue(pkey_AppUserModel_ID, appIdPropVar);
	//PropVariantClear(&appIdPropVar);
	//if (!SUCCEEDED(hr)) return false;

	//PROPVARIANT startPinPropVar;
	//hr = InitPropVariantFromUInt32(APPUSERMODEL_STARTPINOPTION_NOPINONINSTALL, &startPinPropVar);
	//if (!SUCCEEDED(hr)) return false;

	//hr = propertyStore->SetValue(pkey_AppUserModel_StartPinOption, startPinPropVar);
	//PropVariantClear(&startPinPropVar);
	//if (!SUCCEEDED(hr)) return false;

	//hr = propertyStore->Commit();
	//if (!SUCCEEDED(hr)) return false;

	//ComPtr<IPersistFile> persistFile;
	//hr = shellLink.As(&persistFile);
	//if (!SUCCEEDED(hr)) return false;

	//hr = persistFile->Save(QDir::toNativeSeparators(path).toStdWString().c_str(), TRUE);
	//if (!SUCCEEDED(hr)) return false;

	return true;
}

bool InitToastManager() {
	if (!useToast || !ValidateAppUserModelIdShortcut()) return false;
	//if (!SUCCEEDED(setCurrentProcessExplicitAppUserModelID(AppUserModelId()))) {
	//	return false;
	//}
	//if (!SUCCEEDED(wrap_GetActivationFactory(StringReferenceWrapper(RuntimeClass_Windows_UI_Notifications_ToastNotificationManager).Get(), &toastNotificationManager))) {
	//	return false;
	//}
	//if (!SUCCEEDED(toastNotificationManager->CreateToastNotifierWithId(StringReferenceWrapper(AppUserModelId(), wcslen(AppUserModelId())).Get(), &toastNotifier))) {
	//	return false;
	//}
	//if (!SUCCEEDED(wrap_GetActivationFactory(StringReferenceWrapper(RuntimeClass_Windows_UI_Notifications_ToastNotification).Get(), &toastNotificationFactory))) {
	//	return false;
	//}
	QDir().mkpath(cWorkingDir() + qsl("tdata/temp"));
	return true;
}

bool psLaunchMaps(const LocationCoords &coords) {
	return QDesktopServices::openUrl(qsl("bingmaps:?lvl=16&collection=point.%1_%2_Point").arg(coords.lat).arg(coords.lon));
}

bool qt_sendSpontaneousEvent(QObject *receiver, QEvent *ev) {
	return QCoreApplication::sendSpontaneousEvent(receiver, ev);
}
