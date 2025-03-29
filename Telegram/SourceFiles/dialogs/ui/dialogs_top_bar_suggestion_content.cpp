/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/ui/dialogs_top_bar_suggestion_content.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_dialogs.h"

namespace Dialogs {
namespace {
} // namespace

TopBarSuggestionContent::TopBarSuggestionContent(not_null<Ui::RpWidget*> p)
: Ui::RippleButton(p, st::defaultRippleAnimationBgOver)
, _titleSt(st::semiboldTextStyle)
, _contentTitleSt(st::semiboldTextStyle)
, _contentTextSt(st::defaultTextStyle) {
}

void TopBarSuggestionContent::draw(QPainter &p) {
	const auto kLinesForPhoto = 3;
	const auto rightPhotoSize = _titleSt.font->ascent * kLinesForPhoto;
	const auto rightPhotoPlaceholder = _titleSt.font->height * kLinesForPhoto;

	const auto r = Ui::RpWidget::rect();
	p.fillRect(r, st::historyPinnedBg);
	Ui::RippleButton::paintRipple(p, 0, 0);
	const auto leftPadding = st::msgReplyBarSkip + st::msgReplyBarSkip;
	const auto rightPadding = st::msgReplyBarSkip;
	const auto topPadding = st::msgReplyPadding.top();
	const auto availableWidthNoPhoto = r.width()
		- leftPadding
		- rightPadding;
	const auto availableWidth = availableWidthNoPhoto
		- (_rightHide ? _rightHide->width() : 0);
	const auto titleRight = leftPadding
		+ _titleSt.font->spacew * 2;
	const auto hasSecondLineTitle = (titleRight
		> (availableWidth - _contentTitle.maxWidth()));
	p.setPen(st::windowActiveTextFg);
	p.setPen(st::windowFg);
	{
		const auto left = hasSecondLineTitle ? leftPadding : titleRight;
		const auto top = hasSecondLineTitle
			? (topPadding + _titleSt.font->height)
			: topPadding;
		_contentTitle.draw(p, {
			.position = QPoint(left, top),
			.outerWidth = hasSecondLineTitle
				? availableWidth
				: (availableWidth - titleRight),
			.availableWidth = availableWidth,
			.elisionLines = 1,
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
		TextWithEntities description) {
	_contentTitle.setMarkedText(_contentTitleSt, std::move(title));
	_contentText.setMarkedText(_contentTextSt, std::move(description));
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

} // namespace Dialogs
