#pragma once

#include <QAccessibleWidget>
#include <QAccessibleInterface>
#include <QRect>
#include <QtCore/QPointer>

namespace Dialogs {

class InnerWidget;

class AccessibleRow : public QAccessibleInterface {
public:
    AccessibleRow(InnerWidget *parent, int index);

    QObject *object() const override;
    bool isValid() const override;
    QWindow *window() const override;
    QAccessibleInterface *parent() const override;
    QAccessibleInterface *child(int index) const override;
    QAccessibleInterface *childAt(int x, int y) const override; 
    int index() const { return _index; }
    int childCount() const override;
    int indexOfChild(const QAccessibleInterface *child) const override;
    QString text(QAccessible::Text t) const override;
    void setText(QAccessible::Text t, const QString &text) override; 
    QRect rect() const override;
    QAccessible::Role role() const override;
    QAccessible::State state() const override;

private:
    QPointer<InnerWidget> _parent;
    int _index = -1;
};

class AccessibleInnerWidget : public QAccessibleWidget {
public:
    AccessibleInnerWidget(InnerWidget *widget);
    QAccessibleInterface *child(int index) const override;
    int childCount() const override;
    int indexOfChild(const QAccessibleInterface *child) const override;
    QAccessibleInterface *focusChild() const override;
    QAccessibleInterface *childAt(int x, int y) const override;
    QAccessible::Role role() const override;
    QAccessible::State state() const override;

private:
    QPointer<InnerWidget> _inner;
};

} // namespace Dialogs