/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "old_settings/settings_layer.h"
#include "ui/wrap/vertical_layout.h"

namespace OldSettings {

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

	int getUpdateTop() const;

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
	Fn<int()> _getUpdateTop;

};

} // namespace Settings
