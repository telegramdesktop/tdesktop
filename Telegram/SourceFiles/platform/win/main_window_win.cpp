/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/win/main_window_win.h"

#include "styles/style_window.h"
#include "platform/platform_notifications_manager.h"
#include "platform/win/windows_dlls.h"
#include "platform/win/windows_event_filter.h"
#include "window/notifications_manager.h"
#include "mainwindow.h"
#include "base/crc32hash.h"
#include "core/application.h"
#include "lang/lang_keys.h"
#include "storage/localstorage.h"
#include "ui/widgets/popup_menu.h"
#include "window/themes/window_theme.h"
#include "history/history.h"
#include "app.h"

#include <QtWidgets/QDesktopWidget>
#include <QtWidgets/QStyleFactory>
#include <QtWidgets/QApplication>
#include <QtGui/QWindow>
#include <qpa/qplatformnativeinterface.h>

#include <Shobjidl.h>
#include <shellapi.h>
#include <WtsApi32.h>

#include <roapi.h>
#include <wrl/client.h>

#include <Windowsx.h>
#include <VersionHelpers.h>

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) < (b) ? (b) : (a))
#include <gdiplus.h>
#undef min
#undef max

// WM_POINTER support from Windows 8 onwards (WINVER >= 0x0602)
#ifndef WM_POINTERUPDATE
#  define WM_NCPOINTERUPDATE 0x0241
#  define WM_NCPOINTERDOWN   0x0242
#  define WM_NCPOINTERUP     0x0243
#  define WM_POINTERUPDATE   0x0245
#  define WM_POINTERDOWN     0x0246
#  define WM_POINTERUP       0x0247
#  define WM_POINTERENTER    0x0249
#  define WM_POINTERLEAVE    0x024A
#  define WM_POINTERACTIVATE 0x024B
#  define WM_POINTERCAPTURECHANGED 0x024C
#  define WM_POINTERWHEEL    0x024E
#  define WM_POINTERHWHEEL   0x024F
#endif // WM_POINTERUPDATE

HICON qt_pixmapToWinHICON(const QPixmap &);

using namespace Microsoft::WRL;

Q_DECLARE_METATYPE(QMargins);

namespace Platform {
namespace {

HICON createHIconFromQIcon(const QIcon &icon, int xSize, int ySize) {
	if (!icon.isNull()) {
		const QPixmap pm = icon.pixmap(icon.actualSize(QSize(xSize, ySize)));
		if (!pm.isNull()) {
			return qt_pixmapToWinHICON(pm);
		}
	}
	return nullptr;
}

HWND createTaskbarHider() {
	HINSTANCE appinst = (HINSTANCE)GetModuleHandle(0);
	HWND hWnd = 0;

	QString cn = QString("TelegramTaskbarHider");
	LPCWSTR _cn = (LPCWSTR)cn.utf16();
	WNDCLASSEX wc;

	wc.cbSize = sizeof(wc);
	wc.style = 0;
	wc.lpfnWndProc = DefWindowProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = appinst;
	wc.hIcon = 0;
	wc.hCursor = 0;
	wc.hbrBackground = 0;
	wc.lpszMenuName = NULL;
	wc.lpszClassName = _cn;
	wc.hIconSm = 0;
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
	_PsInitHor = 0x01,
	_PsInitVer = 0x02,
};

int32 _psSize = 0;
class _PsShadowWindows {
public:

	using Change = MainWindow::ShadowsChange;
	using Changes = MainWindow::ShadowsChanges;

	_PsShadowWindows() : screenDC(0), noKeyColor(RGB(255, 255, 255)) {
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
		update(Change::Moved | Change::Resized);
	}

	bool init(not_null<MainWindow*> window, QColor c) {
		_window = window;
		_fullsize = st::windowShadow.width();
		_shift = st::windowShadowShift;
		auto cornersImage = QImage(_fullsize, _fullsize, QImage::Format_ARGB32_Premultiplied);
		{
			Painter p(&cornersImage);
			p.setCompositionMode(QPainter::CompositionMode_Source);
			st::windowShadow.paint(p, 0, 0, _fullsize, QColor(0, 0, 0));
		}
		if (rtl()) cornersImage = cornersImage.mirrored(true, false);

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
			LOG(("Application Error: could not init GDI+, error: %1").arg((int)gdiRes));
			return false;
		}
		blend.AlphaFormat = AC_SRC_ALPHA;
		blend.SourceConstantAlpha = 255;
		blend.BlendFlags = 0;
		blend.BlendOp = AC_SRC_OVER;

		screenDC = GetDC(0);
		if (!screenDC) {
			LOG(("Application Error: could not GetDC(0), error: %2").arg(GetLastError()));
			return false;
		}

		const auto avail = QApplication::desktop()->availableGeometry();
		max_w = avail.width();
		accumulate_max(max_w, st::windowMinWidth);
		max_h = avail.height();
		accumulate_max(max_h, st::defaultWindowTitle.height + st::windowMinHeight);

		HINSTANCE appinst = (HINSTANCE)GetModuleHandle(0);
		HWND hwnd = _window ? _window->psHwnd() : nullptr;

		for (int i = 0; i < 4; ++i) {
			QString cn = QString("TelegramShadow%1").arg(i);
			LPCWSTR _cn = (LPCWSTR)cn.utf16();
			WNDCLASSEX wc;

			wc.cbSize = sizeof(wc);
			wc.style = 0;
			wc.lpfnWndProc = wndProc;
			wc.cbClsExtra = 0;
			wc.cbWndExtra = 0;
			wc.hInstance = appinst;
			wc.hIcon = 0;
			wc.hCursor = 0;
			wc.hbrBackground = 0;
			wc.lpszMenuName = NULL;
			wc.lpszClassName = _cn;
			wc.hIconSm = 0;
			if (!RegisterClassEx(&wc)) {
				LOG(("Application Error: could not register shadow window class %1, error: %2").arg(i).arg(GetLastError()));
				destroy();
				return false;
			}

			hwnds[i] = CreateWindowEx(WS_EX_LAYERED | WS_EX_TOOLWINDOW, _cn, 0, WS_POPUP, 0, 0, 0, 0, 0, 0, appinst, 0);
			if (!hwnds[i]) {
				LOG(("Application Error: could not create shadow window class %1, error: %2").arg(i).arg(GetLastError()));
				destroy();
				return false;
			}
			SetWindowLongPtr(hwnds[i], GWLP_HWNDPARENT, (LONG)hwnd);

			dcs[i] = CreateCompatibleDC(screenDC);
			if (!dcs[i]) {
				LOG(("Application Error: could not create dc for shadow window class %1, error: %2").arg(i).arg(GetLastError()));
				destroy();
				return false;
			}

			bitmaps[i] = CreateCompatibleBitmap(screenDC, (i % 2) ? _size : max_w, (i % 2) ? max_h : _size);
			if (!bitmaps[i]) {
				LOG(("Application Error: could not create bitmap for shadow window class %1, error: %2").arg(i).arg(GetLastError()));
				destroy();
				return false;
			}

			SelectObject(dcs[i], bitmaps[i]);
		}

		QStringList alphasForLog;
		for_const (auto alpha, _alphas) {
			alphasForLog.append(QString::number(alpha));
		}
		LOG(("Window Shadow: %1").arg(alphasForLog.join(", ")));

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

	void update(Changes changes, WINDOWPOS *pos = 0) {
		HWND hwnd = _window ? _window->psHwnd() : 0;
		if (!hwnd || !hwnds[0]) return;

		if (changes == Changes(Change::Activate)) {
			for (int i = 0; i < 4; ++i) {
				SetWindowPos(hwnds[i], hwnd, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
			}
			return;
		}

		if (changes & Change::Hidden) {
			if (!hidden) {
				for (int i = 0; i < 4; ++i) {
					hidden = true;
					ShowWindow(hwnds[i], SW_HIDE);
				}
			}
			return;
		}
		if (!_window->positionInited()) return;

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

			POINT p1 = { x + w - _size, y }, p3 = { x, y }, f = { 0, 0 };
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

		if (hidden && (changes & Change::Shown)) {
			for (int i = 0; i < 4; ++i) {
				SetWindowPos(hwnds[i], hwnd, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);
			}
			hidden = false;
		}
	}

	void updateWindow(int i, POINT *p, SIZE *s = 0) {
		static POINT f = { 0, 0 };
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

	MainWindow *window() const {
		return _window;
	}

private:

	int _x = 0, _y = 0, _w = 0, _h = 0;
	int _metaSize = 0, _fullsize = 0, _size = 0, _shift = 0;
	QVector<BYTE> _alphas, _colors;

	MainWindow *_window = nullptr;
	bool hidden = true;

	HWND hwnds[4];
	HDC dcs[4], screenDC;
	HBITMAP bitmaps[4];
	int max_w = 0, max_h = 0;
	BLENDFUNCTION blend;

	BYTE r = 0, g = 0, b = 0;
	COLORREF noKeyColor;

	static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

};
_PsShadowWindows _psShadowWindows;

LRESULT CALLBACK _PsShadowWindows::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	const auto window = _psShadowWindows.window();
	if (!window || !window->shadowsWorking()) {
		return DefWindowProc(hwnd, msg, wParam, lParam);
	}

	int i;
	for (i = 0; i < 4; ++i) {
		if (_psShadowWindows.hwnds[i] && hwnd == _psShadowWindows.hwnds[i]) {
			break;
		}
	}
	if (i == 4) return DefWindowProc(hwnd, msg, wParam, lParam);

	switch (msg) {
	case WM_CLOSE:
		window->close();
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
	if (window && window->psHwnd()) {
		if (msg == WM_NCLBUTTONDOWN) {
			::SetForegroundWindow(window->psHwnd());
		}
		LRESULT res = SendMessage(window->psHwnd(), msg, wParam, lParam);
		return res;
	}
	return 0;
	break;
	case WM_ACTIVATE:
	if (window && window->psHwnd() && wParam == WA_ACTIVE) {
		if ((HWND)lParam != window->psHwnd()) {
			::SetForegroundWindow(hwnd);
			::SetWindowPos(window->psHwnd(), hwnd, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		}
	}
	return DefWindowProc(hwnd, msg, wParam, lParam);
	break;
	default:
	return DefWindowProc(hwnd, msg, wParam, lParam);
	}
	return 0;
}

ComPtr<ITaskbarList3> taskbarList;

bool handleSessionNotification = false;

} // namespace

UINT MainWindow::_taskbarCreatedMsgId = 0;

MainWindow::MainWindow(not_null<Window::Controller*> controller)
: Window::MainWindow(controller)
, ps_tbHider_hWnd(createTaskbarHider()) {
	QCoreApplication::instance()->installNativeEventFilter(
		EventFilter::CreateInstance(this));

	if (!_taskbarCreatedMsgId) {
		_taskbarCreatedMsgId = RegisterWindowMessage(L"TaskbarButtonCreated");
	}
	subscribe(Window::Theme::Background(), [this](const Window::Theme::BackgroundUpdate &update) {
		if (update.paletteChanged()) {
			_psShadowWindows.setColor(st::windowShadowFg->c);
		}
	});
}

void MainWindow::TaskbarCreated() {
	HRESULT hr = CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&taskbarList));
	if (!SUCCEEDED(hr)) {
		taskbarList.Reset();
	}
}

void MainWindow::shadowsUpdate(ShadowsChanges changes, WINDOWPOS *position) {
	_psShadowWindows.update(changes, position);
}

void MainWindow::shadowsActivate() {
//	_psShadowWindows.setColor(_shActive);
	shadowsUpdate(ShadowsChange::Activate);
}

void MainWindow::shadowsDeactivate() {
//	_psShadowWindows.setColor(_shInactive);
}

void MainWindow::psShowTrayMenu() {
	trayIconMenu->popup(QCursor::pos());
}

int32 MainWindow::screenNameChecksum(const QString &name) const {
	constexpr int DeviceNameSize = base::array_size(MONITORINFOEX().szDevice);
	wchar_t buffer[DeviceNameSize] = { 0 };
	if (name.size() < DeviceNameSize) {
		name.toWCharArray(buffer);
	} else {
		memcpy(buffer, name.toStdWString().data(), sizeof(buffer));
	}
	return base::crc32(buffer, sizeof(buffer));
}

void MainWindow::psRefreshTaskbarIcon() {
	const auto refresher = std::make_unique<QWidget>(this);
	refresher->setWindowFlags(static_cast<Qt::WindowFlags>(Qt::Tool) | Qt::FramelessWindowHint);
	refresher->setGeometry(x() + 1, y() + 1, 1, 1);
	auto palette = refresher->palette();
	palette.setColor(QPalette::Background, (isActiveWindow() ? st::titleBgActive : st::titleBg)->c);
	refresher->setPalette(palette);
	refresher->show();
	refresher->activateWindow();

	updateIconCounters();
}

void MainWindow::psTrayMenuUpdated() {
}

void MainWindow::psSetupTrayIcon() {
	if (!trayIcon) {
		trayIcon = new QSystemTrayIcon(this);

		auto icon = QIcon(App::pixmapFromImageInPlace(Core::App().logoNoMargin()));

		trayIcon->setIcon(icon);
		connect(trayIcon, SIGNAL(messageClicked()), this, SLOT(showFromTray()));
		attachToTrayIcon(trayIcon);
	}
	updateIconCounters();

	trayIcon->show();
}

void MainWindow::showTrayTooltip() {
	if (trayIcon && !cSeenTrayTooltip()) {
		trayIcon->showMessage(
			AppName.utf16(),
			tr::lng_tray_icon_text(tr::now),
			QSystemTrayIcon::Information,
			10000);
		cSetSeenTrayTooltip(true);
		Local::writeSettings();
	}
}

void MainWindow::workmodeUpdated(DBIWorkMode mode) {
	switch (mode) {
	case dbiwmWindowAndTray: {
		psSetupTrayIcon();
		HWND psOwner = (HWND)GetWindowLongPtr(ps_hWnd, GWLP_HWNDPARENT);
		if (psOwner) {
			SetWindowLongPtr(ps_hWnd, GWLP_HWNDPARENT, 0);
			psRefreshTaskbarIcon();
		}
	} break;

	case dbiwmTrayOnly: {
		psSetupTrayIcon();
		HWND psOwner = (HWND)GetWindowLongPtr(ps_hWnd, GWLP_HWNDPARENT);
		if (!psOwner) {
			SetWindowLongPtr(ps_hWnd, GWLP_HWNDPARENT, (LONG)ps_tbHider_hWnd);
		}
	} break;

	case dbiwmWindowOnly: {
		if (trayIcon) {
			trayIcon->setContextMenu(0);
			trayIcon->deleteLater();
		}
		trayIcon = 0;

		HWND psOwner = (HWND)GetWindowLongPtr(ps_hWnd, GWLP_HWNDPARENT);
		if (psOwner) {
			SetWindowLongPtr(ps_hWnd, GWLP_HWNDPARENT, 0);
			psRefreshTaskbarIcon();
		}
	} break;
	}
}

void MainWindow::unreadCounterChangedHook() {
	setWindowTitle(titleText());
	updateIconCounters();
}

void MainWindow::updateIconCounters() {
	const auto counter = Core::App().unreadBadge();
	const auto muted = Core::App().unreadBadgeMuted();

	auto iconSizeSmall = QSize(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
	auto iconSizeBig = QSize(GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));

	auto &bg = (muted ? st::trayCounterBgMute : st::trayCounterBg);
	auto &fg = st::trayCounterFg;
	auto iconSmallPixmap16 = App::pixmapFromImageInPlace(iconWithCounter(16, counter, bg, fg, true));
	auto iconSmallPixmap32 = App::pixmapFromImageInPlace(iconWithCounter(32, counter, bg, fg, true));
	QIcon iconSmall, iconBig;
	iconSmall.addPixmap(iconSmallPixmap16);
	iconSmall.addPixmap(iconSmallPixmap32);
	iconBig.addPixmap(App::pixmapFromImageInPlace(iconWithCounter(32, taskbarList.Get() ? 0 : counter, bg, fg, false)));
	iconBig.addPixmap(App::pixmapFromImageInPlace(iconWithCounter(64, taskbarList.Get() ? 0 : counter, bg, fg, false)));
	if (trayIcon) {
		// Force Qt to use right icon size, not the larger one.
		QIcon forTrayIcon;
		forTrayIcon.addPixmap(iconSizeSmall.width() >= 20 ? iconSmallPixmap32 : iconSmallPixmap16);
		trayIcon->setIcon(forTrayIcon);
	}

	psDestroyIcons();
	ps_iconSmall = createHIconFromQIcon(iconSmall, iconSizeSmall.width(), iconSizeSmall.height());
	ps_iconBig = createHIconFromQIcon(iconBig, iconSizeBig.width(), iconSizeBig.height());
	SendMessage(ps_hWnd, WM_SETICON, 0, (LPARAM)ps_iconSmall);
	SendMessage(ps_hWnd, WM_SETICON, 1, (LPARAM)(ps_iconBig ? ps_iconBig : ps_iconSmall));
	if (taskbarList.Get()) {
		if (counter > 0) {
			QIcon iconOverlay;
			iconOverlay.addPixmap(App::pixmapFromImageInPlace(iconWithCounter(-16, counter, bg, fg, false)));
			iconOverlay.addPixmap(App::pixmapFromImageInPlace(iconWithCounter(-32, counter, bg, fg, false)));
			ps_iconOverlay = createHIconFromQIcon(iconOverlay, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
		}
		auto description = (counter > 0) ? tr::lng_unread_bar(tr::now, lt_count, counter) : QString();
		taskbarList->SetOverlayIcon(ps_hWnd, ps_iconOverlay, description.toStdWString().c_str());
	}
	SetWindowPos(ps_hWnd, 0, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void MainWindow::initHook() {
	if (const auto native = QGuiApplication::platformNativeInterface()) {
		ps_hWnd = static_cast<HWND>(native->nativeResourceForWindow(
			QByteArrayLiteral("handle"),
			windowHandle()));
	}
	if (!ps_hWnd) {
		return;
	}

	handleSessionNotification = (Dlls::WTSRegisterSessionNotification != nullptr)
		&& (Dlls::WTSUnRegisterSessionNotification != nullptr);
	if (handleSessionNotification) {
		Dlls::WTSRegisterSessionNotification(ps_hWnd, NOTIFY_FOR_THIS_SESSION);
	}

	psInitSysMenu();
}

void MainWindow::initShadows() {
	_psShadowWindows.init(this, st::windowShadowFg->c);
	_shadowsWorking = true;
	psUpdateMargins();
	shadowsUpdate(ShadowsChange::Hidden);
}

void MainWindow::firstShadowsUpdate() {
	if (!(windowState() & Qt::WindowMinimized) && !isHidden()) {
		shadowsUpdate(ShadowsChange::Moved | ShadowsChange::Resized | ShadowsChange::Shown);
	}
}

void MainWindow::stateChangedHook(Qt::WindowState state) {
	updateSystemMenu(state);
}

void MainWindow::psInitSysMenu() {
	Qt::WindowStates states = windowState();
	ps_menu = GetSystemMenu(ps_hWnd, FALSE);
	updateSystemMenu(windowHandle()->windowState());
}

void MainWindow::updateSystemMenu(Qt::WindowState state) {
	if (!ps_menu) return;

	int menuToDisable = SC_RESTORE;
	if (state == Qt::WindowMaximized) {
		menuToDisable = SC_MAXIMIZE;
	} else if (state == Qt::WindowMinimized) {
		menuToDisable = SC_MINIMIZE;
	}
	int itemCount = GetMenuItemCount(ps_menu);
	for (int i = 0; i < itemCount; ++i) {
		MENUITEMINFO itemInfo = { 0 };
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

void MainWindow::psUpdateMargins() {
	if (!ps_hWnd || _inUpdateMargins) return;

	_inUpdateMargins = true;

	RECT r, a;

	GetClientRect(ps_hWnd, &r);
	a = r;

	LONG style = GetWindowLongPtr(ps_hWnd, GWL_STYLE), styleEx = GetWindowLongPtr(ps_hWnd, GWL_EXSTYLE);
	AdjustWindowRectEx(&a, style, false, styleEx);
	QMargins margins = QMargins(a.left - r.left, a.top - r.top, r.right - a.right, r.bottom - a.bottom);
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

		_deltaLeft = w.left - m.left;
		_deltaTop = w.top - m.top;
		_deltaRight = m.right - w.right;
		_deltaBottom = m.bottom - w.bottom;

		margins.setLeft(margins.left() - _deltaLeft);
		margins.setRight(margins.right() - _deltaRight);
		margins.setBottom(margins.bottom() - _deltaBottom);
		margins.setTop(margins.top() - _deltaTop);
	} else if (_deltaLeft != 0 || _deltaTop != 0 || _deltaRight != 0 || _deltaBottom != 0) {
		RECT w;
		GetWindowRect(ps_hWnd, &w);
		SetWindowPos(ps_hWnd, 0, 0, 0, w.right - w.left - _deltaLeft - _deltaRight, w.bottom - w.top - _deltaBottom - _deltaTop, SWP_NOMOVE | SWP_NOSENDCHANGING | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREPOSITION);
		_deltaLeft = _deltaTop = _deltaRight = _deltaBottom = 0;
	}

	if (const auto native = QGuiApplication::platformNativeInterface()) {
		native->setWindowProperty(
			windowHandle()->handle(),
			qsl("WindowsCustomMargins"),
			QVariant::fromValue<QMargins>(margins));
	}
	if (!_themeInited) {
		_themeInited = true;
		if (!IsWindows8OrGreater()) {
			if (Dlls::SetWindowTheme != nullptr) {
				Dlls::SetWindowTheme(ps_hWnd, L" ", L" ");
				QApplication::setStyle(QStyleFactory::create(qsl("Windows")));
			}
		}
	}
	_inUpdateMargins = false;
}

HWND MainWindow::psHwnd() const {
	return ps_hWnd;
}

HMENU MainWindow::psMenu() const {
	return ps_menu;
}

void MainWindow::psDestroyIcons() {
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

MainWindow::~MainWindow() {
	if (handleSessionNotification) {
		Dlls::WTSUnRegisterSessionNotification(ps_hWnd);
	}
	if (taskbarList) {
		taskbarList.Reset();
	}

	_shadowsWorking = false;
	if (ps_menu) DestroyMenu(ps_menu);
	psDestroyIcons();
	_psShadowWindows.destroy();
	if (ps_tbHider_hWnd) DestroyWindow(ps_tbHider_hWnd);

	EventFilter::Destroy();
}

} // namespace Platform
