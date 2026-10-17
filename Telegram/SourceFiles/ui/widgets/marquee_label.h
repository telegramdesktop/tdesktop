/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "base/unique_qptr.h"
#include "ui/effects/animations.h"
#include "ui/rp_widget.h"
#include "ui/text/text.h"

class Painter;

namespace style {
struct FlatLabel;
} // namespace style

namespace Ui {

class PopupMenu;

class MarqueeLabel final : public RpWidget {
public:
	MarqueeLabel(
		QWidget *parent,
		rpl::producer<QString> text,
		const style::FlatLabel &st);

	[[nodiscard]] const style::FlatLabel &st() const {
		return _st;
	}

	void setTextColorOverride(std::optional<QColor> color);
	void setContextCopyText(const QString &copyText);
	void setSelectable(bool selectable);

	QAccessible::Role accessibilityRole() override;
	QString accessibilityName() override;

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;
	void showEvent(QShowEvent *e) override;
	void hideEvent(QHideEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseDoubleClickEvent(QMouseEvent *e) override;
	void enterEventHook(QEnterEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void focusInEvent(QFocusEvent *e) override;
	void focusOutEvent(QFocusEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	bool eventHook(QEvent *e) override;

private:
	void setText(const QString &text);
	void refreshMarqueeState();
	bool marqueeStep(crl::time now);
	void paintMarquee(Painter &p, TextSelection selection);
	void invalidateCache();
	void validateTape();
	void validateFades();
	void validateFrame(float64 offset, TextSelection selection);

	[[nodiscard]] bool paused() const;
	[[nodiscard]] float64 linearMax() const;
	[[nodiscard]] Text::StateResult getTextState(QPoint m) const;
	Text::StateResult dragActionUpdate();
	void dragActionStart(QPoint position, Qt::MouseButton button);
	void dragActionFinish(QPoint position, Qt::MouseButton button);
	void updateSelection(const Text::StateResult &state);
	void updateCursor(const Text::StateResult &state);
	void clearSelection();
	void copySelectedText();
	void reanchorWrapped();
	void checkEdgeScroll();
	bool edgeScrollStep(crl::time now);
	void refreshOutsideClickFilter();
	void handleOutsidePress(QEvent *e);

	const style::FlatLabel &_st;
	Text::String _text;
	std::optional<QColor> _textColorOverride;
	QString _contextCopyText;
	base::unique_qptr<PopupMenu> _menu;

	QImage _frame;
	QImage _tape;
	QImage _fadeLeft;
	QImage _fadeRight;
	std::optional<float64> _frameOffset;
	TextSelection _frameSelection;
	bool _frameHonestFades = false;
	Animations::Basic _marquee;
	crl::time _lastUpdate = 0;
	crl::time _lastFrame = 0;
	crl::time _delay = 0;
	float64 _offset = 0.;
	int _availableTextWidth = 0;
	bool _overflown = false;

	TextSelection _selection;
	TextSelection _savedSelection;
	TextSelectType _selectionType = TextSelectType::Letters;
	style::cursor _cursor = style::cur_default;
	uint16 _dragSymbol = 0;
	bool _selectable = false;
	bool _inside = false;
	bool _selecting = false;
	bool _dragWasInactive = false;
	bool _windowActive = true;
	QPoint _lastMousePos;
	QPoint _trippleClickPoint;
	base::Timer _trippleClickTimer;
	Animations::Basic _edgeScroll;
	base::unique_qptr<QObject> _outsideClickFilter;

};

} // namespace Ui
