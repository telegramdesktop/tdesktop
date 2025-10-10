/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/ui/dialogs_top_bar_suggestion_content.h"

#include "base/call_delayed.h"
#include "data/data_authorization.h"
#include "lang/lang_keys.h"
#include "ui/rect.h"
#include "ui/text/format_values.h"
#include "ui/text/text_custom_emoji.h"
#include "ui/ui_rpl_filter.h"
#include "ui/vertical_list.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_dialogs.h"
#include "styles/style_settings.h"
#include "styles/style_layers.h"

namespace Dialogs {

class UnconfirmedAuthWrap : public Ui::SlideWrap<Ui::VerticalLayout> {
public:
	UnconfirmedAuthWrap(
		not_null<Ui::RpWidget*> parent,
		object_ptr<Ui::VerticalLayout> &&child)
	: Ui::SlideWrap<Ui::VerticalLayout>(parent, std::move(child)) {
	}

	rpl::producer<int> desiredHeightValue() const override {
		return entity()->heightValue();
	}
};

not_null<Ui::SlideWrap<Ui::VerticalLayout>*> CreateUnconfirmedAuthContent(
		not_null<Ui::RpWidget*> parent,
		const std::vector<Data::UnreviewedAuth> &list,
		Fn<void(bool)> callback) {
	const auto wrap = Ui::CreateChild<UnconfirmedAuthWrap>(
		parent,
		object_ptr<Ui::VerticalLayout>(parent));
	const auto content = wrap->entity();
	content->paintRequest() | rpl::start_with_next([=] {
		auto p = QPainter(content);
		p.fillRect(content->rect(), st::dialogsBg);
	}, content->lifetime());

	const auto padding = st::dialogsUnconfirmedAuthPadding;

	Ui::AddSkip(content);

	content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			tr::lng_unconfirmed_auth_title(),
			st::dialogsUnconfirmedAuthTitle),
		padding,
		style::al_top);

	Ui::AddSkip(content);

	auto messageText = QString();
	if (list.size() == 1) {
		const auto &auth = list.at(0);
		messageText = tr::lng_unconfirmed_auth_single(
			tr::now,
			lt_from,
			auth.device,
			lt_country,
			auth.location);
	} else {
		auto commonLocation = list.at(0).location;
		for (auto i = 1; i < list.size(); ++i) {
			if (commonLocation != list.at(i).location) {
				commonLocation.clear();
				break;
			}
		}
		if (commonLocation.isEmpty()) {
			messageText = tr::lng_unconfirmed_auth_multiple(
				tr::now,
				lt_count,
				list.size());
		} else {
			messageText = tr::lng_unconfirmed_auth_multiple_from(
				tr::now,
				lt_count,
				list.size(),
				lt_country,
				commonLocation);
		}
	}

	content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			rpl::single(messageText),
			st::dialogsUnconfirmedAuthAbout),
		padding,
		style::al_top)->setTryMakeSimilarLines(true);

	Ui::AddSkip(content);
	const auto buttons = content->add(object_ptr<Ui::FixedHeightWidget>(
		content,
		st::dialogsUnconfirmedAuthButton.height));
	const auto yes = Ui::CreateChild<Ui::RoundButton>(
		buttons,
		tr::lng_unconfirmed_auth_confirm(),
		st::dialogsUnconfirmedAuthButton);
	const auto no = Ui::CreateChild<Ui::RoundButton>(
		buttons,
		tr::lng_unconfirmed_auth_deny(),
		st::dialogsUnconfirmedAuthButtonNo);
	yes->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
	no->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
	yes->setClickedCallback([=] {
		wrap->toggle(false, anim::type::normal);
		base::call_delayed(st::universalDuration, wrap, [=] {
			callback(true);
		});
	});
	no->setClickedCallback([=] {
		wrap->toggle(false, anim::type::normal);
		base::call_delayed(st::universalDuration, wrap, [=] {
			callback(false);
		});
	});
	buttons->sizeValue(
	) | rpl::filter_size(
	) | rpl::start_with_next([=](const QSize &s) {
		const auto halfWidth = (s.width() - rect::m::sum::h(padding)) / 2;
		yes->moveToLeft(
			padding.left() + (halfWidth - yes->width()) / 2,
			0);
		no->moveToLeft(
			padding.left() + halfWidth + (halfWidth - no->width()) / 2,
			0);
	}, buttons->lifetime());
	Ui::AddSkip(content);
	content->add(object_ptr<Ui::FadeShadow>(content));

	return wrap;
}

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

	const auto r = Ui::RpWidget::rect();
	p.fillRect(r, st::historyPinnedBg);
	p.fillRect(
		r.x(),
		r.y() + r.height() - st::lineWidth,
		r.width(),
		st::lineWidth,
		st::shadowFg);
	Ui::RippleButton::paintRipple(p, 0, 0);
	const auto leftPadding = _leftPadding;
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
		std::optional<Ui::Text::MarkedContext> context) {
	if (context) {
		context->repaint = [=] { update(); };
		_contentTitle.setMarkedText(
			_contentTitleSt,
			std::move(title),
			kMarkupTextOptions,
			*context);
		_contentText.setMarkedText(
			_contentTextSt,
			std::move(description),
			kMarkupTextOptions,
			base::take(*context));
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
		return std::min(desiredHeight, st::sponsoredMessageBarMaxHeight);
	});
}

void TopBarSuggestionContent::setHideCallback(Fn<void()> hideCallback) {
	Expects(_rightHide != nullptr);
	_rightHide->setClickedCallback(std::move(hideCallback));
}

void TopBarSuggestionContent::setLeftPadding(rpl::producer<int> value) {
	std::move(value) | rpl::start_with_next([=](int padding) {
		_leftPadding = padding;
		update();
	}, lifetime());
}

const style::TextStyle & TopBarSuggestionContent::contentTitleSt() const {
	return _contentTitleSt;
}

} // namespace Dialogs
