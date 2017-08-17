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
#pragma once

namespace Profile {

class CoverWidget;
class BlockWidget;
class SectionMemento;

class InnerWidget final : public TWidget {
	Q_OBJECT

public:
	InnerWidget(QWidget *parent, PeerData *peer);

	PeerData *peer() const {
		return _peer;
	}

	void resizeToWidth(int newWidth, int minHeight) {
		_minHeight = minHeight;
		return TWidget::resizeToWidth(newWidth);
	}

	// Updates the area that is visible inside the scroll container.
	void setVisibleTopBottom(int visibleTop, int visibleBottom) override;

	// Profile fixed top bar should use this flag to decide
	// if it shows "Share contact" button or not.
	// It should show it only if it is hidden in the cover.
	bool shareContactButtonShown() const;

	void saveState(not_null<SectionMemento*> memento);
	void restoreState(not_null<SectionMemento*> memento);

	void showFinished();

signals:
	void cancelled();

private slots:
	void onBlockHeightUpdated();

protected:
	void paintEvent(QPaintEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

	// Resizes content and counts natural widget height for the desired width.
	int resizeGetHeight(int newWidth) override;

private:
	void createBlocks();

	// Counts the natural widget height after resizing of child widgets.
	int countHeight() const;

	enum class Mode {
		OneColumn,
		TwoColumn,
	};
	int countBlocksLeft(int newWidth) const;
	Mode countBlocksMode(int newWidth) const;
	int countLeftColumnWidth(int newWidth) const;
	int countBlocksHeight(RectPart countSide) const;
	void resizeBlocks(int newWidth);
	void refreshBlocksPositions();

	// Sometimes height of this widget is larger than it is required
	// so that it is allowed to scroll down to the desired position.
	// When resizing with scroll moving up the additional height may be decreased.
	void decreaseAdditionalHeight(int removeHeight);

	PeerData *_peer;

	// Height that we added to the natural height so that it is allowed
	// to scroll down to the desired position.
	int _addedHeight = 0;
	int _minHeight = 0;
	int _visibleTop = 0;
	int _visibleBottom = 0;

	object_ptr<CoverWidget> _cover;

	int _blocksLeft = 0; // Caching countBlocksLeft() result.
	int _blocksTop = 0;
	int _columnDivider = 0;
	int _leftColumnWidth = 0; // Caching countLeftColumnWidth() result.
	struct Block {
		BlockWidget *block;
		RectPart side;
	};
	QList<Block> _blocks;

	Mode _mode = Mode::OneColumn;

};

} // namespace Profile
