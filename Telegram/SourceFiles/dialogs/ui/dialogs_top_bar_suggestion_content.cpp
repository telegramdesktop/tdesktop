/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/ui/dialogs_top_bar_suggestion_content.h"
#include "ui/effects/credits_graphics.h"
#include "ui/text/text_custom_emoji.h"
#include "ui/ui_rpl_filter.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_dialogs.h"
#include "styles/style_settings.h"

namespace Dialogs {

TopBarSuggestionContent::TopBarSuggestionContent(not_null<Ui::RpWidget*> p)
: Ui::RippleButton(p, st::defaultRippleAnimationBgOver)
, _titleSt(st::semiboldTextStyle)
, _contentTitleSt(st::dialogsTopBarSuggestionTitleStyle)
, _contentTextSt(st::dialogsTopBarSuggestionAboutStyle) {
	setRightIcon(RightIcon::Close);
}

void TopBarSuggestionContent::setRightIcon(RightIcon icon) {
	if (icon == _rightIcon) {
		return;
	}
	_rightHide = nullptr;
	_rightArrow = nullptr;
	_rightIcon = icon;
	if (icon == RightIcon::Close) {
		_rightHide = base::make_unique_q<Ui::IconButton>(
			this,
			st::dialogsCancelSearchInPeer);
		const auto rightHide = _rightHide.get();
		sizeValue() | rpl::filter_size(
		) | rpl::start_with_next([=](const QSize &s) {
			rightHide->moveToRight(st::buttonRadius, st::lineWidth);
		}, rightHide->lifetime());
		rightHide->show();
	} else if (icon == RightIcon::Arrow) {
		_rightArrow = base::make_unique_q<Ui::IconButton>(
			this,
			st::backButton);
		const auto arrow = _rightArrow.get();
		arrow->setIconOverride(
			&st::settingsPremiumArrow,
			&st::settingsPremiumArrowOver);
		arrow->setAttribute(Qt::WA_TransparentForMouseEvents);
		sizeValue() | rpl::filter_size(
		) | rpl::start_with_next([=](const QSize &s) {
			const auto &point = st::settingsPremiumArrowShift;
			arrow->moveToLeft(
				s.width() - arrow->width(),
				point.y() + (s.height() - arrow->height()) / 2);
		}, arrow->lifetime());
		arrow->show();
	}
}

void TopBarSuggestionContent::draw(QPainter &p) {
	const auto kLinesForPhoto = 3;
	const auto rightPhotoSize = _titleSt.font->ascent * kLinesForPhoto;
	const auto rightPhotoPlaceholder = _titleSt.font->height * kLinesForPhoto;

	const auto r = Ui::RpWidget::rect();
	p.fillRect(r, st::historyPinnedBg);
	Ui::RippleButton::paintRipple(p, 0, 0);
	const auto leftPadding = st::defaultDialogRow.padding.left()
		+ _leftPadding;
	const auto rightPadding = 0;
	const auto topPadding = st::msgReplyPadding.top();
	const auto availableWidthNoPhoto = r.width()
		- (_rightArrow
			? (_rightArrow->width() / 4 * 3) // Takes full height.
			: 0)
		- leftPadding
		- rightPadding;
	const auto availableWidth = availableWidthNoPhoto
		- (_rightHide ? _rightHide->width() : 0);
	const auto titleRight = leftPadding;
	const auto hasSecondLineTitle = availableWidth < _contentTitle.maxWidth();
	p.setPen(st::windowActiveTextFg);
	p.setPen(st::windowFg);
	{
		const auto left = leftPadding;
		const auto top = topPadding;
		_contentTitle.draw(p, {
			.position = QPoint(left, top),
			.outerWidth = hasSecondLineTitle
				? availableWidth
				: (availableWidth - titleRight),
			.availableWidth = availableWidth,
			.elisionLines = hasSecondLineTitle ? 2 : 1,
		});
	}
	{
		const auto left = leftPadding;
		const auto top = hasSecondLineTitle
			? (topPadding
				+ _titleSt.font->height
				+ _contentTitleSt.font->height)
			: topPadding + _titleSt.font->height;
		auto lastContentLineAmount = 0;
		const auto lineHeight = _contentTextSt.font->height;
		const auto lineLayout = [&](int line) -> Ui::Text::LineGeometry {
			line++;
			lastContentLineAmount = line;
			const auto diff = (st::sponsoredMessageBarMaxHeight)
				- line * lineHeight;
			if (diff < 3 * lineHeight) {
				return {
					.width = availableWidthNoPhoto,
					.elided = true,
				};
			} else if (diff < 2 * lineHeight) {
				return {};
			}
			line += (hasSecondLineTitle ? 2 : 1) + 1;
			return {
				.width = (line > kLinesForPhoto)
					? availableWidthNoPhoto
					: availableWidth,
			};
		};
		p.setPen(st::windowSubTextFg);
		_contentText.draw(p, {
			.position = QPoint(left, top),
			.outerWidth = availableWidth,
			.availableWidth = availableWidth,
			.geometry = Ui::Text::GeometryDescriptor{
				.layout = std::move(lineLayout),
			},
		});
		_lastPaintedContentTop = top;
		_lastPaintedContentLineAmount = lastContentLineAmount;
	}
}

void TopBarSuggestionContent::setContent(
		TextWithEntities title,
		TextWithEntities description,
		bool makeContext) {
	if (makeContext) {
		auto customEmojiFactory = [=, h = _contentTitleSt.font->height](
			QStringView data,
			const Ui::Text::MarkedContext &context
		) -> std::unique_ptr<Ui::Text::CustomEmoji> {
			return Ui::MakeCreditsIconEmoji(h, 1);
		};
		const auto context = Ui::Text::MarkedContext{
			.repaint = [=] { update(); },
			.customEmojiFactory = std::move(customEmojiFactory),
		};
		_contentTitle.setMarkedText(
			_contentTitleSt,
			std::move(title),
			kMarkupTextOptions,
			context);
		_contentText.setMarkedText(
			_contentTextSt,
			std::move(description),
			kMarkupTextOptions,
			context);
	} else {
		_contentTitle.setMarkedText(_contentTitleSt, std::move(title));
		_contentText.setMarkedText(_contentTextSt, std::move(description));
	}
}

void TopBarSuggestionContent::paintEvent(QPaintEvent *) {
	auto p = QPainter(this);
	draw(p);
}

rpl::producer<int> TopBarSuggestionContent::desiredHeightValue() const {
	const auto kLinesForPhoto = 3;
	const auto rightPhotoSize = _titleSt.font->ascent * kLinesForPhoto;
	const auto rightPhotoPlaceholder = _titleSt.font->height * kLinesForPhoto;
	return rpl::combine(
		_lastPaintedContentTop.value(),
		_lastPaintedContentLineAmount.value()
	) | rpl::distinct_until_changed() | rpl::map([=](
			int lastTop,
			int lastLines) {
		const auto bottomPadding = st::msgReplyPadding.top();
		const auto desiredHeight = lastTop
			+ (lastLines * _contentTextSt.font->height)
			+ bottomPadding;
		const auto minHeight = desiredHeight;
		return std::clamp(
			desiredHeight,
			minHeight,
			st::sponsoredMessageBarMaxHeight);
	});
}

void TopBarSuggestionContent::setHideCallback(Fn<void()> hideCallback) {
	_rightHide->setClickedCallback(std::move(hideCallback));
}

void TopBarSuggestionContent::setLeftPadding(int value) {
	_leftPadding = value;
	update();
}

} // namespace Dialogs
