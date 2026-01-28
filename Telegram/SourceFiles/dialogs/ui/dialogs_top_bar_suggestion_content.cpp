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
#include "lottie/lottie_icon.h"
#include "settings/settings_common.h"
#include "ui/effects/animation_value.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
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
#include "styles/style_chat_helpers.h"
#include "styles/style_chat.h"
#include "styles/style_dialogs.h"
#include "styles/style_layers.h"
#include "styles/style_premium.h"
#include "styles/style_settings.h"

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
	content->paintRequest() | rpl::on_next([=] {
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
	) | rpl::on_next([=](const QSize &s) {
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

void ShowAuthDeniedBox(
		not_null<Ui::GenericBox*> box,
		float64 count,
		const QString &messageText) {
	box->setStyle(st::showOrBox);
	box->setWidth(st::boxWideWidth);
	const auto buttonPadding = QMargins(
		st::showOrBox.buttonPadding.left(),
		0,
		st::showOrBox.buttonPadding.right(),
		0);
	auto icon = Settings::CreateLottieIcon(
		box,
		{
			.name = u"ban"_q,
			.sizeOverride = st::dialogsSuggestionDeniedAuthLottie,
		},
		st::dialogsSuggestionDeniedAuthLottieMargins);
	Settings::AddLottieIconWithCircle(
		box->verticalLayout(),
		std::move(icon.widget),
		st::settingsBlockedListIconPadding,
		st::dialogsSuggestionDeniedAuthLottieCircle);
	box->setShowFinishedCallback([=, animate = std::move(icon.animate)] {
		animate(anim::repeat::once);
	});
	Ui::AddSkip(box->verticalLayout());
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_unconfirmed_auth_denied_title(
				lt_count,
				rpl::single(count)),
			st::boostCenteredTitle),
		st::showOrTitlePadding + buttonPadding,
		style::al_top);
	Ui::AddSkip(box->verticalLayout());
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			messageText,
			st::boostText),
		st::showOrAboutPadding + buttonPadding,
		style::al_top);
	Ui::AddSkip(box->verticalLayout());
	const auto warning = box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_unconfirmed_auth_denied_warning(tr::bold),
			st::boostText),
		st::showOrAboutPadding + buttonPadding
			+ QMargins(st::boostTextSkip, 0, st::boostTextSkip, 0),
		style::al_top);
	warning->setTextColorOverride(st::attentionButtonFg->c);
	const auto warningBg = Ui::CreateChild<Ui::RpWidget>(
		box->verticalLayout());
	warning->geometryValue() | rpl::on_next([=](QRect r) {
		warningBg->setGeometry(r + Margins(st::boostTextSkip));
	}, warningBg->lifetime());
	warningBg->paintOn([=](QPainter &p) {
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::attentionButtonBgOver);
		p.drawRoundedRect(
			warningBg->rect(),
			st::buttonRadius,
			st::buttonRadius);
	});
	warningBg->show();
	warning->raise();
	warningBg->stackUnder(warning);
	const auto confirm = box->addButton(
		object_ptr<Ui::RoundButton>(
			box,
			rpl::single(QString()),
			st::defaultActiveButton));
	confirm->setClickedCallback([=] {
		box->closeBox();
	});
	confirm->resize(
		st::showOrShowButton.width,
		st::showOrShowButton.height);

	const auto textLabel = Ui::CreateChild<Ui::FlatLabel>(
		confirm,
		tr::lng_archive_hint_button(),
		st::defaultSubsectionTitle);
	textLabel->setTextColorOverride(st::defaultActiveButton.textFg->c);
	textLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

	const auto timerLabel = Ui::CreateChild<Ui::FlatLabel>(
		confirm,
		rpl::single(QString()),
		st::defaultSubsectionTitle);
	timerLabel->setTextColorOverride(
		anim::with_alpha(st::defaultActiveButton.textFg->c, 0.75));
	timerLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

	constexpr auto kTimer = 5;
	const auto remaining = confirm->lifetime().make_state<int>(kTimer);
	const auto timerLifetime
		= confirm->lifetime().make_state<rpl::lifetime>();
	const auto timer = timerLifetime->make_state<base::Timer>([=] {
		if ((*remaining) > 0) {
			timerLabel->setText(QString::number((*remaining)--));
		} else {
			timerLabel->hide();
			confirm->setAttribute(Qt::WA_TransparentForMouseEvents, false);
			box->setCloseByEscape(true);
			box->setCloseByOutsideClick(true);
			timerLifetime->destroy();
		}
	});
	box->setCloseByEscape(false);
	box->setCloseByOutsideClick(false);
	confirm->setAttribute(Qt::WA_TransparentForMouseEvents, true);
	timerLabel->setText(QString::number((*remaining)));
	timer->callEach(1000);

	rpl::combine(
		confirm->sizeValue(),
		textLabel->sizeValue(),
		timerLabel->sizeValue(),
		timerLabel->shownValue()
	) | rpl::on_next([=](QSize btn, QSize text, QSize timer, bool shown) {
		const auto skip = st::normalFont->spacew;
		const auto totalWidth = shown
			? (text.width() + skip + timer.width())
			: text.width();
		const auto left = (btn.width() - totalWidth) / 2;
		textLabel->moveToLeft(left, (btn.height() - text.height()) / 2);
		timerLabel->moveToLeft(
			left + text.width() + skip,
			(btn.height() - timer.height()) / 2);
	}, confirm->lifetime());
}

TopBarSuggestionContent::TopBarSuggestionContent(
	not_null<Ui::RpWidget*> parent,
	Fn<bool()> emojiPaused)
: Ui::RippleButton(parent, st::defaultRippleAnimationBgOver)
, _titleSt(st::semiboldTextStyle)
, _contentTitleSt(st::dialogsTopBarSuggestionTitleStyle)
, _contentTextSt(st::dialogsTopBarSuggestionAboutStyle)
, _emojiPaused(std::move(emojiPaused)) {
	setRightIcon(RightIcon::Close);
}

void TopBarSuggestionContent::setRightIcon(RightIcon icon) {
	_rightButton = nullptr;
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
		) | rpl::on_next([=](const QSize &s) {
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
		) | rpl::on_next([=](const QSize &s) {
			const auto &point = st::settingsPremiumArrowShift;
			arrow->moveToLeft(
				s.width() - arrow->width(),
				point.y() + (s.height() - arrow->height()) / 2);
		}, arrow->lifetime());
		arrow->show();
	}
}

void TopBarSuggestionContent::setRightButton(
		rpl::producer<TextWithEntities> text,
		Fn<void()> callback) {
	_rightHide = nullptr;
	_rightArrow = nullptr;
	_rightIcon = RightIcon::None;
	if (!text) {
		_rightButton = nullptr;
		return;
	}
	using namespace Ui;
	_rightButton = base::make_unique_q<RoundButton>(
		this,
		rpl::single(QString()),
		st::dialogsTopBarRightButton);
	_rightButton->setText(std::move(text));
	rpl::combine(
		sizeValue(),
		_rightButton->sizeValue()
	) | rpl::on_next([=](QSize outer, QSize inner) {
		const auto top = (outer.height() - inner.height()) / 2;
		_rightButton->moveToRight(top, top, outer.width());
	}, _rightButton->lifetime());
	_rightButton->setFullRadius(true);
	_rightButton->setTextTransform(RoundButton::TextTransform::NoTransform);
	_rightButton->setClickedCallback(std::move(callback));
	_rightButton->show();
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
	const auto paused = On(PowerSaving::kEmojiChat)
		|| (_emojiPaused && _emojiPaused());
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
			.pausedEmoji = paused,
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
		p.setPen(_descriptionColorOverride.value_or(st::windowSubTextFg->c));
		_contentText.draw(p, {
			.position = QPoint(left, top),
			.outerWidth = availableWidth,
			.availableWidth = availableWidth,
			.geometry = Ui::Text::GeometryDescriptor{
				.layout = std::move(lineLayout),
			},
			.pausedEmoji = paused,
		});
		_lastPaintedContentTop = top;
		_lastPaintedContentLineAmount = lastContentLineAmount;
	}
}

void TopBarSuggestionContent::setContent(
		TextWithEntities title,
		TextWithEntities description,
		std::optional<Ui::Text::MarkedContext> context,
		std::optional<QColor> descriptionColorOverride) {
	_descriptionColorOverride = descriptionColorOverride;
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
	update();
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
	std::move(value) | rpl::on_next([=](int padding) {
		_leftPadding = padding;
		update();
	}, lifetime());
}

const style::TextStyle & TopBarSuggestionContent::contentTitleSt() const {
	return _contentTitleSt;
}

} // namespace Dialogs
