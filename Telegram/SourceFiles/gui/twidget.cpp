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

#include "application.h"

namespace {
    void _sendResizeEvents(QWidget *target) {
        QResizeEvent e(target->size(), QSize());
        QApplication::sendEvent(target, &e);
        
        const QObjectList children = target->children();
        for (int i = 0; i < children.size(); ++i) {
            QWidget *child = static_cast<QWidget*>(children.at(i));
            if (child->isWidgetType() && !child->isWindow() && child->testAttribute(Qt::WA_PendingResizeEvent)) {
                _sendResizeEvents(child);
            }
        }
    }
}

QPixmap myGrab(QWidget *target, const QRect &rect) {
    if (!cRetina()) return target->grab(rect);
        
    if (target->testAttribute(Qt::WA_PendingResizeEvent) || !target->testAttribute(Qt::WA_WState_Created)) {
        _sendResizeEvents(target);
    }
    
    qreal dpr = App::app()->devicePixelRatio();
    QPixmap result(rect.size() * dpr);
    result.setDevicePixelRatio(dpr);
    result.fill(Qt::transparent);
    target->render(&result, QPoint(), QRegion(rect), QWidget::DrawWindowBackground | QWidget::DrawChildren | QWidget::IgnoreMask);
    return result;
}
