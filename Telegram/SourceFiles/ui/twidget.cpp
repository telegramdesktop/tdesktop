/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "twidget.h"

#include "application.h"
#include "mainwindow.h"

namespace Fonts {
namespace {

bool ValidateFont(const QString &familyName, int flags = 0) {
	QFont checkFont(familyName);
	checkFont.setPixelSize(13);
	checkFont.setBold(flags & style::internal::FontBold);
	checkFont.setItalic(flags & style::internal::FontItalic);
	checkFont.setUnderline(flags & style::internal::FontUnderline);
	checkFont.setStyleStrategy(QFont::PreferQuality);
	auto realFamily = QFontInfo(checkFont).family();
	if (realFamily.trimmed().compare(familyName, Qt::CaseInsensitive)) {
		LOG(("Font Error: could not resolve '%1' font, got '%2' after feeding '%3'.").arg(familyName).arg(realFamily));
		return false;
	}

	auto metrics = QFontMetrics(checkFont);
	if (!metrics.height()) {
		LOG(("Font Error: got a zero height in '%1'.").arg(familyName));
		return false;
	}

	return true;
}

bool LoadCustomFont(const QString &filePath, const QString &familyName, int flags = 0) {
	auto regularId = QFontDatabase::addApplicationFont(filePath);
	if (regularId < 0) {
		LOG(("Font Error: could not add '%1'.").arg(filePath));
		return false;
	}

	auto found = [&familyName, regularId] {
		for (auto &family : QFontDatabase::applicationFontFamilies(regularId)) {
			if (!family.trimmed().compare(familyName, Qt::CaseInsensitive)) {
				return true;
			}
		}
		return false;
	};
	if (!found()) {
		LOG(("Font Error: could not locate '%1' font in '%2'.").arg(familyName).arg(filePath));
		return false;
	}

	return ValidateFont(familyName, flags);
}

bool Started = false;
QString OpenSansOverride;
QString OpenSansSemiboldOverride;

} // namespace

void Start() {
	if (Started) {
		return;
	}
	Started = true;

	auto regular = LoadCustomFont(qsl(":/gui/fonts/OpenSans-Regular.ttf"), qsl("Open Sans"));
	auto bold = LoadCustomFont(qsl(":/gui/fonts/OpenSans-Bold.ttf"), qsl("Open Sans"), style::internal::FontBold);
	auto semibold = LoadCustomFont(qsl(":/gui/fonts/OpenSans-Semibold.ttf"), qsl("Open Sans Semibold"));

#ifdef Q_OS_WIN
	// Attempt to workaround a strange font bug with Open Sans Semibold not loading.
	// See https://github.com/telegramdesktop/tdesktop/issues/3276 for details.
	// Crash happens on "options.maxh / _t->_st->font->height" with "division by zero".
	// In that place "_t->_st->font" is "semiboldFont" is "font(13 "Open Sans Semibold").
	if (!regular || !bold) {
		if (ValidateFont(qsl("Segoe UI")) && ValidateFont(qsl("Segoe UI"), style::internal::FontBold)) {
			OpenSansOverride = qsl("Segoe UI");
			LOG(("Fonts Info: Using Segoe UI instead of Open Sans."));
		}
	}
	if (!semibold) {
		if (ValidateFont(qsl("Segoe UI Semibold"))) {
			OpenSansSemiboldOverride = qsl("Segoe UI Semibold");
			LOG(("Fonts Info: Using Segoe UI Semibold instead of Open Sans Semibold."));
		}
	}
#endif // Q_OS_WIN
}

QString GetOverride(const QString &familyName) {
	if (familyName == qstr("Open Sans")) {
		return OpenSansOverride.isEmpty() ? familyName : OpenSansOverride;
	} else if (familyName == qstr("Open Sans Semibold")) {
		return OpenSansSemiboldOverride.isEmpty() ? familyName : OpenSansSemiboldOverride;
	}
	return familyName;
}

} // Fonts

namespace Ui {
namespace {

class WidgetCreator : public QWidget {
public:
	static void Create(not_null<QWidget*> widget) {
		volatile auto unknown = widget.get();
		static_cast<WidgetCreator*>(unknown)->create();
	}

};

void CreateWidgetStateRecursive(not_null<QWidget*> target) {
	if (!target->testAttribute(Qt::WA_WState_Created)) {
		if (!target->isWindow()) {
			CreateWidgetStateRecursive(target->parentWidget());
			WidgetCreator::Create(target);
		} else if (!cIsSnowLeopard()) {
			WidgetCreator::Create(target);
		}
	}
}

void SendPendingEventsRecursive(QWidget *target, bool parentHiddenFlag) {
	auto wasVisible = target->isVisible();
	if (!wasVisible) {
		target->setAttribute(Qt::WA_WState_Visible, true);
	}
	if (target->testAttribute(Qt::WA_PendingMoveEvent)) {
		target->setAttribute(Qt::WA_PendingMoveEvent, false);
		QMoveEvent e(target->pos(), QPoint());
		QApplication::sendEvent(target, &e);
	}
	if (target->testAttribute(Qt::WA_PendingResizeEvent)) {
		target->setAttribute(Qt::WA_PendingResizeEvent, false);
		QResizeEvent e(target->size(), QSize());
		QApplication::sendEvent(target, &e);
	}

	auto removeVisibleFlag = [&] {
		return parentHiddenFlag
			|| target->testAttribute(Qt::WA_WState_Hidden);
	};

	auto &children = target->children();
	for (auto i = 0; i < children.size(); ++i) {
		auto child = children[i];
		if (child->isWidgetType()) {
			auto widget = static_cast<QWidget*>(child);
			if (!widget->isWindow()) {
				if (!widget->testAttribute(Qt::WA_WState_Created)) {
					WidgetCreator::Create(widget);
				}
				SendPendingEventsRecursive(widget, removeVisibleFlag());
			}
		}
	}

	if (removeVisibleFlag()) {
		target->setAttribute(Qt::WA_WState_Visible, false);
	}
}

} // namespace

void SendPendingMoveResizeEvents(not_null<QWidget*> target) {
	CreateWidgetStateRecursive(target);
	SendPendingEventsRecursive(target, !target->isVisible());
}

QPixmap GrabWidget(not_null<QWidget*> target, QRect rect, QColor bg) {
	SendPendingMoveResizeEvents(target);
	if (rect.isNull()) {
		rect = target->rect();
	}

	auto result = QPixmap(rect.size() * cIntRetinaFactor());
	result.setDevicePixelRatio(cRetinaFactor());
	if (!target->testAttribute(Qt::WA_OpaquePaintEvent)) {
		result.fill(bg);
	}
	target->render(
		&result,
		QPoint(0, 0),
		rect,
		QWidget::DrawChildren | QWidget::IgnoreMask);
	return result;
}

QImage GrabWidgetToImage(not_null<QWidget*> target, QRect rect, QColor bg) {
	Ui::SendPendingMoveResizeEvents(target);
	if (rect.isNull()) {
		rect = target->rect();
	}

	auto result = QImage(
		rect.size() * cIntRetinaFactor(),
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(cRetinaFactor());
	if (!target->testAttribute(Qt::WA_OpaquePaintEvent)) {
		result.fill(bg);
	}
	target->render(
		&result,
		QPoint(0, 0),
		rect,
		QWidget::DrawChildren | QWidget::IgnoreMask);
	return result;
}

} // namespace Ui

void sendSynteticMouseEvent(QWidget *widget, QEvent::Type type, Qt::MouseButton button, const QPoint &globalPoint) {
	if (auto windowHandle = widget->window()->windowHandle()) {
		auto localPoint = windowHandle->mapFromGlobal(globalPoint);
		QMouseEvent ev(type
			, localPoint
			, localPoint
			, globalPoint
			, button
			, QGuiApplication::mouseButtons() | button
			, QGuiApplication::keyboardModifiers()
#ifndef OS_MAC_OLD
			, Qt::MouseEventSynthesizedByApplication
#endif // OS_MAC_OLD
		);
		ev.setTimestamp(getms());
		QGuiApplication::sendEvent(windowHandle, &ev);
	}
}
