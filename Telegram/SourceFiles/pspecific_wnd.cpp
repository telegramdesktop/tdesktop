
/*
This file is part of Telegram Desktop,
an unofficial desktop messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://tdesktop.com
*/
#include "stdafx.h"
#include "pspecific.h"

#include "lang.h"
#include "application.h"
#include "mainwidget.h"

#include <Shobjidl.h>
#include <dbghelp.h>
#include <shellapi.h>
#include <Strsafe.h>
#include <shlobj.h>
#include <Windowsx.h>

#include <qpa/qplatformnativeinterface.h>

#include <dwmapi.h>

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) < (b) ? (b) : (a))

#ifndef DCX_USESTYLE
#define DCX_USESTYLE 0x00010000
#endif

#ifndef WM_NCPOINTERUPDATE
#define WM_NCPOINTERUPDATE              0x0241
#define WM_NCPOINTERDOWN                0x0242
#define WM_NCPOINTERUP                  0x0243
#endif

#include <gdiplus.h>
#pragma comment (lib,"Gdiplus.lib")
#pragma comment (lib,"Msimg32.lib")

namespace {
	bool frameless = true;
	bool useDWM = false;
	bool useTheme = false;
	bool useOpenAs = false;
	bool themeInited = false;
	bool finished = true;
	int menuShown = 0, menuHidden = 0;
	int dleft = 0, dtop = 0;
	QMargins simpleMargins, margins;
	HICON bigIcon = 0, smallIcon = 0, overlayIcon = 0;

	UINT tbCreatedMsgId = 0;
	ITaskbarList3 *tbListInterface = 0;

	HWND createTaskbarHider() {
		HINSTANCE appinst = (HINSTANCE)GetModuleHandle(0);
		HWND hWnd = 0;
		
		QString cn = QString("TelegramTaskbarHider");
		LPCWSTR _cn = (LPCWSTR)cn.utf16();
		WNDCLASSEX wc;

		wc.cbSize        = sizeof(wc);
		wc.style         = 0;
		wc.lpfnWndProc   = DefWindowProc;
		wc.cbClsExtra    = 0;
		wc.cbWndExtra    = 0;
		wc.hInstance     = appinst;
		wc.hIcon         = 0;
		wc.hCursor       = 0;
		wc.hbrBackground = 0;
		wc.lpszMenuName  = NULL;
		wc.lpszClassName = _cn;
		wc.hIconSm       = 0;
		if (!RegisterClassEx(&wc)) {
			DEBUG_LOG(("Application Error: could not register taskbar hider window class, error: %1").arg(GetLastError()));
			return hWnd;
		}
		
		hWnd = CreateWindowEx(WS_EX_TOOLWINDOW, _cn, 0, WS_POPUP, 0, 0, 0, 0, 0, 0, appinst, 0);
		if (!hWnd) {
			DEBUG_LOG(("Application Error: could not create taskbar hider window class, error: %1").arg(GetLastError()));
			return hWnd;
		}
		return hWnd;
	}

	enum {
		_PsShadowMoved = 0x01,
		_PsShadowResized = 0x02,
		_PsShadowShown = 0x04,
		_PsShadowHidden = 0x08,
		_PsShadowActivate = 0x10,
	};

	enum {
		_PsInitHor = 0x01,
		_PsInitVer = 0x02,
	};

	int32 _psSize = 0;
	class _PsShadowWindows {
	public:

		_PsShadowWindows() : screenDC(0), max_w(0), max_h(0), _x(0), _y(0), _w(0), _h(0), hidden(true), r(0), g(0), b(0), noKeyColor(RGB(255, 255, 255)) {
			for (int i = 0; i < 4; ++i) {
				dcs[i] = 0;
				bitmaps[i] = 0;
				hwnds[i] = 0;
			}
		}
		
		void setColor(QColor c) {
			r = c.red();
			g = c.green();
			b = c.blue();
			
			if (!hwnds[0]) return;
			Gdiplus::SolidBrush brush(Gdiplus::Color(_alphas[0], r, g, b));
			for (int i = 0; i < 4; ++i) {
				Gdiplus::Graphics graphics(dcs[i]);
				graphics.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
				if ((i % 2) && _h || !(i % 2) && _w) {
					graphics.FillRectangle(&brush, 0, 0, (i % 2) ? _size : _w, (i % 2) ? _h : _size);
				}
			}
			initCorners();

			_x = _y = _w = _h = 0;
			update(_PsShadowMoved | _PsShadowResized);
		}

		bool init(QColor c) {
			style::rect topLeft = st::wndShadow;
			_fullsize = topLeft.width();
			_shift = st::wndShadowShift;
			QImage cornersImage(_fullsize, _fullsize, QImage::Format_ARGB32_Premultiplied);
			{
				QPainter p(&cornersImage);
				p.drawPixmap(QPoint(0, 0), App::sprite(), topLeft);
			}
			uchar *bits = cornersImage.bits();
			if (bits) {
				for (
					quint32 *p = (quint32*)bits, *end = (quint32*)(bits + cornersImage.byteCount());
					p < end;
				++p
					) {
					*p = (*p ^ 0x00ffffff) << 24;
				}
			}

			_metaSize = _fullsize + 2 * _shift;
			_alphas.reserve(_metaSize);
			_colors.reserve(_metaSize * _metaSize);
			for (int32 j = 0; j < _metaSize; ++j) {
				for (int32 i = 0; i < _metaSize; ++i) {
					_colors.push_back((i < 2 * _shift || j < 2 * _shift) ? 1 : qMax(BYTE(1), BYTE(cornersImage.pixel(QPoint(i - 2 * _shift, j - 2 * _shift)) >> 24)));
				}
			}
			uchar prev = 0;
			for (int32 i = 0; i < _metaSize; ++i) {
				uchar a = _colors[(_metaSize - 1) * _metaSize + i];
				if (a < prev) break;

				_alphas.push_back(a);
				prev = a;
			}
			_psSize = _size = _alphas.size() - 2 * _shift;

			setColor(c);

			Gdiplus::GdiplusStartupInput gdiplusStartupInput;
			ULONG_PTR gdiplusToken;
			Gdiplus::Status gdiRes = Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
	
			if (gdiRes != Gdiplus::Ok) {
				DEBUG_LOG(("Application Error: could not init GDI+, error: %1").arg((int)gdiRes));
				return false;
			}
			blend.AlphaFormat = AC_SRC_ALPHA;
			blend.SourceConstantAlpha = 255;
			blend.BlendFlags = 0;
			blend.BlendOp = AC_SRC_OVER;

			screenDC = GetDC(0);
			if (!screenDC) return false;

			QRect avail(QDesktopWidget().availableGeometry());
			max_w = avail.width();
			if (max_w < st::wndMinWidth) max_w = st::wndMinWidth;
			max_h = avail.height();
			if (max_h < st::wndMinHeight) max_h = st::wndMinHeight;

			HINSTANCE appinst = (HINSTANCE)GetModuleHandle(0);
		
			for (int i = 0; i < 4; ++i) {
				QString cn = QString("TelegramShadow%1").arg(i);
				LPCWSTR _cn = (LPCWSTR)cn.utf16();
				WNDCLASSEX wc;

				wc.cbSize        = sizeof(wc);
				wc.style         = 0;
				wc.lpfnWndProc   = wndProc;
				wc.cbClsExtra    = 0;
				wc.cbWndExtra    = 0;
				wc.hInstance     = appinst;
				wc.hIcon         = 0;
				wc.hCursor       = 0;
				wc.hbrBackground = 0;
				wc.lpszMenuName  = NULL;
				wc.lpszClassName = _cn;
				wc.hIconSm       = 0;
				if (!RegisterClassEx(&wc)) {
					DEBUG_LOG(("Application Error: could not register shadow window class %1, error: %2").arg(i).arg(GetLastError()));
					destroy();
					return false;
				}
		
				hwnds[i] = CreateWindowEx(WS_EX_LAYERED | WS_EX_TOOLWINDOW, _cn, 0, WS_POPUP, 0, 0, 0, 0, 0, 0, appinst, 0);
				if (!hwnds[i]) {
					DEBUG_LOG(("Application Error: could not create shadow window class %1, error: %2").arg(i).arg(GetLastError()));
					destroy();
					return false;
				}

				dcs[i] = CreateCompatibleDC(screenDC);
				if (!dcs[i]) {
					DEBUG_LOG(("Application Error: could not create dc for shadow window class %1, error: %2").arg(i).arg(GetLastError()));
					destroy();
					return false;
				}

				bitmaps[i] = CreateCompatibleBitmap(screenDC, (i % 2) ? _size : max_w, (i % 2) ? max_h : _size);
				if (!bitmaps[i]) {
					DEBUG_LOG(("Application Error: could not create bitmap for shadow window class %1, error: %2").arg(i).arg(GetLastError()));
					destroy();
					return false;
				}

				SelectObject(dcs[i], bitmaps[i]);
			}

			initCorners();
			return true;
		}

		void initCorners(int directions = (_PsInitHor | _PsInitVer)) {
			bool hor = (directions & _PsInitHor), ver = (directions & _PsInitVer);
			Gdiplus::Graphics graphics0(dcs[0]), graphics1(dcs[1]), graphics2(dcs[2]), graphics3(dcs[3]);
			graphics0.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
			graphics1.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
			graphics2.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
			graphics3.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);

			Gdiplus::SolidBrush brush(Gdiplus::Color(_alphas[0], r, g, b));
			if (hor) graphics0.FillRectangle(&brush, 0, 0, _fullsize - (_size - _shift), 2 * _shift);

			if (ver) {
				graphics1.FillRectangle(&brush, 0, 0, _size, 2 * _shift);
				graphics3.FillRectangle(&brush, 0, 0, _size, 2 * _shift);
				graphics1.FillRectangle(&brush, _size - _shift, 2 * _shift, _shift, _fullsize);
				graphics3.FillRectangle(&brush, 0, 2 * _shift, _shift, _fullsize);
			}

			if (hor) {
				for (int j = 2 * _shift; j < _size; ++j) {
					for (int k = 0; k < _fullsize - (_size - _shift); ++k) {
						brush.SetColor(Gdiplus::Color(_colors[j * _metaSize + k + (_size + _shift)], r, g, b));
						graphics0.FillRectangle(&brush, k, j, 1, 1);
						graphics2.FillRectangle(&brush, k, _size - (j - 2 * _shift) - 1, 1, 1);
					}
				}
				for (int j = _size; j < _size + 2 * _shift; ++j) {
					for (int k = 0; k < _fullsize - (_size - _shift); ++k) {
						brush.SetColor(Gdiplus::Color(_colors[j * _metaSize + k + (_size + _shift)], r, g, b));
						graphics2.FillRectangle(&brush, k, _size - (j - 2 * _shift) - 1, 1, 1);
					}
				}
			}
			if (ver) {
				for (int j = 2 * _shift; j < _fullsize + 2 * _shift; ++j) {
					for (int k = _shift; k < _size; ++k) {
						brush.SetColor(Gdiplus::Color(_colors[j * _metaSize + (k + _shift)], r, g, b));
						graphics1.FillRectangle(&brush, _size - k - 1, j, 1, 1);
						graphics3.FillRectangle(&brush, k, j, 1, 1);
					}
				}
			}
		}
		void verCorners(int h, Gdiplus::Graphics *pgraphics1, Gdiplus::Graphics *pgraphics3) {
			Gdiplus::SolidBrush brush(Gdiplus::Color(_alphas[0], r, g, b));
			pgraphics1->FillRectangle(&brush, _size - _shift, h - _fullsize, _shift, _fullsize);
			pgraphics3->FillRectangle(&brush, 0, h - _fullsize, _shift, _fullsize);
			for (int j = 0; j < _fullsize; ++j) {
				for (int k = _shift; k < _size; ++k) {
					brush.SetColor(Gdiplus::Color(_colors[(j + 2 * _shift) * _metaSize + k + _shift], r, g, b));
					pgraphics1->FillRectangle(&brush, _size - k - 1, h - j - 1, 1, 1);
					pgraphics3->FillRectangle(&brush, k, h - j - 1, 1, 1);
				}
			}
		}
		void horCorners(int w, Gdiplus::Graphics *pgraphics0, Gdiplus::Graphics *pgraphics2) {
			Gdiplus::SolidBrush brush(Gdiplus::Color(_alphas[0], r, g, b));
			pgraphics0->FillRectangle(&brush, w - 2 * _size - (_fullsize - (_size - _shift)), 0, _fullsize - (_size - _shift), 2 * _shift);
			for (int j = 2 * _shift; j < _size; ++j) {
				for (int k = 0; k < _fullsize - (_size - _shift); ++k) {
					brush.SetColor(Gdiplus::Color(_colors[j * _metaSize + k + (_size + _shift)], r, g, b));
					pgraphics0->FillRectangle(&brush, w - 2 * _size - k - 1, j, 1, 1);
					pgraphics2->FillRectangle(&brush, w - 2 * _size - k - 1, _size - (j - 2 * _shift) - 1, 1, 1);
				}
			}
			for (int j = _size; j < _size + 2 * _shift; ++j) {
				for (int k = 0; k < _fullsize - (_size - _shift); ++k) {
					brush.SetColor(Gdiplus::Color(_colors[j * _metaSize + k + (_size + _shift)], r, g, b));
					pgraphics2->FillRectangle(&brush, w - 2 * _size - k - 1, _size - (j - 2 * _shift) - 1, 1, 1);
				}
			}
		}

		void update(int changes, WINDOWPOS *pos = 0) {
			HWND hwnd = Application::wnd() ? Application::wnd()->psHwnd() : 0;
			if (!hwnd || !hwnds[0]) return;

			if (changes == _PsShadowActivate) {
				for (int i = 0; i < 4; ++i) {
					SetWindowPos(hwnds[i], hwnd, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
				}
				return;
			}

			if (changes & _PsShadowHidden) {
				if (!hidden) {
					for (int i = 0; i < 4; ++i) {
						hidden = true;
						ShowWindow(hwnds[i], SW_HIDE);
					}
				}
				return;
			}
			if (!Application::wnd()->psPosInited()) return;

			int x = _x, y = _y, w = _w, h = _h;
			if (pos && (!(pos->flags & SWP_NOMOVE) || !(pos->flags & SWP_NOSIZE) || !(pos->flags & SWP_NOREPOSITION))) {
				if (!(pos->flags & SWP_NOMOVE)) {
					x = pos->x - _size;
					y = pos->y - _size;
				} else if (pos->flags & SWP_NOSIZE) {
					for (int i = 0; i < 4; ++i) {
						SetWindowPos(hwnds[i], hwnd, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
					}
					return;
				}
				if (!(pos->flags & SWP_NOSIZE)) {
					w = pos->cx + 2 * _size;
					h = pos->cy + 2 * _size;
				}
			} else {
				RECT r;
				GetWindowRect(hwnd, &r);
				x = r.left - _size;
				y = r.top - _size;
				w = r.right + _size - x;
				h = r.bottom + _size - y;
			}
			if (h < 2 * _fullsize + 2 * _shift) {
				h = 2 * _fullsize + 2 * _shift;
			}
			if (w < 2 * (_fullsize + _shift)) {
				w = 2 * (_fullsize + _shift);
			}

			if (w != _w) {
				int from = (_w > 2 * (_fullsize + _shift)) ? (_w - _size - _fullsize - _shift) : (_fullsize - (_size - _shift));
				int to = w - _size - _fullsize - _shift;
				if (w > max_w) {
					from = _fullsize - (_size - _shift);
					max_w *= 2;
					for (int i = 0; i < 4; i += 2) {
						DeleteObject(bitmaps[i]); 
						bitmaps[i] = CreateCompatibleBitmap(screenDC, max_w, _size);
						SelectObject(dcs[i], bitmaps[i]);
					}
					initCorners(_PsInitHor);
				}
				Gdiplus::Graphics graphics0(dcs[0]), graphics2(dcs[2]);
				graphics0.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
				graphics2.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
				Gdiplus::SolidBrush brush(Gdiplus::Color(_alphas[0], r, g, b));
				if (to > from) {
					graphics0.FillRectangle(&brush, from, 0, to - from, 2 * _shift);
					for (int i = 2 * _shift; i < _size; ++i) {
						Gdiplus::Pen pen(Gdiplus::Color(_alphas[i], r, g, b));
						graphics0.DrawLine(&pen, from, i, to, i);
						graphics2.DrawLine(&pen, from, _size - (i - 2 * _shift) - 1, to, _size - (i - 2 * _shift) - 1);
					}
					for (int i = _size; i < _size + 2 * _shift; ++i) {
						Gdiplus::Pen pen(Gdiplus::Color(_alphas[i], r, g, b));
						graphics2.DrawLine(&pen, from, _size - (i - 2 * _shift) - 1, to, _size - (i - 2 * _shift) - 1);
					}
				}
				if (_w > w) {
					graphics0.FillRectangle(&brush, w - _size - _fullsize - _shift, 0, _fullsize - (_size - _shift), _size);
					graphics2.FillRectangle(&brush, w - _size - _fullsize - _shift, 0, _fullsize - (_size - _shift), _size);
				}
				horCorners(w, &graphics0, &graphics2);
				POINT p0 = { x + _size, y }, p2 = { x + _size, y + h - _size }, f = { 0, 0 };
				SIZE s = { w - 2 * _size, _size };
				updateWindow(0, &p0, &s);
				updateWindow(2, &p2, &s);
			} else if (x != _x || y != _y) {
				POINT p0 = { x + _size, y }, p2 = { x + _size, y + h - _size };
				updateWindow(0, &p0);
				updateWindow(2, &p2);
			} else if (h != _h) {
				POINT p2 = { x + _size, y + h - _size };
				updateWindow(2, &p2);
			}

			if (h != _h) {
				int from = (_h > 2 * _fullsize + 2 * _shift) ? (_h - _fullsize) : (_fullsize + 2 * _shift);
				int to = h - _fullsize;
				if (h > max_h) {
					from = (_fullsize + 2 * _shift);
					max_h *= 2;
					for (int i = 1; i < 4; i += 2) {
						DeleteObject(bitmaps[i]);
						bitmaps[i] = CreateCompatibleBitmap(dcs[i], _size, max_h);
						SelectObject(dcs[i], bitmaps[i]);
					}
					initCorners(_PsInitVer);
				}
				Gdiplus::Graphics graphics1(dcs[1]), graphics3(dcs[3]);
				graphics1.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
				graphics3.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);

				Gdiplus::SolidBrush brush(Gdiplus::Color(_alphas[0], r, g, b));
				if (to > from) {
					graphics1.FillRectangle(&brush, _size - _shift, from, _shift, to - from);
					graphics3.FillRectangle(&brush, 0, from, _shift, to - from);
					for (int i = 2 * _shift; i < _size + _shift; ++i) {
						Gdiplus::Pen pen(Gdiplus::Color(_alphas[i], r, g, b));
						graphics1.DrawLine(&pen, _size + _shift - i - 1, from, _size + _shift - i - 1, to);
						graphics3.DrawLine(&pen, i - _shift, from, i - _shift, to);
					}
				}
				if (_h > h) {
					graphics1.FillRectangle(&brush, 0, h - _fullsize, _size, _fullsize);
					graphics3.FillRectangle(&brush, 0, h - _fullsize, _size, _fullsize);
				}
				verCorners(h, &graphics1, &graphics3);

				POINT p1 = {x + w - _size, y}, p3 = {x, y}, f = {0, 0};
				SIZE s = { _size, h };
				updateWindow(1, &p1, &s);
				updateWindow(3, &p3, &s);
			} else if (x != _x || y != _y) {
				POINT p1 = { x + w - _size, y }, p3 = { x, y };
				updateWindow(1, &p1);
				updateWindow(3, &p3);
			} else if (w != _w) {
				POINT p1 = { x + w - _size, y };
				updateWindow(1, &p1);
			}
			_x = x;
			_y = y;
			_w = w;
			_h = h;
	
			if (hidden && (changes & _PsShadowShown)) {
				for (int i = 0; i < 4; ++i) {
					SetWindowPos(hwnds[i], hwnd, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);
				}
				hidden = false;
			}
		}

		void updateWindow(int i, POINT *p, SIZE *s = 0) {
			static POINT f = {0, 0};
			if (s) {
				UpdateLayeredWindow(hwnds[i], (s ? screenDC : 0), p, s, (s ? dcs[i] : 0), (s ? (&f) : 0), noKeyColor, &blend, ULW_ALPHA);
			} else {
				SetWindowPos(hwnds[i], 0, p->x, p->y, 0, 0, SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOZORDER);
			}
		}

		void destroy() {
			for (int i = 0; i < 4; ++i) {
				if (dcs[i]) DeleteDC(dcs[i]);
				if (bitmaps[i]) DeleteObject(bitmaps[i]);
				if (hwnds[i]) DestroyWindow(hwnds[i]);
				dcs[i] = 0;
				bitmaps[i] = 0;
				hwnds[i] = 0;
			}
			if (screenDC) ReleaseDC(0, screenDC);
		}

	private:

		int _x, _y, _w, _h;
		int _metaSize, _fullsize, _size, _shift;
		QVector<BYTE> _alphas, _colors;

		bool hidden;

		HWND hwnds[4];
		HDC dcs[4], screenDC;
		HBITMAP bitmaps[4];
		int max_w, max_h;
		BLENDFUNCTION blend;

		BYTE r, g, b;
		COLORREF noKeyColor;

		static LRESULT CALLBACK _PsShadowWindows::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	};
	_PsShadowWindows _psShadowWindows;

	LRESULT CALLBACK _PsShadowWindows::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		if (finished) return DefWindowProc(hwnd, msg, wParam, lParam);

		int i;
		for (i = 0; i < 4; ++i) {
			if (_psShadowWindows.hwnds[i] && hwnd == _psShadowWindows.hwnds[i]) {
				break;
			}
		}
		if (i == 4) return DefWindowProc(hwnd, msg, wParam, lParam);

		switch (msg) {
			case WM_CLOSE:
				Application::wnd()->close();
			break;
			case WM_NCHITTEST: {
				int32 xPos = GET_X_LPARAM(lParam), yPos = GET_Y_LPARAM(lParam);
				switch (i) {
				case 0: return HTTOP;
				case 1: return (yPos < _psShadowWindows._y + _psSize) ? HTTOPRIGHT : ((yPos >= _psShadowWindows._y + _psShadowWindows._h - _psSize) ? HTBOTTOMRIGHT : HTRIGHT);
				case 2: return HTBOTTOM;
				case 3: return (yPos < _psShadowWindows._y + _psSize) ? HTTOPLEFT : ((yPos >= _psShadowWindows._y + _psShadowWindows._h - _psSize) ? HTBOTTOMLEFT : HTLEFT);
				}
				return HTTRANSPARENT;
			} break;

			case WM_NCACTIVATE: return DefWindowProc(hwnd, msg, wParam, lParam);
			case WM_NCLBUTTONDOWN:
			case WM_NCLBUTTONUP:
			case WM_NCLBUTTONDBLCLK:
			case WM_NCMBUTTONDOWN:
			case WM_NCMBUTTONUP:
			case WM_NCMBUTTONDBLCLK:
			case WM_NCRBUTTONDOWN:
			case WM_NCRBUTTONUP:
			case WM_NCRBUTTONDBLCLK:
			case WM_NCXBUTTONDOWN:
			case WM_NCXBUTTONUP:
			case WM_NCXBUTTONDBLCLK:
			case WM_NCMOUSEHOVER:
			case WM_NCMOUSELEAVE:
			case WM_NCMOUSEMOVE:
			case WM_NCPOINTERUPDATE:
			case WM_NCPOINTERDOWN:
			case WM_NCPOINTERUP:
				if (App::wnd() && App::wnd()->psHwnd()) {
					if (msg == WM_NCLBUTTONDOWN) {
						::SetForegroundWindow(App::wnd()->psHwnd());
					}
					LRESULT res = SendMessage(App::wnd()->psHwnd(), msg, wParam, lParam);
					return res;
				}
				return 0;
			break;
			case WM_ACTIVATE:
				if (App::wnd() && App::wnd()->psHwnd() && wParam == WA_ACTIVE) {
					if ((HWND)lParam != App::wnd()->psHwnd()) {
						::SetForegroundWindow(hwnd);
						::SetWindowPos(App::wnd()->psHwnd(), hwnd, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
					}
				}
				return DefWindowProc(hwnd, msg, wParam, lParam);
			break;
			default:
				return DefWindowProc(hwnd, msg, wParam, lParam);
		}
		return 0;
	}

	QColor _shActive(0, 0, 0), _shInactive(0, 0, 0);

	typedef BOOL (FAR STDAPICALLTYPE *f_dwmDefWindowProc)(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, _Out_ LRESULT *plResult);
	f_dwmDefWindowProc dwmDefWindowProc;

	typedef HRESULT (FAR STDAPICALLTYPE *f_dwmSetWindowAttribute)(HWND hWnd, DWORD dwAttribute, _In_ LPCVOID pvAttribute, DWORD cbAttribute);
	f_dwmSetWindowAttribute dwmSetWindowAttribute;
	
	typedef HRESULT (FAR STDAPICALLTYPE *f_dwmExtendFrameIntoClientArea)(HWND hWnd, const MARGINS *pMarInset);
	f_dwmExtendFrameIntoClientArea dwmExtendFrameIntoClientArea;

	typedef HRESULT (FAR STDAPICALLTYPE *f_setWindowTheme)(HWND hWnd, LPCWSTR pszSubAppName, LPCWSTR pszSubIdList);
	f_setWindowTheme setWindowTheme;

	typedef HRESULT (FAR STDAPICALLTYPE *f_openAs_RunDLL)(HWND hWnd, HINSTANCE hInstance, LPCWSTR lpszCmdLine, int nCmdShow);
	f_openAs_RunDLL openAs_RunDLL;

	typedef HRESULT (FAR STDAPICALLTYPE *f_shOpenWithDialog)(HWND hwndParent, const OPENASINFO *poainfo);
	f_shOpenWithDialog shOpenWithDialog;

	template <typename TFunction>
	bool loadFunction(HINSTANCE dll, LPCSTR name, TFunction &func) {
		if (!dll) return false;

		func = (TFunction)GetProcAddress(dll, name);
		return !!func;
	}

	class _PsInitializer {
	public:
		_PsInitializer() {
			setupDWM();
			useDWM = true;
			frameless = !useDWM;

			setupUx();
			setupOpenAs();
		}
		void setupDWM() {
			HINSTANCE procId = LoadLibrary(L"DWMAPI.DLL");

			if (!loadFunction(procId, "DwmDefWindowProc", dwmDefWindowProc)) return;
			if (!loadFunction(procId, "DwmSetWindowAttribute", dwmSetWindowAttribute)) return;
			if (!loadFunction(procId, "DwmExtendFrameIntoClientArea", dwmExtendFrameIntoClientArea)) return;
			useDWM = true;
		}
		void setupUx() {
			HINSTANCE procId = LoadLibrary(L"UXTHEME.DLL");

			if (!loadFunction(procId, "SetWindowTheme", setWindowTheme)) return;
			useTheme = true;
		}
		void setupOpenAs() {
			HINSTANCE procId = LoadLibrary(L"SHELL32.DLL");

			if (!loadFunction(procId, "SHOpenWithDialog", shOpenWithDialog) && !loadFunction(procId, "OpenAs_RunDLLW", openAs_RunDLL)) return;
			useOpenAs = true;
		}
	};
	_PsInitializer _psInitializer;

	class _PsEventFilter : public QAbstractNativeEventFilter {
	public:
		_PsEventFilter() {
		}

		bool nativeEventFilter(const QByteArray &eventType, void *message, long *result) {
			Window *wnd = Application::wnd();
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
			if (tbCreatedMsgId && msg == tbCreatedMsgId) {
				if (CoCreateInstance(CLSID_TaskbarList, NULL, CLSCTX_ALL, IID_ITaskbarList3, (void**)&tbListInterface) != S_OK) {
					tbListInterface = 0;
				}
			}
			switch (msg) {

			case WM_DESTROY: {
				App::quit();
			} return false;

			case WM_ACTIVATE: {
				if (LOWORD(wParam) == WA_CLICKACTIVE) {
					App::wnd()->inactivePress(true);
				}
				Application::wnd()->psUpdateMargins();
				if (LOWORD(wParam) != WA_INACTIVE) {
					_psShadowWindows.setColor(_shActive);
					_psShadowWindows.update(_PsShadowActivate);
				} else {
					_psShadowWindows.setColor(_shInactive);
				}
				QTimer::singleShot(0, Application::wnd(), SLOT(psUpdateCounter()));
				Application::wnd()->update();
			} return false;
				
			case WM_NCPAINT: if (QSysInfo::WindowsVersion >= QSysInfo::WV_WINDOWS8) return false; *result = 0; return true;

			case WM_NCCALCSIZE: if (!useDWM) return false; {
				if (wParam == TRUE) {
					LPNCCALCSIZE_PARAMS params = (LPNCCALCSIZE_PARAMS)lParam;
					params->rgrc[0].left += margins.left() - simpleMargins.left();
					params->rgrc[0].top += margins.top() - simpleMargins.top();
					params->rgrc[0].right -= margins.right() - simpleMargins.right();
					params->rgrc[0].bottom -= margins.bottom() - simpleMargins.bottom();
				} else if (wParam == FALSE) {
					LPRECT rect = (LPRECT)lParam;

					rect->left += margins.left() - simpleMargins.left();
					rect->top += margins.top() - simpleMargins.top();
					rect->right += margins.right() - simpleMargins.right();
					rect->bottom += margins.bottom() - simpleMargins.bottom();
				}
				*result = 0;
			} return true;

			case WM_NCACTIVATE: {
				Application::wnd()->psUpdateMargins();
				*result = LRESULT(TRUE);
				Application::wnd()->repaint();
			} return true;

			case WM_WINDOWPOSCHANGING:
			case WM_WINDOWPOSCHANGED: {
				_psShadowWindows.update(_PsShadowMoved | _PsShadowResized, (WINDOWPOS*)lParam);
			} return false;

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
						int changes = (wParam == SIZE_MINIMIZED || wParam == SIZE_MAXIMIZED) ? _PsShadowHidden : (_PsShadowResized | _PsShadowShown);
						_psShadowWindows.update(changes);
					}
				}
			} return false;

			case WM_SHOWWINDOW: {
				LONG style = GetWindowLong(hWnd, GWL_STYLE);
				int changes = _PsShadowResized | ((wParam && !(style & (WS_MAXIMIZE | WS_MINIMIZE))) ? _PsShadowShown : _PsShadowHidden);
				_psShadowWindows.update(changes);
			} return false;

			case WM_MOVE: {
				_psShadowWindows.update(_PsShadowMoved);
				App::wnd()->psUpdatedPosition();
			} return false;

			case WM_NCHITTEST: {
				POINTS p = MAKEPOINTS(lParam);
				RECT r;
				GetWindowRect(hWnd, &r);
				HitTestType res = Application::wnd()->hitTest(QPoint(p.x - r.left + dleft, p.y - r.top + dtop));
				switch (res) {
					case HitTestClient:
					case HitTestSysButton:   *result = HTCLIENT; break;
					case HitTestIcon:        *result = HTCAPTION; break;
					case HitTestCaption:     *result = HTCAPTION; break;
					case HitTestTop:         *result = HTTOP; break;
					case HitTestTopRight:    *result = HTTOPRIGHT; break;
					case HitTestRight:       *result = HTRIGHT; break;
					case HitTestBottomRight: *result = HTBOTTOMRIGHT; break;
					case HitTestBottom:      *result = HTBOTTOM; break;
					case HitTestBottomLeft:  *result = HTBOTTOMLEFT; break;
					case HitTestLeft:        *result = HTLEFT; break;
					case HitTestTopLeft:     *result = HTTOPLEFT; break;
					case HitTestNone: 
					default:                 *result = HTTRANSPARENT; break;
				};
			} return true;

			case WM_NCRBUTTONUP: {
				SendMessage(hWnd, WM_SYSCOMMAND, SC_MOUSEMENU, lParam);
			} return true;

			case WM_NCLBUTTONDOWN: {
				POINTS p = MAKEPOINTS(lParam);
				RECT r;
				GetWindowRect(hWnd, &r);
				HitTestType res = Application::wnd()->hitTest(QPoint(p.x - r.left + dleft, p.y - r.top + dtop));
				switch (res) {
					case HitTestIcon: 
						if (menuHidden && getms() < menuHidden + 10) {
							menuHidden = 0;
							if (getms() < menuShown + GetDoubleClickTime()) {
								Application::wnd()->close();
							}
						} else {
							QRect icon = Application::wnd()->iconRect();
							p.x = r.left - dleft + icon.left();
							p.y = r.top - dtop + icon.top() + icon.height();
							Application::wnd()->psUpdateSysMenu(Application::wnd()->windowHandle()->windowState());
							menuShown = getms();
							menuHidden = 0;
							TrackPopupMenu(Application::wnd()->psMenu(), TPM_LEFTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON, p.x, p.y, 0, hWnd, 0);
							menuHidden = getms();
						}
					return true;
				};
			} return false;

			case WM_NCLBUTTONDBLCLK: {
				POINTS p = MAKEPOINTS(lParam);
				RECT r;
				GetWindowRect(hWnd, &r);
				HitTestType res = Application::wnd()->hitTest(QPoint(p.x - r.left + dleft, p.y - r.top + dtop));
				switch (res) {
					case HitTestIcon: Application::wnd()->close(); return true;
				};
			} return false;

			case WM_SYSCOMMAND: {
				if (wParam == SC_MOUSEMENU) {
					POINTS p = MAKEPOINTS(lParam);
					Application::wnd()->psUpdateSysMenu(Application::wnd()->windowHandle()->windowState());
					TrackPopupMenu(Application::wnd()->psMenu(), TPM_LEFTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON, p.x, p.y, 0, hWnd, 0);
				}
			} return false;

			case WM_COMMAND: {
				if (HIWORD(wParam)) return false;
				int cmd = LOWORD(wParam);
				switch (cmd) {
					case SC_CLOSE: Application::wnd()->close(); return true;
					case SC_MINIMIZE: Application::wnd()->setWindowState(Qt::WindowMinimized); return true;
					case SC_MAXIMIZE: Application::wnd()->setWindowState(Qt::WindowMaximized); return true;
					case SC_RESTORE: Application::wnd()->setWindowState(Qt::WindowNoState); return true;
				}
			} return true;

			}
			return false;
		}
	};
	_PsEventFilter *_psEventFilter = 0;

};

PsMainWindow::PsMainWindow(QWidget *parent) : QMainWindow(parent), ps_hWnd(0), ps_menu(0), icon256(qsl(":/gui/art/iconround256.png")),
	ps_iconBig(0), ps_iconSmall(0), ps_iconOverlay(0), trayIcon(0), trayIconMenu(0), posInited(false), ps_tbHider_hWnd(createTaskbarHider()), psIdle(false) {
	tbCreatedMsgId = RegisterWindowMessage(L"TaskbarButtonCreated");
	icon16 = icon256.scaledToWidth(16, Qt::SmoothTransformation);
	icon32 = icon256.scaledToWidth(32, Qt::SmoothTransformation);
	connect(&psIdleTimer, SIGNAL(timeout()), this, SLOT(psIdleTimeout()));
	psIdleTimer.setSingleShot(false);
	connect(&notifyWaitTimer, SIGNAL(timeout()), this, SLOT(psNotifyFire()));
	notifyWaitTimer.setSingleShot(true);
}

void PsMainWindow::psNotIdle() const {
	psIdleTimer.stop();
	if (psIdle) {
		psIdle = false;
		if (App::main()) App::main()->setOnline();
		if (App::wnd()) App::wnd()->checkHistoryActivation();
	}
}

void PsMainWindow::psIdleTimeout() {
	LASTINPUTINFO lii;
	lii.cbSize = sizeof(LASTINPUTINFO);
	BOOL res = GetLastInputInfo(&lii);
	if (res) {
		uint64 ticks = GetTickCount();
		if (lii.dwTime >= ticks - IdleMsecs) {
			psNotIdle();
		}
	} else { // error {
		psNotIdle();
	}
}

bool PsMainWindow::psIsActive(int state) const {
	if (state < 0) state = this->windowState();
	return isActiveWindow() && isVisible() && !(state & Qt::WindowMinimized) && !psIdle;
}

bool PsMainWindow::psIsOnline(int windowState) const {
	if (windowState < 0) windowState = this->windowState();
	if (windowState & Qt::WindowMinimized) {
		return false;
	} else if (!isVisible()) {
		return false;
	}
	LASTINPUTINFO lii;
	lii.cbSize = sizeof(LASTINPUTINFO);
	BOOL res = GetLastInputInfo(&lii);
	if (res) {
		uint64 ticks = GetTickCount();
		if (lii.dwTime < ticks - IdleMsecs) {
			if (!psIdle) {
				psIdle = true;
				psIdleTimer.start(900);
			}
			return false;
		} else {
			psNotIdle();
		}
	} else { // error
		psNotIdle();
	}
	return true;
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

void PsMainWindow::psUpdateWorkmode() {
	switch (cWorkMode()) {
	case dbiwmWindowAndTray: {
		setupTrayIcon();
		HWND psOwner = (HWND)GetWindowLong(ps_hWnd, GWL_HWNDPARENT);
		if (psOwner) {
			SetWindowLong(ps_hWnd, GWL_HWNDPARENT, 0);
			psRefreshTaskbarIcon();
		}
	} break;

	case dbiwmTrayOnly: {
		setupTrayIcon();
		HWND psOwner = (HWND)GetWindowLong(ps_hWnd, GWL_HWNDPARENT);
		if (!psOwner) {
			SetWindowLong(ps_hWnd, GWL_HWNDPARENT, (LONG)ps_tbHider_hWnd);
		}
	} break;

	case dbiwmWindowOnly: {
		if (trayIconMenu) trayIconMenu->deleteLater();
		trayIconMenu = 0;
		if (trayIcon) trayIcon->deleteLater();
		trayIcon = 0;

		HWND psOwner = (HWND)GetWindowLong(ps_hWnd, GWL_HWNDPARENT);
		if (psOwner) {
			SetWindowLong(ps_hWnd, GWL_HWNDPARENT, 0);
			psRefreshTaskbarIcon();
		}
	} break;
	}
}

HICON qt_pixmapToWinHICON(const QPixmap &);
static HICON _qt_createHIcon(const QIcon &icon, int xSize, int ySize) {
    if (!icon.isNull()) {
        const QPixmap pm = icon.pixmap(icon.actualSize(QSize(xSize, ySize)));
        if (!pm.isNull())
            return qt_pixmapToWinHICON(pm);
    }
    return 0;
}

void PsMainWindow::psUpdateCounter() {
	int32 counter = App::histories().unreadFull;
	style::color bg = (App::histories().unreadMuted < counter) ? st::counterBG : st::counterMuteBG;
	QIcon icon;
	QImage cicon16(icon16), cicon32(icon32);
	if (counter > 0) {
		{
			QString cnt = (counter < 1000) ? QString("%1").arg(counter) : QString("..%1").arg(counter % 100, 2, 10, QChar('0'));
			QPainter p16(&cicon16);
			p16.setBrush(bg->b);
			p16.setPen(Qt::NoPen);
			p16.setRenderHint(QPainter::Antialiasing);
			int32 fontSize = 8;
			style::font f(fontSize);
			int32 w = f->m.width(cnt), d = 2, r = 3;
			p16.drawRoundedRect(QRect(16 - w - d * 2, 16 - f->height, w + d * 2, f->height), r, r);
			p16.setFont(f->f);

			p16.setPen(st::counterColor->p);

			p16.drawText(16 - w - d, 16 - f->height + f->ascent, cnt);
		}
		if (!tbListInterface) {
			QString cnt = (counter < 10000) ? QString("%1").arg(counter) : ((counter < 1000000) ? QString("%1K").arg(counter / 1000) : QString("%1M").arg(counter / 1000000));
			QPainter p32(&cicon32);
			style::font f(10);
			int32 w = f->m.width(cnt), d = 3, r = 6;
			p32.setBrush(bg->b);
			p32.setPen(Qt::NoPen);
			p32.setRenderHint(QPainter::Antialiasing);
			p32.drawRoundedRect(QRect(32 - w - d * 2, 0, w + d * 2, f->height - 1), r, r);
			p32.setPen(st::counterColor->p);
			p32.setFont(f->f);
			p32.drawText(32 - w - d, f->ascent - 1, cnt);
		}
	}
	icon.addPixmap(QPixmap::fromImage(cicon16));
	icon.addPixmap(QPixmap::fromImage(cicon32));
	if (trayIcon) {
		QIcon ticon;
		QImage ticon16(icon16);
		if (counter > 0) {
			QString cnt = (counter < 1000) ? QString("%1").arg(counter) : QString("..%1").arg(counter % 100, 2, 10, QChar('0'));
			{
				QPainter p16(&ticon16);
				p16.setBrush(bg->b);
				p16.setPen(Qt::NoPen);
				p16.setRenderHint(QPainter::Antialiasing);
				int32 fontSize = 8;
				style::font f(fontSize);
				int32 w = f->m.width(cnt), d = 2, r = 3;
				p16.drawRoundedRect(QRect(16 - w - d * 2, 16 - f->height, w + d * 2, f->height), r, r);
				p16.setFont(f->f);

				p16.setPen(st::counterColor->p);

				p16.drawText(16 - w - d, 16 - f->height + f->ascent, cnt);
			}
		}
		ticon.addPixmap(QPixmap::fromImage(ticon16));
		ticon.addPixmap(QPixmap::fromImage(cicon32));
		trayIcon->setIcon(ticon);
	}

	setWindowTitle((counter > 0) ? qsl("Telegram (%1)").arg(counter) : qsl("Telegram"));
	psDestroyIcons();
    ps_iconSmall = _qt_createHIcon(icon, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
    ps_iconBig = _qt_createHIcon(icon, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
    SendMessage(ps_hWnd, WM_SETICON, 0, (LPARAM)ps_iconSmall);
	SendMessage(ps_hWnd, WM_SETICON, 1, (LPARAM)(ps_iconBig ? ps_iconBig : ps_iconSmall));
	if (tbListInterface) {
		if (counter > 0) {
			QString cnt = (counter < 1000) ? QString("%1").arg(counter) : QString("..%1").arg(counter % 100, 2, 10, QChar('0'));
			QImage oicon16(16, 16, QImage::Format_ARGB32);
			int32 cntSize = cnt.size();
			oicon16.fill(st::transparent->c);
			{
				QPainter p16(&oicon16);
				p16.setBrush(bg->b);
				p16.setPen(Qt::NoPen);
				p16.setRenderHint(QPainter::Antialiasing);
				int32 fontSize = (cntSize < 2) ? 12 : ((cntSize < 3) ? 12 : 8);
				style::font f(fontSize);
				int32 w = f->m.width(cnt), d = (cntSize < 2) ? 5 : ((cntSize < 3) ? 2 : 2), r = (cntSize < 2) ? 8 : ((cntSize < 3) ? 7 : 3);
				p16.drawRoundedRect(QRect(16 - w - d * 2, 16 - f->height, w + d * 2, f->height), r, r);
				p16.setFont(f->f);

				p16.setPen(st::counterColor->p);

				p16.drawText(16 - w - d, 16 - f->height + f->ascent, cnt);
			}
			QIcon oicon(QPixmap::fromImage(oicon16));
			ps_iconOverlay = _qt_createHIcon(oicon, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
		}
		QString description = counter > 0 ? QString("%1 unread messages").arg(counter) : qsl("No unread messages");
		static WCHAR descriptionArr[1024];
		description.toWCharArray(descriptionArr);
		tbListInterface->SetOverlayIcon(ps_hWnd, ps_iconOverlay, descriptionArr);
	}
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
		MONITORINFOEX info;
		info.cbSize = sizeof(info);
		GetMonitorInfo(hMonitor, &info);
		if (dwData == hashCrc32(info.szDevice, sizeof(info.szDevice))) {
			enumMonitor = hMonitor;
			enumMonitorWork = info.rcWork;
			return FALSE;
		}
		return TRUE;
	}
}

void PsMainWindow::psInitSize() {
	setMinimumWidth(st::wndMinWidth);
	setMinimumHeight(st::wndMinHeight);

	TWindowPos pos(cWindowPos());
	if (cDebug()) { // temp while design
		pos.w = 800;
		pos.h = 600;
	}
	QRect avail(QDesktopWidget().availableGeometry());
	bool maximized = false;
	QRect geom(avail.x() + (avail.width() - st::wndDefWidth) / 2, avail.y() + (avail.height() - st::wndDefHeight) / 2, st::wndDefWidth, st::wndDefHeight);
	if (pos.w && pos.h) {
		if (pos.y < 0) pos.y = 0;
		enumMonitor = 0;
		EnumDisplayMonitors(0, 0, &_monitorEnumProc, pos.moncrc);
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

void PsMainWindow::psInitFrameless() {
	psUpdatedPositionTimer.setSingleShot(true);
	connect(&psUpdatedPositionTimer, SIGNAL(timeout()), this, SLOT(psSavePosition()));

	QPlatformNativeInterface *i = QGuiApplication::platformNativeInterface();
    ps_hWnd = static_cast<HWND>(i->nativeResourceForWindow(QByteArrayLiteral("handle"), windowHandle()));

	if (!ps_hWnd) return;

	if (frameless) {
		setWindowFlags(Qt::FramelessWindowHint);
	}

//	RegisterApplicationRestart(NULL, 0);

	psInitSysMenu();
	connect(windowHandle(), SIGNAL(windowStateChanged(Qt::WindowState)), this, SLOT(psStateChanged(Qt::WindowState)));
}

void PsMainWindow::psSavePosition(Qt::WindowState state) {
	if (state == Qt::WindowActive) state = windowHandle()->windowState();
	if (state == Qt::WindowMinimized || !posInited) return;

	TWindowPos pos(cWindowPos()), curPos = pos;

	if (state == Qt::WindowMaximized) {
		curPos.maximized = 1;
	} else {
		RECT w;
		GetWindowRect(ps_hWnd, &w);
		curPos.x = w.left;
		curPos.y = w.top;
		curPos.w = w.right - w.left;
		curPos.h = w.bottom - w.top;
		curPos.maximized = 0;
	}

	HMONITOR hMonitor = MonitorFromWindow(ps_hWnd, MONITOR_DEFAULTTONEAREST);
	if (hMonitor) {
		MONITORINFOEX info;
		info.cbSize = sizeof(info);
		GetMonitorInfo(hMonitor, &info);
		if (!curPos.maximized) {
			curPos.x -= info.rcWork.left;
			curPos.y -= info.rcWork.top;
		}
		curPos.moncrc = hashCrc32(info.szDevice, sizeof(info.szDevice));
	}

	if (curPos.w >= st::wndMinWidth && curPos.h >= st::wndMinHeight) {
		if (curPos.x != pos.x || curPos.y != pos.y || curPos.w != pos.w || curPos.h != pos.h || curPos.moncrc != pos.moncrc || curPos.maximized != pos.maximized) {
			cSetWindowPos(curPos);
			App::writeConfig();
		}
	}
}

void PsMainWindow::psUpdatedPosition() {
	psUpdatedPositionTimer.start(4000);
}

void PsMainWindow::psStateChanged(Qt::WindowState state) {
	psUpdateSysMenu(state);
	psUpdateMargins();
	if (state == Qt::WindowMinimized && GetWindowLong(ps_hWnd, GWL_HWNDPARENT)) {
		minimizeToTray();
	}
	psSavePosition(state);
}

Q_DECLARE_METATYPE(QMargins);
void PsMainWindow::psFirstShow() {
	_psShadowWindows.init(_shActive);
	finished = false;

	psUpdateMargins();

	_psShadowWindows.update(_PsShadowHidden);
	bool showShadows = true;

	show();
	if (cWindowPos().maximized) {
		setWindowState(Qt::WindowMaximized);
	}

	if (cFromAutoStart()) {
		if (cStartMinimized()) {
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
	} else {
		show();
	}
	posInited = true;
	if (showShadows) {
		_psShadowWindows.update(_PsShadowMoved | _PsShadowResized | _PsShadowShown);
	}
}

bool PsMainWindow::psHandleTitle() {
	return useDWM;
}

void PsMainWindow::psInitSysMenu() {
	Qt::WindowStates states = windowState();
	ps_menu = GetSystemMenu(ps_hWnd, FALSE);
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
	int itemCount = GetMenuItemCount(ps_menu);
	for (int i = 0; i < itemCount; ++i) {
		MENUITEMINFO itemInfo = {0};
		itemInfo.cbSize = sizeof(itemInfo);
		itemInfo.fMask = MIIM_TYPE | MIIM_STATE | MIIM_ID;
		if (GetMenuItemInfo(ps_menu, i, TRUE, &itemInfo)) {
			if (itemInfo.fType & MFT_SEPARATOR) {
				continue;
			}
			if (itemInfo.wID && !(itemInfo.fState & MFS_DEFAULT)) {
				UINT fOldState = itemInfo.fState, fState = itemInfo.fState & ~MFS_DISABLED;
				if (itemInfo.wID == SC_CLOSE) {
					fState |= MFS_DEFAULT;
				} else if (itemInfo.wID == menuToDisable || (itemInfo.wID != SC_MINIMIZE && itemInfo.wID != SC_MAXIMIZE && itemInfo.wID != SC_RESTORE)) {
					fState |= MFS_DISABLED;
				}
				itemInfo.fMask = MIIM_STATE;
				itemInfo.fState = fState;
				if (!SetMenuItemInfo(ps_menu, i, TRUE, &itemInfo)) {
					DEBUG_LOG(("PS Error: could not set state %1 to menu item %2, old state %3, error %4").arg(fState).arg(itemInfo.wID).arg(fOldState).arg(GetLastError()));
					DestroyMenu(ps_menu);
					ps_menu = 0;
					break;
				}
			}
		} else {
			DEBUG_LOG(("PS Error: could not get state, menu item %1 of %2, error %3").arg(i).arg(itemCount).arg(GetLastError()));
			DestroyMenu(ps_menu);
			ps_menu = 0;
			break;
		}
	}
}

void PsMainWindow::psUpdateMargins() {
	if (!useDWM) return;

	RECT r, a;

	GetClientRect(ps_hWnd, &r);
	a = r;

	LONG style = GetWindowLong(ps_hWnd, GWL_STYLE), styleEx = GetWindowLong(ps_hWnd, GWL_EXSTYLE);
	AdjustWindowRectEx(&a, style, false, styleEx);
	simpleMargins = QMargins(a.left - r.left, a.top - r.top, r.right - a.right, r.bottom - a.bottom);
	if (style & WS_MAXIMIZE) {
		RECT w, m;
		GetWindowRect(ps_hWnd, &w);
		m = w;
		
		HMONITOR hMonitor = MonitorFromRect(&w, MONITOR_DEFAULTTONEAREST);
		if (hMonitor) {
			MONITORINFO mi;
			mi.cbSize = sizeof(mi);
			GetMonitorInfo(hMonitor, &mi);
			m = mi.rcWork;
		}

		dleft = w.left - m.left;
		dtop = w.top - m.top;

		margins.setLeft(simpleMargins.left() - w.left + m.left);
		margins.setRight(simpleMargins.right() - m.right + w.right);
		margins.setBottom(simpleMargins.bottom() - m.bottom + w.bottom);
		margins.setTop(simpleMargins.top() - w.top + m.top);
	} else {
		margins = simpleMargins;
		dleft = dtop = 0;
	}

	QPlatformNativeInterface *i = QGuiApplication::platformNativeInterface();
	i->setWindowProperty(windowHandle()->handle(), "WindowsCustomMargins", QVariant::fromValue<QMargins>(margins));
	if (!themeInited) {
		themeInited = true;
		if (useTheme) {
			if (QSysInfo::WindowsVersion < QSysInfo::WV_WINDOWS8) {
				setWindowTheme(ps_hWnd, L" ", L" ");
				QApplication::setStyle(QStyleFactory::create("Windows"));
			}
		}
	}
}

void PsMainWindow::psFlash() {
	if (GetForegroundWindow() == ps_hWnd) return;

	FLASHWINFO info;
	info.cbSize = sizeof(info);
	info.hwnd = ps_hWnd;
	info.dwFlags = FLASHW_ALL;
	info.dwTimeout = 0;
	info.uCount = 1;
	FlashWindowEx(&info);
}

HWND PsMainWindow::psHwnd() const {
	return ps_hWnd;
}

HMENU PsMainWindow::psMenu() const {
	return ps_menu;
}

void PsMainWindow::psDestroyIcons() {
	if (ps_iconBig) {
        DestroyIcon(ps_iconBig);
        ps_iconBig = 0;
    }
    if (ps_iconSmall) {
        DestroyIcon(ps_iconSmall);
        ps_iconSmall = 0;
    }
	if (ps_iconOverlay) {
		DestroyIcon(ps_iconOverlay);
		ps_iconOverlay = 0;
	}
}

PsMainWindow::~PsMainWindow() {
	finished = true;
	if (ps_menu) DestroyMenu(ps_menu);
	psDestroyIcons();
	_psShadowWindows.destroy();
	psClearNotifyFast();
	if (ps_tbHider_hWnd) DestroyWindow(ps_tbHider_hWnd);
}

void PsMainWindow::psNotify(History *history, MsgId msgId) {
	if (App::quiting() || !history->notifyFrom) return;

	bool haveSetting = (history->peer->notify != UnknownNotifySettings);
	if (haveSetting) {
		if (history->peer->notify != EmptyNotifySettings && history->peer->notify->mute > unixtime()) {
			history->clearNotifyFrom();
			return;
		}
	} else {
		App::wnd()->getNotifySetting(MTP_inputNotifyPeer(history->peer->input));
	}

	uint64 ms = getms() + NotifyWaitTimeout;
	notifyWhenAlerts[history].insert(ms);
	if (cDesktopNotify()) {
		NotifyWhenMaps::iterator i = notifyWhenMaps.find(history);
		if (i == notifyWhenMaps.end()) {
			i = notifyWhenMaps.insert(history, NotifyWhenMap());
		}
		if (i.value().constFind(msgId) == i.value().cend()) {
			i.value().insert(msgId, ms);
		}
		NotifyWaiters *addTo = haveSetting ? &notifyWaiters : &notifySettingWaiters;
		if (addTo->constFind(history) == addTo->cend()) {
			addTo->insert(history, NotifyWaiter(msgId, ms));
		}
	}
	if (haveSetting) {
		if (!notifyWaitTimer.isActive()) {
			notifyWaitTimer.start(NotifyWaitTimeout);
		}
	}
}

void PsMainWindow::psNotifyFire() {
	psShowNextNotify();
}

void PsMainWindow::psNotifySettingGot() {
	int32 t = unixtime();
	for (NotifyWaiters::iterator i = notifySettingWaiters.begin(); i != notifySettingWaiters.end();) {
		History *history = i.key();
		if (history->peer->notify == UnknownNotifySettings) {
			++i;
		} else {
			if (history->peer->notify == EmptyNotifySettings || history->peer->notify->mute <= t) {
				notifyWaiters.insert(i.key(), i.value());
			}
			i = notifySettingWaiters.erase(i);
		}
	}
	notifyWaitTimer.stop();
	psShowNextNotify();
}

void PsMainWindow::psClearNotify(History *history) {
	if (!history) {
		for (PsNotifyWindows::const_iterator i = notifyWindows.cbegin(), e = notifyWindows.cend(); i != e; ++i) {
			(*i)->unlinkHistory();
		}
		for (NotifyWhenMaps::const_iterator i = notifyWhenMaps.cbegin(), e = notifyWhenMaps.cend(); i != e; ++i) {
			i.key()->clearNotifyFrom();
		}
		notifyWaiters.clear();
		notifySettingWaiters.clear();
		notifyWhenMaps.clear();
		return;
	}
	notifyWaiters.remove(history);
	notifySettingWaiters.remove(history);
	for (PsNotifyWindows::const_iterator i = notifyWindows.cbegin(), e = notifyWindows.cend(); i != e; ++i) {
		(*i)->unlinkHistory(history);
	}
	notifyWhenMaps.remove(history);
	notifyWhenAlerts.remove(history);
}

void PsMainWindow::psClearNotifyFast() {
	notifyWaiters.clear();
	notifySettingWaiters.clear();
	for (PsNotifyWindows::const_iterator i = notifyWindows.cbegin(), e = notifyWindows.cend(); i != e; ++i) {
		(*i)->deleteLater();
	}
	notifyWindows.clear();
	notifyWhenMaps.clear();
	notifyWhenAlerts.clear();
}

// QApplication::desktop()->availableGeometry(App::wnd()) works not very fine, returns not nearest
namespace {
	//RECT _monitorRECT;
	QRect _monitorRect;
	//uint32 _monitorDelta;
	//int32 _wndX, _wndY;
	uint64 _monitorLastGot = 0;

	//BOOL CALLBACK _monitorRectProc(
	//_In_  HMONITOR hMonitor,
	//_In_  HDC hdcMonitor,
	//_In_  LPRECT lprcMonitor,
	//_In_  LPARAM dwData
	//) {
	//	MONITORINFOEX info;
	//	info.cbSize = sizeof(info);
	//	GetMonitorInfo(hMonitor, &info);
	//	int32 centerx = (info.rcWork.right + info.rcWork.left) / 2, centery = (info.rcWork.bottom + info.rcWork.top) / 2;
	//	uint32 delta = (info.rcWork.right > _wndX && info.rcWork.left <= _wndX && info.rcWork.bottom > _wndY && info.rcWork.top <= _wndY) ? 0 : ((centerx - _wndX) * (centerx - _wndX) + (centery - _wndY) * (centery - _wndY));
	//	if (delta < _monitorDelta) {
	//		_monitorDelta = delta;
	//		_monitorRECT = info.rcWork;
	//	}
	//	return !!delta;
	//}
	QRect _desktopRect() {
		uint64 tnow = getms();
		if (tnow > _monitorLastGot + 1000 || tnow < _monitorLastGot) {
			_monitorLastGot = tnow;
			//RECT r;
			//GetWindowRect(App::wnd()->psHwnd(), &r);
			//_wndX = (r.right + r.left) / 2;
			//_wndY = (r.bottom + r.top) / 2;
			//_monitorDelta = INT_MAX;
			//EnumDisplayMonitors(0, 0, &_monitorRectProc, 0);
			//_monitorRect = (_monitorDelta < INT_MAX) ? QRect(_monitorRECT.left, _monitorRECT.top, _monitorRECT.right - _monitorRECT.left, _monitorRECT.bottom - _monitorRECT.top) : QApplication::desktop()->availableGeometry(App::wnd());
			HMONITOR hMonitor = MonitorFromWindow(App::wnd()->psHwnd(), MONITOR_DEFAULTTONEAREST);
			if (hMonitor) {
				MONITORINFOEX info;
				info.cbSize = sizeof(info);
				GetMonitorInfo(hMonitor, &info);
				_monitorRect = QRect(info.rcWork.left, info.rcWork.top, info.rcWork.right - info.rcWork.left, info.rcWork.bottom - info.rcWork.top);
			} else {
				_monitorRect = QApplication::desktop()->availableGeometry(App::wnd());
			}
		}
		return _monitorRect;
	}
}

void PsMainWindow::psShowNextNotify(PsNotifyWindow *remove) {
	if (App::quiting()) return;

	int32 count = NotifyWindows;
	if (remove) {
		for (PsNotifyWindows::iterator i = notifyWindows.begin(), e = notifyWindows.end(); i != e; ++i) {
			if ((*i) == remove) {
				notifyWindows.erase(i);
				break;
			}
		}
	}

	uint64 ms = getms(), nextAlert = 0;
	bool alert = false;
	for (NotifyWhenAlerts::iterator i = notifyWhenAlerts.begin(); i != notifyWhenAlerts.end();) {
		while (!i.value().isEmpty() && *i.value().begin() <= ms) {
			i.value().erase(i.value().begin());
			NotifySettingsPtr n = i.key()->peer->notify;
			if (n == EmptyNotifySettings || n != UnknownNotifySettings && n->mute <= unixtime()) {
				alert = true;
			}
		}
		if (i.value().isEmpty()) {
			i = notifyWhenAlerts.erase(i);
		} else {
			if (!nextAlert || nextAlert > *i.value().begin()) {
				nextAlert = *i.value().begin();
			}
			++i;
		}
	}
	if (alert) {
		psFlash();
		App::playSound();
	}

	for (PsNotifyWindows::const_iterator i = notifyWindows.cbegin(), e = notifyWindows.cend(); i != e; ++i) {
		int32 ind = (*i)->index();
		if (ind < 0) continue;
		--count;
	}
	if (count <= 0 || !cDesktopNotify()) {
		if (nextAlert) {
			notifyWaitTimer.start(nextAlert - ms);
		}
		return;
	}

	QRect r = _desktopRect();
	int32 x = r.x() + r.width() - st::notifyWidth - st::notifyDeltaX, y = r.y() + r.height() - st::notifyHeight - st::notifyDeltaY;
	while (count > 0) {
		uint64 next = 0;
		HistoryItem *notifyItem = 0;
		NotifyWaiters::iterator notifyWaiter;
		for (NotifyWaiters::iterator i = notifyWaiters.begin(); i != notifyWaiters.end(); ++i) {
			History *history = i.key();
			if (history->notifyFrom && history->notifyFrom->id != i.value().msg) {
				NotifyWhenMaps::iterator j = notifyWhenMaps.find(history);
				if (j == notifyWhenMaps.end()) {
					history->clearNotifyFrom();
					i = notifyWaiters.erase(i);
					continue;
				}
				do {
					NotifyWhenMap::const_iterator k = j.value().constFind(history->notifyFrom->id);
					if (k != j.value().cend()) {
						i.value().msg = k.key();
						i.value().when = k.value();
						break;
					}
					history->getNextNotifyFrom();
				} while (history->notifyFrom);
			}
			if (!history->notifyFrom) {
				notifyWhenMaps.remove(history);
				i = notifyWaiters.erase(i);
				continue;
			}
			uint64 when = i.value().when;
			if (!notifyItem || next > when) {
				next = when;
				notifyItem = history->notifyFrom;
				notifyWaiter = i;
			}
		}
		if (notifyItem) {
			if (next > ms) {
				if (nextAlert && nextAlert < next) {
					next = nextAlert;
					nextAlert = 0;
				}
				notifyWaitTimer.start(next - ms);
				break;
			} else {
				notifyWindows.push_back(new PsNotifyWindow(notifyItem, x, y));
				--count;

				uint64 ms = getms();
				History *history = notifyItem->history();
				history->getNextNotifyFrom();
				NotifyWhenMaps::iterator j = notifyWhenMaps.find(history);
				if (j == notifyWhenMaps.end() || !history->notifyFrom) {
					history->clearNotifyFrom();
					notifyWaiters.erase(notifyWaiter);
					if (j != notifyWhenMaps.end()) notifyWhenMaps.erase(j);
					continue;
				}
				j.value().remove(notifyItem->id);
				do {
					NotifyWhenMap::const_iterator k = j.value().constFind(history->notifyFrom->id);
					if (k != j.value().cend()) {
						notifyWaiter.value().msg = k.key();
						notifyWaiter.value().when = k.value();
						break;
					}
					history->getNextNotifyFrom();
				} while (history->notifyFrom);
				if (!history->notifyFrom) {
					notifyWaiters.erase(notifyWaiter);
					notifyWhenMaps.erase(j);
					continue;
				}
			}
		} else {
			break;
		}
	}
	if (nextAlert) {
		notifyWaitTimer.start(nextAlert - ms);
	}

	count = NotifyWindows - count;
	for (PsNotifyWindows::const_iterator i = notifyWindows.cbegin(), e = notifyWindows.cend(); i != e; ++i) {
		int32 ind = (*i)->index();
		if (ind < 0) continue;
		--count;
		(*i)->moveTo(x, y - count * (st::notifyHeight + st::notifyDeltaY));
	}
}

void PsMainWindow::psStopHiding() {
	for (PsNotifyWindows::const_iterator i = notifyWindows.cbegin(), e = notifyWindows.cend(); i != e; ++i) {
		(*i)->stopHiding();
	}
}

void PsMainWindow::psStartHiding() {
	for (PsNotifyWindows::const_iterator i = notifyWindows.cbegin(), e = notifyWindows.cend(); i != e; ++i) {
		(*i)->startHiding();
	}
}

void PsMainWindow::psUpdateNotifies() {
	for (PsNotifyWindows::const_iterator i = notifyWindows.cbegin(), e = notifyWindows.cend(); i != e; ++i) {
		(*i)->updatePeerPhoto();
	}
}

PsNotifyWindow::PsNotifyWindow(HistoryItem *item, int32 x, int32 y) : history(item->history()), aOpacity(0), _index(0), hiding(false), started(GetTickCount()),
	alphaDuration(st::notifyFastAnim), posDuration(st::notifyFastAnim), aY(y + st::notifyHeight + st::notifyDeltaY), close(this, st::notifyClose), aOpacityFunc(st::notifyFastAnimFunc) {

	int32 w = st::notifyWidth, h = st::notifyHeight;
	QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
	img.fill(st::notifyBG->c);

	{
		QPainter p(&img);
		p.setPen(st::notifyBorder->p);
		p.setBrush(Qt::NoBrush);
		p.drawRect(0, 0, w - 1, h - 1);

		if (history->peer->photo->loaded()) {
			p.drawPixmap(st::notifyPhotoPos.x(), st::notifyPhotoPos.y(), history->peer->photo->pix(st::notifyPhotoSize));
		} else {
			MTP::clearLoaderPriorities();
			peerPhoto = history->peer->photo;
			peerPhoto->load(true, true);
		}

		int32 itemWidth = w - st::notifyPhotoPos.x() - st::notifyPhotoSize - st::notifyTextLeft - st::notifyClosePos.x() - st::notifyClose.width;

		QRect rectForName(st::notifyPhotoPos.x() + st::notifyPhotoSize + st::notifyTextLeft, st::notifyTextTop, itemWidth, st::msgNameFont->height);
		if (history->peer->chat) {
			p.drawPixmap(QPoint(rectForName.left() + st::dlgChatImgLeft, rectForName.top() + st::dlgChatImgTop), App::sprite(), st::dlgChatImg);
			rectForName.setLeft(rectForName.left() + st::dlgChatImgSkip);
		}

		QDateTime now(QDateTime::currentDateTime()), lastTime(item->date);
		QDate nowDate(now.date()), lastDate(lastTime.date());
		QString dt = lastTime.toString(qsl("hh:mm"));
		int32 dtWidth = st::dlgHistFont->m.width(dt);
		rectForName.setWidth(rectForName.width() - dtWidth - st::dlgDateSkip);
		p.setFont(st::dlgDateFont->f);
		p.setPen(st::dlgDateColor->p);
		p.drawText(rectForName.left() + rectForName.width() + st::dlgDateSkip, rectForName.top() + st::dlgHistFont->ascent, dt);

		const HistoryItem *textCachedFor = 0;
		Text itemTextCache(itemWidth);
		bool active = false;
		item->drawInDialog(p, QRect(st::notifyPhotoPos.x() + st::notifyPhotoSize + st::notifyTextLeft, st::notifyItemTop + st::msgNameFont->height, itemWidth, 2 * st::dlgFont->height), active, textCachedFor, itemTextCache);

		p.setPen(st::dlgNameColor->p);
		history->nameText.drawElided(p, rectForName.left(), rectForName.top(), rectForName.width());
	}
	pm = QPixmap::fromImage(img);

	hideTimer.setSingleShot(true);
	connect(&hideTimer, SIGNAL(timeout()), this, SLOT(hideByTimer()));

	inputTimer.setSingleShot(true);
	connect(&inputTimer, SIGNAL(timeout()), this, SLOT(checkLastInput()));

	connect(&close, SIGNAL(clicked()), this, SLOT(unlinkHistory()));
	close.setAcceptBoth(true);
	close.move(w - st::notifyClose.width - st::notifyClosePos.x(), st::notifyClosePos.y());
	close.show();

	aY.start(y);
	setGeometry(x, aY.current(), st::notifyWidth, st::notifyHeight);

	aOpacity.start(1);
	setWindowFlags(Qt::Tool | Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint);

	show();

	setWindowOpacity(aOpacity.current());

	alphaDuration = posDuration = st::notifyFastAnim;
	anim::start(this);

	checkLastInput();
}

void PsNotifyWindow::checkLastInput() {
	LASTINPUTINFO lii;
	lii.cbSize = sizeof(LASTINPUTINFO);
	BOOL res = GetLastInputInfo(&lii);
	if (!res || lii.dwTime >= started) {
		hideTimer.start(st::notifyWaitLongHide);
	} else {
		inputTimer.start(300);
	}
}

void PsNotifyWindow::moveTo(int32 x, int32 y, int32 index) {
	if (index >= 0) {
		_index = index;
	}
	move(x, aY.current());
	aY.start(y);
	aOpacity.restart();
	posDuration = st::notifyFastAnim;
	anim::start(this);
}

void PsNotifyWindow::updatePeerPhoto() {
	if (!peerPhoto->isNull() && peerPhoto->loaded()) {
		QImage img(pm.toImage());
		{
			QPainter p(&img);
			p.drawPixmap(st::notifyPhotoPos.x(), st::notifyPhotoPos.y(), peerPhoto->pix(st::notifyPhotoSize));
		}
		peerPhoto = ImagePtr();
		pm = QPixmap::fromImage(img);
		update();
	}
}

void PsNotifyWindow::unlinkHistory(History *hist) {
	if (!hist || hist == history) {
		animHide(st::notifyFastAnim, st::notifyFastAnimFunc);
		history = 0;
		App::wnd()->psShowNextNotify();
	}
}

void PsNotifyWindow::enterEvent(QEvent *e) {
	if (!history) return;
	if (App::wnd()) App::wnd()->psStopHiding();
}

void PsNotifyWindow::leaveEvent(QEvent *e) {
	if (!history) return;
	App::wnd()->psStartHiding();
}

void PsNotifyWindow::startHiding() {
	hideTimer.start(st::notifyWaitShortHide);
}

void PsNotifyWindow::mousePressEvent(QMouseEvent *e) {
	if (!history) return;
	if (e->button() == Qt::RightButton) {
		unlinkHistory();
	} else if (history) {
		App::wnd()->showFromTray();
		App::wnd()->hideSettings();
		App::main()->showPeer(history->peer->id, false, true);
		e->ignore();
	}
}

void PsNotifyWindow::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	p.drawPixmap(0, 0, pm);
}

void PsNotifyWindow::animHide(float64 duration, anim::transition func) {
	if (!history) return;
	alphaDuration = duration;
	aOpacityFunc = func;
	aOpacity.start(0);
	aY.restart();
	hiding = true;
	anim::start(this);
}

void PsNotifyWindow::stopHiding() {
	if (!history) return;
	alphaDuration = st::notifyFastAnim;
	aOpacityFunc = st::notifyFastAnimFunc;
	aOpacity.start(1);
	aY.restart();
	hiding = false;
	hideTimer.stop();
	anim::start(this);
}

void PsNotifyWindow::hideByTimer() {
	if (!history) return;
	animHide(st::notifySlowHide, st::notifySlowHideFunc);
}

bool PsNotifyWindow::animStep(float64 ms) {
	float64 dtAlpha = ms / alphaDuration, dtPos = ms / posDuration;
	if (dtAlpha >= 1) {
		aOpacity.finish();
		if (hiding) {
			deleteLater();
		}
	} else {
		aOpacity.update(dtAlpha, aOpacityFunc);
	}
	setWindowOpacity(aOpacity.current());
	if (dtPos >= 1) {
		aY.finish();
	} else {
		aY.update(dtPos, anim::linear);
	}
	move(x(), aY.current());
	update();
	return (dtAlpha < 1 || !hiding && dtPos < 1);
}

PsNotifyWindow::~PsNotifyWindow() {
	if (App::wnd()) App::wnd()->psShowNextNotify(this);
}

PsApplication::PsApplication(int &argc, char **argv) : QApplication(argc, argv) {
}

void PsApplication::psInstallEventFilter() {
	delete _psEventFilter;
	_psEventFilter = new _PsEventFilter();
	installNativeEventFilter(_psEventFilter);
}

PsApplication::~PsApplication() {
	delete _psEventFilter;
	_psEventFilter = 0;
}

PsUpdateDownloader::PsUpdateDownloader(QThread *thread, const MTPDhelp_appUpdate &update) : already(0), reply(0), full(0) {
	updateUrl = qs(update.vurl);
	moveToThread(thread);
	manager.moveToThread(thread);
	App::setProxySettings(manager);

	connect(thread, SIGNAL(started()), this, SLOT(start()));
	initOutput();
}

PsUpdateDownloader::PsUpdateDownloader(QThread *thread, const QString &url) : already(0), reply(0), full(0) {
	updateUrl = url;
	moveToThread(thread);
	manager.moveToThread(thread);
	App::setProxySettings(manager);

	connect(thread, SIGNAL(started()), this, SLOT(start()));
	initOutput();
}

void PsUpdateDownloader::initOutput() {
	QString fileName;
	QRegularExpressionMatch m = QRegularExpression(qsl("/([^/\\?]+)(\\?|$)")).match(updateUrl);
	if (m.hasMatch()) {
		fileName = m.captured(1).replace(QRegularExpression(qsl("[^a-zA-Z0-9_\\-]")), QString());
	}
	if (fileName.isEmpty()) {
		fileName = qsl("tupdate-%1").arg(rand());
	}
	QString dirStr = cWorkingDir() + qsl("tupdates/");
	fileName = dirStr + fileName;
	QFileInfo file(fileName);

	QDir dir(dirStr);
	if (dir.exists()) {
		QFileInfoList all = dir.entryInfoList(QDir::Files);
		for (QFileInfoList::iterator i = all.begin(), e = all.end(); i != e; ++i) {
			if (i->absoluteFilePath() != file.absoluteFilePath()) {
				QFile::remove(i->absoluteFilePath());
			}
		}
	} else {
		dir.mkdir(dir.absolutePath());
	}
	outputFile.setFileName(fileName);
	if (file.exists()) {
		uint64 fullSize = file.size();
		if (fullSize < INT_MAX) {
			int32 goodSize = (int32)fullSize;
			if (goodSize % UpdateChunk) {
				goodSize = goodSize - (goodSize % UpdateChunk);
				if (goodSize) {
					if (outputFile.open(QIODevice::ReadOnly)) {
						QByteArray goodData = outputFile.readAll().mid(0, goodSize);
						outputFile.close();
						if (outputFile.open(QIODevice::WriteOnly)) {
							outputFile.write(goodData);
							outputFile.close();
							
							QMutexLocker lock(&mutex);
							already = goodSize;
						}
					}
				}
			} else {
				QMutexLocker lock(&mutex);
				already = goodSize;
			}
		}
		if (!already) {
			QFile::remove(fileName);
		}
	}
}

void PsUpdateDownloader::start() {
	sendRequest();
}

void PsUpdateDownloader::sendRequest() {
	QNetworkRequest req(updateUrl);
	QByteArray rangeHeaderValue = "bytes=" + QByteArray::number(already) + "-";// + QByteArray::number(already + cUpdateChunk() - 1); 
	req.setRawHeader("Range", rangeHeaderValue);
	req.setAttribute(QNetworkRequest::HttpPipeliningAllowedAttribute, true);
	if (reply) reply->deleteLater();
	reply = manager.get(req);
	connect(reply, SIGNAL(downloadProgress(qint64,qint64)), this, SLOT(partFinished(qint64,qint64)));
	connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(partFailed(QNetworkReply::NetworkError)));
	connect(reply, SIGNAL(metaDataChanged()), this, SLOT(partMetaGot()));
}

void PsUpdateDownloader::partMetaGot() {
	typedef QList<QNetworkReply::RawHeaderPair> Pairs;
	Pairs pairs = reply->rawHeaderPairs();
	for (Pairs::iterator i = pairs.begin(), e = pairs.end(); i != e; ++i) {
		if (QString::fromUtf8(i->first).toLower() == "content-range") {
			QRegularExpressionMatch m = QRegularExpression(qsl("/(\\d+)([^\\d]|$)")).match(QString::fromUtf8(i->second));
			if (m.hasMatch()) {
				{
					QMutexLocker lock(&mutex);
					full = m.captured(1).toInt();
				}
				emit App::app()->updateDownloading(already, full);
			}
		}
	}
}

int32 PsUpdateDownloader::ready() {
	QMutexLocker lock(&mutex);
	return already;
}

int32 PsUpdateDownloader::size() {
	QMutexLocker lock(&mutex);
	return full;
}

void PsUpdateDownloader::partFinished(qint64 got, qint64 total) {
	if (!reply) return;

	QVariant statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (statusCode.isValid()) {
	    int status = statusCode.toInt();
		if (status != 200 && status != 206 && status != 416) {
			LOG(("Update Error: Bad HTTP status received in partFinished(): %1").arg(status));
			return fatalFail();
		}
	}

	if (!already && !full) {
		QMutexLocker lock(&mutex);
		full = total;
	}
	DEBUG_LOG(("Update Info: part %1 of %2").arg(got).arg(total));

	if (!outputFile.isOpen()) {
		if (!outputFile.open(QIODevice::Append)) {
			LOG(("Update Error: Could not open output file '%1' for appending").arg(outputFile.fileName()));
			return fatalFail();
		}
	}
	QByteArray r = reply->readAll();
	if (!r.isEmpty()) {
		outputFile.write(r);

		QMutexLocker lock(&mutex);
		already += r.size();
	}
	if (got >= total) {
		reply->deleteLater();
		reply = 0;
		outputFile.close();
		unpackUpdate();
	} else {
		emit App::app()->updateDownloading(already, full);
	}
}

void PsUpdateDownloader::partFailed(QNetworkReply::NetworkError e) {
	if (!reply) return;

	QVariant statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
	reply->deleteLater();
	reply = 0;
    if (statusCode.isValid()) {
	    int status = statusCode.toInt();
		if (status == 416) { // Requested range not satisfiable
			outputFile.close();
			unpackUpdate();
			return;
		}
	}
	LOG(("Update Error: failed to download part starting from %1, error %2").arg(already).arg(e));
	emit App::app()->updateFailed();
}

void PsUpdateDownloader::deleteDir(const QString &dir) {
	std::wstring wDir = QDir::toNativeSeparators(dir).toStdWString();
	WCHAR path[4096];
	memcpy(path, wDir.c_str(), (wDir.size() + 1) * sizeof(WCHAR));
	path[wDir.size() + 1] = 0;
	SHFILEOPSTRUCT file_op = {
		NULL,
		FO_DELETE,
		path,
		L"",
		FOF_NOCONFIRMATION |
		FOF_NOERRORUI |
		FOF_SILENT,
		false,
		0,
		L""
	};
	int res = SHFileOperation(&file_op);
}

void PsUpdateDownloader::fatalFail() {
	clearAll();
	emit App::app()->updateFailed();
}

void PsUpdateDownloader::clearAll() {
	deleteDir(cWorkingDir() + qsl("tupdates"));
}

void PsUpdateDownloader::unpackUpdate() {
	QByteArray packed;
	if (!outputFile.open(QIODevice::ReadOnly)) {
		LOG(("Update Error: cant read updates file!"));
		return fatalFail();
	}

	const int32 hSigLen = 128, hShaLen = 20, hPropsLen = LZMA_PROPS_SIZE, hOriginalSizeLen = sizeof(int32), hSize = hSigLen + hShaLen + hPropsLen + hOriginalSizeLen; // header

	QByteArray compressed = outputFile.readAll();
	int32 compressedLen = compressed.size() - hSize;
	if (compressedLen <= 0) {
		LOG(("Update Error: bad compressed size: %1").arg(compressed.size()));
		return fatalFail();
	}
	outputFile.close();

	QString tempDirPath = cWorkingDir() + qsl("tupdates/temp"), readyDirPath = cWorkingDir() + qsl("tupdates/ready");
	deleteDir(tempDirPath);
	deleteDir(readyDirPath);

	QDir tempDir(tempDirPath), readyDir(readyDirPath);
	if (tempDir.exists() || readyDir.exists()) {
		LOG(("Update Error: cant clear tupdates/temp or tupdates/ready dir!"));
		return fatalFail();
	}

	uchar sha1Buffer[20];
	bool goodSha1 = !memcmp(compressed.constData() + hSigLen, hashSha1(compressed.constData() + hSigLen + hShaLen, compressedLen + hPropsLen + hOriginalSizeLen, sha1Buffer), hShaLen);
	if (!goodSha1) {
		LOG(("Update Error: bad SHA1 hash of update file!"));
		return fatalFail();
	}

	RSA *pbKey = PEM_read_bio_RSAPublicKey(BIO_new_mem_buf(const_cast<char*>(UpdatesPublicKey), -1), 0, 0, 0);
	if (!pbKey) {
		LOG(("Update Error: cant read public rsa key!"));
		return fatalFail();
	}
	if (RSA_verify(NID_sha1, (const uchar*)(compressed.constData() + hSigLen), hShaLen, (const uchar*)(compressed.constData()), hSigLen, pbKey) != 1) { // verify signature
		RSA_free(pbKey);
		LOG(("Update Error: bad RSA signature of update file!"));
		return fatalFail();
	}
	RSA_free(pbKey);

	QByteArray uncompressed;

	int32 uncompressedLen;
	memcpy(&uncompressedLen, compressed.constData() + hSigLen + hShaLen + hPropsLen, hOriginalSizeLen);
	uncompressed.resize(uncompressedLen);

	size_t resultLen = uncompressed.size();
	SizeT srcLen = compressedLen;
	int uncompressRes = LzmaUncompress((uchar*)uncompressed.data(), &resultLen, (const uchar*)(compressed.constData() + hSize), &srcLen, (const uchar*)(compressed.constData() + hSigLen + hShaLen), LZMA_PROPS_SIZE);
	if (uncompressRes != SZ_OK) {
		LOG(("Update Error: could not uncompress lzma, code: %1").arg(uncompressRes));
		return fatalFail();
	}

	tempDir.mkdir(tempDir.absolutePath());

	quint32 version;
	{
		QBuffer buffer(&uncompressed);
		buffer.open(QIODevice::ReadOnly);
		QDataStream stream(&buffer);
		stream.setVersion(QDataStream::Qt_5_1);

		stream >> version;
		if (stream.status() != QDataStream::Ok) {
			LOG(("Update Error: cant read version from downloaded stream, status: %1").arg(stream.status()));
			return fatalFail();
		}
		if (version <= AppVersion) {
			LOG(("Update Error: downloaded version %1 is not greater, than mine %2").arg(version).arg(AppVersion));
			return fatalFail();
		}

		quint32 filesCount;
		stream >> filesCount;
		if (stream.status() != QDataStream::Ok) {
			LOG(("Update Error: cant read files count from downloaded stream, status: %1").arg(stream.status()));
			return fatalFail();
		}
		if (!filesCount) {
			LOG(("Update Error: update is empty!"));
			return fatalFail();
		}
		for (int32 i = 0; i < filesCount; ++i) {
			QString relativeName;
			quint32 fileSize;
			QByteArray fileInnerData;

			stream >> relativeName >> fileSize >> fileInnerData;
			if (stream.status() != QDataStream::Ok) {
				LOG(("Update Error: cant read file from downloaded stream, status: %1").arg(stream.status()));
				return fatalFail();
			}
			if (fileSize != fileInnerData.size()) {
				LOG(("Update Error: bad file size %1 not matching data size %2").arg(fileSize).arg(fileInnerData.size()));
				return fatalFail();
			}

			QFile f(tempDirPath + '/' + relativeName);
			if (!f.open(QIODevice::WriteOnly)) {
				LOG(("Update Error: cant open file '%1' for writing").arg(tempDirPath + '/' + relativeName));
				return fatalFail();
			}
			if (f.write(fileInnerData) != fileSize) {
				f.close();
				LOG(("Update Error: cant write file '%1'").arg(tempDirPath + '/' + relativeName));
				return fatalFail();
			}
			f.close();
		}

		// create tdata/version file
		tempDir.mkdir(QDir(tempDirPath + qsl("/tdata")).absolutePath());
		std::wstring versionString = ((version % 1000) ? QString("%1.%2.%3").arg(int(version / 1000000)).arg(int((version % 1000000) / 1000)).arg(int(version % 1000)) : QString("%1.%2").arg(int(version / 1000000)).arg(int((version % 1000000) / 1000))).toStdWString();
		DWORD versionNum = DWORD(version), versionLen = DWORD(versionString.size() * sizeof(WCHAR));
		WCHAR versionStr[32];
		memcpy(versionStr, versionString.c_str(), versionLen);

		QFile fVersion(tempDirPath + qsl("/tdata/version"));		
		if (!fVersion.open(QIODevice::WriteOnly)) {
			LOG(("Update Error: cant write version file '%1'").arg(tempDirPath + qsl("/version")));
			return fatalFail();
		}
		fVersion.write((const char*)&versionNum, sizeof(DWORD));
		fVersion.write((const char*)&versionLen, sizeof(DWORD));
		fVersion.write((const char*)&versionStr[0], versionLen);
		fVersion.close();
	}
	
	if (!tempDir.rename(tempDir.absolutePath(), readyDir.absolutePath())) {
		LOG(("Update Error: cant rename temp dir '%1' to ready dir '%2'").arg(tempDir.absolutePath()).arg(readyDir.absolutePath()));
		return fatalFail();
	}
	deleteDir(tempDirPath);
	outputFile.remove();

	emit App::app()->updateReady();
}

PsUpdateDownloader::~PsUpdateDownloader() {
	delete reply;
	reply = 0;
}

namespace {
	BOOL CALLBACK _ActivateProcess(HWND hWnd, LPARAM lParam) {
		uint64 &processId(*(uint64*)lParam);

		DWORD dwProcessId;
		::GetWindowThreadProcessId(hWnd, &dwProcessId);

		if ((uint64)dwProcessId == processId) { // found top-level window
			static const int32 nameBufSize = 1024;
			WCHAR nameBuf[nameBufSize];
			int32 len = GetWindowText(hWnd, nameBuf, nameBufSize);
			if (len && len < nameBufSize) {
				if (QRegularExpression(qsl("^Telegram(\\s*\\(\\d+\\))?$")).match(QString::fromStdWString(nameBuf)).hasMatch()) {
					BOOL res = ::SetForegroundWindow(hWnd);
					return FALSE;
				}
			}
		}
		return TRUE;
	}
}

void psActivateProcess(uint64 pid) {
	::EnumWindows((WNDENUMPROC)_ActivateProcess, (LPARAM)&pid);
}

QString psCurrentCountry() {
	int chCount = GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SISO3166CTRYNAME, 0, 0);
	if (chCount && chCount < 128) {
		WCHAR wstrCountry[128];
		int len = GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SISO3166CTRYNAME, wstrCountry, chCount);
		return len ? QString::fromStdWString(std::wstring(wstrCountry)) : QString::fromLatin1(DefaultCountry);
	}
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
	int chCount = GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SNAME, 0, 0);
	if (chCount && chCount < 128) {
		WCHAR wstrLocale[128];
		int len = GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SNAME, wstrLocale, chCount);
		if (!len) return QString::fromLatin1(DefaultLanguage);
		QString locale = QString::fromStdWString(std::wstring(wstrLocale));
		QRegularExpressionMatch m = QRegularExpression("(^|[^a-z])([a-z]{2})-").match(locale);
		if (m.hasMatch()) {
			return m.captured(2);
		}
	}
	chCount = GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_ILANGUAGE, 0, 0);
	if (chCount && chCount < 128) {
		WCHAR wstrLocale[128];
		int len = GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_ILANGUAGE, wstrLocale, chCount), lngId = 0;
		if (len < 5) return QString::fromLatin1(DefaultLanguage);

		for (int i = 0; i < 4; ++i) {
			WCHAR ch = wstrLocale[i];
			lngId *= 16;
			if (ch >= WCHAR('0') && ch <= WCHAR('9')) {
				lngId += (ch - WCHAR('0'));
			} else if (ch >= WCHAR('A') && ch <= WCHAR('F')) {
				lngId += (10 + ch - WCHAR('A'));
			} else {
				return QString::fromLatin1(DefaultLanguage);
			}
		}
		return langById(lngId);
	}
	return QString::fromLatin1(DefaultLanguage);
}

QString psAppDataPath() {
	static const int maxFileLen = MAX_PATH * 10;
	WCHAR wstrPath[maxFileLen];
	if (GetEnvironmentVariable(L"APPDATA", wstrPath, maxFileLen)) {
		QDir appData(QString::fromStdWString(std::wstring(wstrPath)));
		return appData.absolutePath() + '/' + QString::fromWCharArray(AppName) + '/';
	}
	return QString();
}

QString psCurrentExeDirectory(int argc, char *argv[]) {
	LPWSTR *args;
	int argsCount;
	args = CommandLineToArgvW(GetCommandLine(), &argsCount);
	if (args) {
		QFileInfo info(QDir::fromNativeSeparators(QString::fromWCharArray(args[0])));
		if (info.isFile()) {
			return info.absoluteDir().absolutePath() + '/';
		}
		LocalFree(args);
	}
	return QString();
}

void psDoCleanup() {
	try {
		psAutoStart(false, true);
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
		static const int bufSize = 4096;
		DWORD checkType, checkSize = bufSize * 2;
		WCHAR checkStr[bufSize];

		QString appId = QString::fromStdWString(AppId);
		QString newKeyStr1 = QString("Software\\Wow6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\%1_is1").arg(appId);
		QString newKeyStr2 = QString("Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\%1_is1").arg(appId);
		QString oldKeyStr1 = QString("SOFTWARE\\Wow6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\%1_is1").arg(appId);
		QString oldKeyStr2 = QString("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\%1_is1").arg(appId);
		HKEY newKey1, newKey2, oldKey1, oldKey2;
		LSTATUS newKeyRes1 = RegOpenKeyEx(HKEY_CURRENT_USER, newKeyStr1.toStdWString().c_str(), 0, KEY_READ, &newKey1);
		LSTATUS newKeyRes2 = RegOpenKeyEx(HKEY_CURRENT_USER, newKeyStr2.toStdWString().c_str(), 0, KEY_READ, &newKey2);
		LSTATUS oldKeyRes1 = RegOpenKeyEx(HKEY_LOCAL_MACHINE, oldKeyStr1.toStdWString().c_str(), 0, KEY_READ, &oldKey1);
		LSTATUS oldKeyRes2 = RegOpenKeyEx(HKEY_LOCAL_MACHINE, oldKeyStr2.toStdWString().c_str(), 0, KEY_READ, &oldKey2);
		
		bool existNew1 = (newKeyRes1 == ERROR_SUCCESS) && (RegQueryValueEx(newKey1, L"InstallDate", 0, &checkType, (BYTE*)checkStr, &checkSize) == ERROR_SUCCESS); checkSize = bufSize * 2;
		bool existNew2 = (newKeyRes2 == ERROR_SUCCESS) && (RegQueryValueEx(newKey2, L"InstallDate", 0, &checkType, (BYTE*)checkStr, &checkSize) == ERROR_SUCCESS); checkSize = bufSize * 2;
		bool existOld1 = (oldKeyRes1 == ERROR_SUCCESS) && (RegQueryValueEx(oldKey1, L"InstallDate", 0, &checkType, (BYTE*)checkStr, &checkSize) == ERROR_SUCCESS); checkSize = bufSize * 2;
		bool existOld2 = (oldKeyRes2 == ERROR_SUCCESS) && (RegQueryValueEx(oldKey2, L"InstallDate", 0, &checkType, (BYTE*)checkStr, &checkSize) == ERROR_SUCCESS); checkSize = bufSize * 2;

		if (newKeyRes1 == ERROR_SUCCESS) RegCloseKey(newKey1);
		if (newKeyRes2 == ERROR_SUCCESS) RegCloseKey(newKey2);
		if (oldKeyRes1 == ERROR_SUCCESS) RegCloseKey(oldKey1);
		if (oldKeyRes2 == ERROR_SUCCESS) RegCloseKey(oldKey2);

		if (existNew1 || existNew2) {
			oldKeyRes1 = existOld1 ? RegDeleteKey(HKEY_LOCAL_MACHINE, oldKeyStr1.toStdWString().c_str()) : ERROR_SUCCESS;
			oldKeyRes2 = existOld2 ? RegDeleteKey(HKEY_LOCAL_MACHINE, oldKeyStr2.toStdWString().c_str()) : ERROR_SUCCESS;
		}

		QString userDesktopLnk, commonDesktopLnk;
		WCHAR userDesktopFolder[MAX_PATH], commonDesktopFolder[MAX_PATH];
		HRESULT userDesktopRes = SHGetFolderPath(0, CSIDL_DESKTOPDIRECTORY, 0, SHGFP_TYPE_CURRENT, userDesktopFolder);
		HRESULT commonDesktopRes = SHGetFolderPath(0, CSIDL_COMMON_DESKTOPDIRECTORY, 0, SHGFP_TYPE_CURRENT, commonDesktopFolder);
		if (SUCCEEDED(userDesktopRes)) {
			userDesktopLnk = QString::fromWCharArray(userDesktopFolder) + "\\Telegram.lnk";
		}
		if (SUCCEEDED(commonDesktopRes)) {
			commonDesktopLnk = QString::fromWCharArray(commonDesktopFolder) + "\\Telegram.lnk";
		}
		QFile userDesktopFile(userDesktopLnk), commonDesktopFile(commonDesktopLnk);
		if (QFile::exists(userDesktopLnk) && QFile::exists(commonDesktopLnk) && userDesktopLnk != commonDesktopLnk) {
			bool removed = QFile::remove(commonDesktopLnk);
		}
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

bool psCheckReadyUpdate() {
	QString readyPath = cWorkingDir() + qsl("tupdates/ready");
	if (!QDir(readyPath).exists()) {
		return false;
	}

	// check ready version
	QString versionPath = readyPath + qsl("/tdata/version");
	{
		QFile fVersion(versionPath);
		if (!fVersion.open(QIODevice::ReadOnly)) {
			LOG(("Update Error: cant read version file '%1'").arg(versionPath));
			PsUpdateDownloader::clearAll();
			return false;
		}
		DWORD versionNum;
		if (fVersion.read((char*)&versionNum, sizeof(DWORD)) != sizeof(DWORD)) {
			LOG(("Update Error: cant read version from file '%1'").arg(versionPath));
			PsUpdateDownloader::clearAll();
			return false;
		}
		fVersion.close();
		if (versionNum <= AppVersion) {
			LOG(("Update Error: cant install version %1 having version %2").arg(versionNum).arg(AppVersion));
			PsUpdateDownloader::clearAll();
			return false;
		}
	}

	QString curUpdater = (cExeDir() + "Updater.exe");
	QFileInfo updater(cWorkingDir() + "tupdates/ready/Updater.exe");
	if (!updater.exists()) {
		QFileInfo current(curUpdater);
		if (!current.exists()) {
			PsUpdateDownloader::clearAll();
			return false;
		}
		if (CopyFile(current.absoluteFilePath().toStdWString().c_str(), updater.absoluteFilePath().toStdWString().c_str(), TRUE) == FALSE) {
			PsUpdateDownloader::clearAll();
			return false;
		}
	}
	if (CopyFile(updater.absoluteFilePath().toStdWString().c_str(), curUpdater.toStdWString().c_str(), FALSE) == FALSE) {
		PsUpdateDownloader::clearAll();
		return false;
	}
	if (DeleteFile(updater.absoluteFilePath().toStdWString().c_str()) == FALSE) {
		PsUpdateDownloader::clearAll();
		return false;
	}
	return true;
}

void psPostprocessFile(const QString &name) {
	std::wstring zoneFile = QDir::toNativeSeparators(name).toStdWString() + L":Zone.Identifier";
	HANDLE f = CreateFile(zoneFile.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
	if (f == INVALID_HANDLE_VALUE) { // :(
		return;
	}

	const char data[] = "[ZoneTransfer]\r\nZoneId=3\r\n";

	DWORD written = 0;
	BOOL result = WriteFile(f, data, sizeof(data), &written, NULL);
	CloseHandle(f);

	if (!result || written != sizeof(data)) { // :(
		return;
	}
}

void psOpenFile(const QString &name, bool openWith) {
	std::wstring wname = QDir::toNativeSeparators(name).toStdWString();

	if (openWith && useOpenAs) {
		if (shOpenWithDialog) {
			OPENASINFO info;
			info.oaifInFlags = OAIF_ALLOW_REGISTRATION | OAIF_REGISTER_EXT | OAIF_EXEC;
			info.pcszClass = NULL;
			info.pcszFile = wname.c_str();
			shOpenWithDialog(0, &info);
		} else {
			openAs_RunDLL(0, 0, wname.c_str(), SW_SHOWNORMAL);
		}
	} else {
		ShellExecute(0, L"open", wname.c_str(), 0, 0, SW_SHOWNORMAL);
	}
}

void psShowInFolder(const QString &name) {
	QString nameEscaped = QDir::toNativeSeparators(name).replace('"', qsl("\"\""));
	ShellExecute(0, 0, qsl("explorer").toStdWString().c_str(), (qsl("/select,") + nameEscaped).toStdWString().c_str(), 0, SW_SHOWNORMAL);
}

void psFinish() {
}

void psExecUpdater() {
	QString targs = qsl("-update");
	if (cFromAutoStart()) targs += qsl(" -autostart");
	if (cDebug()) targs += qsl(" -debug");

	QString updater(QDir::toNativeSeparators(cExeDir() + "Updater.exe")), wdir(QDir::toNativeSeparators(cWorkingDir()));

	DEBUG_LOG(("Application Info: executing %1 %2").arg(cExeDir() + "Updater.exe").arg(targs));
	HINSTANCE r = ShellExecute(0, 0, updater.toStdWString().c_str(), targs.toStdWString().c_str(), wdir.isEmpty() ? 0 : wdir.toStdWString().c_str(), SW_SHOWNORMAL);
	if (long(r) < 32) {
		DEBUG_LOG(("Application Error: failed to execute %1, working directory: '%2', result: %3").arg(updater).arg(wdir).arg(long(r)));
		QString readyPath = cWorkingDir() + qsl("tupdates/ready");
		PsUpdateDownloader::deleteDir(readyPath);
	}
}

void psExecTelegram() {
	QString targs = qsl("-noupdate -tosettings");
	if (cFromAutoStart()) targs += qsl(" -autostart");
	if (cDebug()) targs += qsl(" -debug");
	if (cDataFile() != (cTestMode() ? qsl("data_test") : qsl("data"))) targs += qsl(" -key \"") + cDataFile() + '"';

	QString telegram(QDir::toNativeSeparators(cExeDir() + "Telegram.exe")), wdir(QDir::toNativeSeparators(cWorkingDir()));

	DEBUG_LOG(("Application Info: executing %1 %2").arg(cExeDir() + "Telegram.exe").arg(targs));
	HINSTANCE r = ShellExecute(0, 0, telegram.toStdWString().c_str(), targs.toStdWString().c_str(), wdir.isEmpty() ? 0 : wdir.toStdWString().c_str(), SW_SHOWNORMAL);
	if (long(r) < 32) {
		DEBUG_LOG(("Application Error: failed to execute %1, working directory: '%2', result: %3").arg(telegram).arg(wdir).arg(long(r)));
	}
}

void psAutoStart(bool start, bool silent) {
	WCHAR startupFolder[MAX_PATH];
	HRESULT hres = SHGetFolderPath(0, CSIDL_STARTUP, 0, SHGFP_TYPE_CURRENT, startupFolder);
	if (SUCCEEDED(hres)) {
		QString lnk = QString::fromWCharArray(startupFolder) + "\\Telegram.lnk"; 
		if (start) {
			IShellLink* psl; 
			hres = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (LPVOID*)&psl); 
			if (SUCCEEDED(hres)) { 
				IPersistFile* ppf; 
 
				QString exe = QDir::toNativeSeparators(QDir(cExeDir()).absolutePath() + "//Telegram.exe"), dir = QDir::toNativeSeparators(QDir(cWorkingDir()).absolutePath());
				psl->SetArguments(L"-autostart");
				psl->SetPath(exe.toStdWString().c_str());
				psl->SetWorkingDirectory(dir.toStdWString().c_str());
				psl->SetDescription(L"Telegram autorun link.\nYou can disable autorun in Telegram settings."); 
 
				hres = psl->QueryInterface(IID_IPersistFile, (LPVOID*)&ppf); 
 
				if (SUCCEEDED(hres)) { 
					hres = ppf->Save(lnk.toStdWString().c_str(), TRUE); 
					ppf->Release(); 
				}  else {
					if (!silent) LOG(("App Error: could not create interface IID_IPersistFile %1").arg(hres));
				}
				psl->Release(); 
			} else {
				if (!silent) LOG(("App Error: could not create instance of IID_IShellLink %1").arg(hres));
			}
		} else {
			QFile::remove(lnk);
		}
	} else {
		if (!silent) LOG(("App Error: could not get CSIDL_STARTUP folder %1").arg(hres));
	}
}

#ifdef _NEED_WIN_GENERATE_DUMP
static const WCHAR *_programName = L"Telegram Win (Unofficial)"; // folder in APPDATA, if current path is unavailable for writing
static const WCHAR *_exeName = L"Telegram.exe";

LPTOP_LEVEL_EXCEPTION_FILTER _oldWndExceptionFilter = 0;

typedef BOOL (FAR STDAPICALLTYPE *t_miniDumpWriteDump)(
    _In_ HANDLE hProcess,
    _In_ DWORD ProcessId,
    _In_ HANDLE hFile,
    _In_ MINIDUMP_TYPE DumpType,
    _In_opt_ PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam,
    _In_opt_ PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
    _In_opt_ PMINIDUMP_CALLBACK_INFORMATION CallbackParam
);
t_miniDumpWriteDump miniDumpWriteDump = 0;

HANDLE _generateDumpFileAtPath(const WCHAR *path) {
	static const int maxFileLen = MAX_PATH * 10;

	WCHAR szPath[maxFileLen];
	wsprintf(szPath, L"%stdumps\\", path);

    if (!CreateDirectory(szPath, NULL)) {
		DWORD errCode = GetLastError();
		if (errCode && errCode != ERROR_ALREADY_EXISTS) {
			return 0;
		}
	}

    WCHAR szFileName[maxFileLen];
	WCHAR szExeName[maxFileLen];

	wcscpy_s(szExeName, _exeName);
	WCHAR *dotFrom = wcschr(szExeName, WCHAR(L'.'));
	if (dotFrom) {
		wsprintf(dotFrom, L"");
	}

    SYSTEMTIME stLocalTime;

    GetLocalTime(&stLocalTime);

    wsprintf(szFileName, L"%s%s-%s-%04d%02d%02d-%02d%02d%02d-%ld-%ld.dmp", 
             szPath, szExeName, AppVersionStr, 
             stLocalTime.wYear, stLocalTime.wMonth, stLocalTime.wDay, 
             stLocalTime.wHour, stLocalTime.wMinute, stLocalTime.wSecond, 
             GetCurrentProcessId(), GetCurrentThreadId());
    return CreateFile(szFileName, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_WRITE|FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);
}

void _generateDump(EXCEPTION_POINTERS* pExceptionPointers) {
	static const int maxFileLen = MAX_PATH * 10;

	HMODULE hDll = LoadLibrary(L"DBGHELP.DLL");
	if (!hDll) return;

	miniDumpWriteDump = (t_miniDumpWriteDump)GetProcAddress(hDll, "MiniDumpWriteDump");
	if (!miniDumpWriteDump) return;

	HANDLE hDumpFile = 0;

	WCHAR szPath[maxFileLen];
	DWORD len = GetModuleFileName(GetModuleHandle(0), szPath, maxFileLen);
	if (!len) return;

	WCHAR *pathEnd = szPath  + len;

	if (!_wcsicmp(pathEnd - wcslen(_exeName), _exeName)) {
		wsprintf(pathEnd - wcslen(_exeName), L"");
		hDumpFile = _generateDumpFileAtPath(szPath);
	}
	if (!hDumpFile || hDumpFile == INVALID_HANDLE_VALUE) {
		WCHAR wstrPath[maxFileLen];
		DWORD wstrPathLen;
		if (wstrPathLen = GetEnvironmentVariable(L"APPDATA", wstrPath, maxFileLen)) {
			wsprintf(wstrPath + wstrPathLen, L"\\%s\\", _programName);
			hDumpFile = _generateDumpFileAtPath(wstrPath);
		}
	}

	if (!hDumpFile || hDumpFile == INVALID_HANDLE_VALUE) {
		return;
	}

	MINIDUMP_EXCEPTION_INFORMATION ExpParam = {0};
    ExpParam.ThreadId = GetCurrentThreadId();
    ExpParam.ExceptionPointers = pExceptionPointers;
    ExpParam.ClientPointers = TRUE;

    miniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hDumpFile, MiniDumpWithDataSegs, &ExpParam, NULL, NULL);
}

LONG CALLBACK _exceptionFilter(EXCEPTION_POINTERS* pExceptionPointers) {
	_generateDump(pExceptionPointers);
    return _oldWndExceptionFilter ? (*_oldWndExceptionFilter)(pExceptionPointers) : EXCEPTION_CONTINUE_SEARCH;
}
#endif
