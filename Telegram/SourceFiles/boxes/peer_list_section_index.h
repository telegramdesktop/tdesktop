/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/effects/animations.h"

// Right-edge A-Z fast-scroll index strip for the contacts box: thins letters
// when they do not all fit and magnifies the one under the cursor Dock-like.
class PeerListSectionIndex final : public Ui::RpWidget {
public:
	struct Entry {
		QString letter;
		int contentTop = 0;
	};
	explicit PeerListSectionIndex(QWidget *parent);

	void setLetters(std::vector<Entry> letters);
	void setVisibleLetters(base::flat_set<QString> letters);
	void setJumpCallback(Fn<void(int contentTop, anim::type)> callback);
	void setScrollCallback(Fn<void(not_null<QWheelEvent*>)> callback);

	[[nodiscard]] int idealWidth() const;

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void wheelEvent(QWheelEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	struct Slot {
		QString letter;
		int sourceIndex = 0;
		int y = 0;
	};
	void relayout();
	void setCursor(QPoint position);
	void scrubTo(int slot, anim::type animated);
	bool updateCursorFromGlobal();
	bool fisheyeFrame();
	[[nodiscard]] float64 slotScale(int slotY) const;
	[[nodiscard]] int slotAtY(int y) const;
	[[nodiscard]] float64 columnCenterX() const;

	std::vector<Entry> _letters;
	std::vector<Slot> _slots;
	std::vector<float64> _scale;
	base::flat_set<QString> _visible;
	Fn<void(int, anim::type)> _jump;
	Fn<void(not_null<QWheelEvent*>)> _scroll;
	int _pitch = 0;
	int _cursorX = -1;
	int _cursorY = -1;
	int _current = -1;
	bool _scrubbing = false;
	Ui::Animations::Basic _fisheye;

};
