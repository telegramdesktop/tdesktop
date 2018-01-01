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

#include "settings/settings_layer.h"
#include "ui/wrap/vertical_layout.h"

namespace Settings {

class CoverWidget;
class BlockWidget;

class InnerWidget : public LayerInner, private base::Subscriber {
public:
	InnerWidget(QWidget *parent);

	// Count new height for width=newWidth and resize to it.
	void resizeToWidth(int newWidth, int contentLeft) override {
		_contentLeft = contentLeft;
		return TWidget::resizeToWidth(newWidth);
	}

protected:
	int resizeGetHeight(int newWidth) override;
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;

private:
	void fullRebuild();
	void refreshBlocks();

	object_ptr<CoverWidget> _cover = { nullptr };
	object_ptr<Ui::VerticalLayout> _blocks;

	UserData *_self = nullptr;

	int _contentLeft = 0;

};

} // namespace Settings
