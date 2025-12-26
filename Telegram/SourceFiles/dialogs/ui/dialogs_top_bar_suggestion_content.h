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
class GenericBox;
class IconButton;
class VerticalLayout;
template<typename Widget>
class SlideWrap;
} // namespace Ui

namespace Ui::Text {
struct MarkedContext;
} // namespace Ui::Text

namespace Data {
struct UnreviewedAuth;
} // namespace Data

namespace Dialogs {

not_null<Ui::SlideWrap<Ui::VerticalLayout>*> CreateUnconfirmedAuthContent(
		not_null<Ui::RpWidget*> parent,
		const std::vector<Data::UnreviewedAuth> &list,
		Fn<void(bool)> callback);

void ShowAuthDeniedBox(
	not_null<Ui::GenericBox*> box,
	float64 count,
	const QString &messageText);

class TopBarSuggestionContent : public Ui::RippleButton {
public:
	enum class RightIcon {
		None,
		Close,
		Arrow,
	};

	TopBarSuggestionContent(
		not_null<Ui::RpWidget*> parent,
		Fn<bool()> emojiPaused = nullptr);

	void setContent(
		TextWithEntities title,
		TextWithEntities description,
		std::optional<Ui::Text::MarkedContext> context = std::nullopt,
		std::optional<QColor> descriptionColorOverride = std::nullopt);

	[[nodiscard]] rpl::producer<int> desiredHeightValue() const override;

	void setHideCallback(Fn<void()>);
	void setRightIcon(RightIcon);
	void setRightButton(
		rpl::producer<TextWithEntities> text,
		Fn<void()> callback);
	void setLeftPadding(rpl::producer<int>);

	[[nodiscard]] const style::TextStyle &contentTitleSt() const;

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
	std::optional<QColor> _descriptionColorOverride;

	base::unique_qptr<Ui::IconButton> _rightHide;
	base::unique_qptr<Ui::IconButton> _rightArrow;
	base::unique_qptr<Ui::RoundButton> _rightButton;
	Fn<void()> _hideCallback;
	Fn<bool()> _emojiPaused;

	int _leftPadding = 0;

	RightIcon _rightIcon = RightIcon::None;

	std::shared_ptr<Ui::DynamicImage> _rightPhoto;
	QImage _rightPhotoImage;

};

} // namespace Dialogs
