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
	TopBarSuggestionContent(not_null<Ui::RpWidget*>);

	void setContent(
		TextWithEntities title,
		TextWithEntities description);

	[[nodiscard]] rpl::producer<int> desiredHeightValue() const override;

	void setHideCallback(Fn<void()>);

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
	Fn<void()> _hideCallback;

	std::shared_ptr<Ui::DynamicImage> _rightPhoto;
	QImage _rightPhotoImage;

};

} // namespace Dialogs
