/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/widgets/buttons.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"

namespace Ui {
class DynamicImage;
class ElasticScroll;
class IconButton;
} // namespace Ui

namespace Ui::Text {
struct MarkedContext;
} // namespace Ui::Text

namespace Data {
struct UnreviewedAuth;
} // namespace Data

namespace Dialogs {

[[nodiscard]] int PillRadius();

int PaintSuggestionBubbleBackground(
	QPainter &p,
	QRect outer,
	const Ui::BoxShadow &shadow,
	int cornerRadius = 0);

void PaintBottomFade(
	QPainter &p,
	int outerWidth,
	int fadeHeight,
	style::color bg);

class UnconfirmedAuthWrap : public Ui::SlideWrap<Ui::VerticalLayout> {
public:
	UnconfirmedAuthWrap(
		not_null<Ui::RpWidget*> parent,
		object_ptr<Ui::VerticalLayout> &&child);

	[[nodiscard]] const Ui::BoxShadow &shadow() const {
		return _shadow;
	}

	[[nodiscard]] rpl::producer<int> desiredHeightValue() const override;

	void setCollapseProgress(rpl::producer<float64> progress);
	void prepareCollapseSnapshot();

protected:
	int resizeGetHeight(int newWidth) override;

private:
	void releaseCollapseSnapshot();

	float64 _collapseProgress = 0.;
	QPixmap _collapseSnapshot;
	Ui::BoxShadow _shadow;

};

not_null<UnconfirmedAuthWrap*> CreateUnconfirmedAuthContent(
		not_null<Ui::RpWidget*> parent,
		rpl::producer<std::vector<Data::UnreviewedAuth>> list,
		Fn<void(bool)> callback,
		rpl::producer<float64> collapseProgress);

struct TopBarSuggestionGeometry {
	int cardInnerHeight = 0;
	int iconLeft = 0;
	int leadingTextSkip = 0;
	int rightInset = 0;
	int cornerRadius = 0;
	bool centerSingleLineTitle = false;
};

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

	void setHideCallback(Fn<void()>);
	void setRightIcon(RightIcon);
	void setRightButton(
		rpl::producer<TextWithEntities> text,
		Fn<void()> callback);
	void setRightBadge(rpl::producer<int> count);
	void setLeadingWidget(Ui::RpWidget *widget);
	void setGeometryOverride(TopBarSuggestionGeometry geometry);
	void setCollapseProgress(rpl::producer<float64> progress);
	void prepareCollapseSnapshot();

	[[nodiscard]] const style::TextStyle &contentTitleSt() const;

protected:
	void paintEvent(QPaintEvent *) override;
	int resizeGetHeight(int newWidth) override;

private:
	void draw(QPainter &p);
	void releaseCollapseSnapshot();

	const style::TextStyle &_titleSt;
	const style::TextStyle &_contentTitleSt;
	const style::TextStyle &_contentTextSt;

	Ui::Text::String _contentTitle;
	Ui::Text::String _contentText;
	float64 _collapseProgress = 0.;
	QPixmap _collapseSnapshot;
	std::optional<QColor> _descriptionColorOverride;

	Ui::BoxShadow _shadow;

	base::unique_qptr<Ui::IconButton> _rightHide;
	base::unique_qptr<Ui::IconButton> _rightArrow;
	base::unique_qptr<Ui::RoundButton> _rightButton;
	rpl::lifetime _rightBadgeLifetime;
	QString _rightBadgeText;
	QSize _rightBadgeSize;
	QPointer<Ui::RpWidget> _leadingWidget;
	rpl::lifetime _leadingWidgetLifetime;
	Fn<void()> _hideCallback;
	Fn<bool()> _emojiPaused;

	int _leftPadding = 0;
	TopBarSuggestionGeometry _geometry;

	RightIcon _rightIcon = RightIcon::None;

	std::shared_ptr<Ui::DynamicImage> _rightPhoto;
	QImage _rightPhotoImage;

};

struct MountTopBarSuggestionArgs {
	not_null<Ui::ElasticScroll*> scroll;
	not_null<Ui::VerticalLayout*> innerList;
	not_null<Ui::SlideWrap<Ui::RpWidget>*> wrap;
	base::unique_qptr<Ui::RpWidget> *placeholder = nullptr;
	Fn<void(int)> heightChanged;
};

void MountTopBarSuggestion(MountTopBarSuggestionArgs args);

[[nodiscard]] not_null<Ui::RpWidget*> CreateRequestsBubbleIcon(
	not_null<Ui::RpWidget*> parent);

} // namespace Dialogs
