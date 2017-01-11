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

namespace Settings {

class CoverWidget;
class BlockWidget;

class InnerWidget : public TWidget, private base::Subscriber {
	Q_OBJECT

public:
	InnerWidget(QWidget *parent);

	// Count new height for width=newWidth and resize to it.
	void resizeToWidth(int newWidth, int contentLeft) {
		_contentLeft = contentLeft;
		return TWidget::resizeToWidth(newWidth);
	}

	// Updates the area that is visible inside the scroll container.
	void setVisibleTopBottom(int visibleTop, int visibleBottom) override;

	void showFinished();

private slots:
	void onBlockHeightUpdated();

protected:
	// Resizes content and counts natural widget height for the desired width.
	int resizeGetHeight(int newWidth) override;

private:
	void selfUpdated();
	void refreshBlocks();

	// Returns the new height value.
	int refreshBlocksPositions(int newWidth);

	object_ptr<CoverWidget> _cover = { nullptr };
	QList<BlockWidget*> _blocks;

	UserData *_self = nullptr;

	int _contentLeft = 0;
	bool _showFinished = false;

	int _visibleTop = 0;
	int _visibleBottom = 0;

};

} // namespace Settings
