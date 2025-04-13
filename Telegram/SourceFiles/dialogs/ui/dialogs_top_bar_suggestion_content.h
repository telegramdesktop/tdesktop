/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/widgets/buttons.h"

namespace Ui {
class DynamicImage;
class IconButton;
} // namespace Ui

namespace Dialogs {

class TopBarSuggestionContent : public Ui::RippleButton {
public:
	enum class RightIcon {
		None,
		Close,
		Arrow,
	};

	TopBarSuggestionContent(not_null<Ui::RpWidget*>);

	void setContent(
		TextWithEntities title,
		TextWithEntities description,
		bool makeContext = false);

	[[nodiscard]] rpl::producer<int> desiredHeightValue() const override;

	void setHideCallback(Fn<void()>);
	void setRightIcon(RightIcon);
	void setLeftPadding(int);

protected:
	void paintEvent(QPaintEvent *) override;

private:
	void draw(QPainter &p);

	const style::TextStyle &_titleSt;
	const style::TextStyle &_contentTitleSt;
	const style::TextStyle &_contentTextSt;

	Ui::Text::String _contentTitle;
	Ui::Text::String _contentText;
	rpl::variable<int> _lastPaintedContentLineAmount = 0;
	rpl::variable<int> _lastPaintedContentTop = 0;

	base::unique_qptr<Ui::IconButton> _rightHide;
	base::unique_qptr<Ui::IconButton> _rightArrow;
	Fn<void()> _hideCallback;

	int _leftPadding = 0;

	RightIcon _rightIcon = RightIcon::None;

	std::shared_ptr<Ui::DynamicImage> _rightPhoto;
	QImage _rightPhotoImage;

};

} // namespace Dialogs
