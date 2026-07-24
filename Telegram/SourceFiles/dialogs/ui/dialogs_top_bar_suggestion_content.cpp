/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/ui/dialogs_top_bar_suggestion_content.h"

#include "base/call_delayed.h"
#include "data/data_authorization.h"
#include "dialogs/ui/dialogs_pill.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "settings/settings_common.h"
#include "ui/effects/animation_value.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "ui/rect.h"
#include "ui/round_rect.h"
#include "ui/text/format_values.h"
#include "ui/text/text_custom_emoji.h"
#include "ui/ui_rpl_filter.h"
#include "ui/ui_utility.h"
#include "ui/unread_badge_paint.h"
#include "ui/vertical_list.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/elastic_scroll.h"
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
#include "styles/style_window.h"

namespace Dialogs {
namespace {

[[nodiscard]] QString FormatUnconfirmedAuthMessage(
		const std::vector<Data::UnreviewedAuth> &list) {
	if (list.empty()) {
		return QString();
	} else if (list.size() == 1) {
		const auto &auth = list.at(0);
		return tr::lng_unconfirmed_auth_single(
			tr::now,
			lt_from,
			auth.device,
			lt_country,
			auth.location);
	}
	auto commonLocation = list.at(0).location;
	for (auto i = 1; i < list.size(); ++i) {
		if (commonLocation != list.at(i).location) {
			commonLocation.clear();
			break;
		}
	}
	if (commonLocation.isEmpty()) {
		return tr::lng_unconfirmed_auth_multiple(
			tr::now,
			lt_count,
			list.size());
	}
	return tr::lng_unconfirmed_auth_multiple_from(
		tr::now,
		lt_count,
		list.size(),
		lt_country,
		commonLocation);
}

[[nodiscard]] bool TopBarSuggestionNarrow(int width) {
	return (width < st::columnMinimalWidthLeft / 2);
}

} // namespace

void PaintBottomFade(
		QPainter &p,
		int outerWidth,
		int fadeHeight,
		style::color bg) {
	if (fadeHeight <= 0) {
		return;
	}
	auto transparent = bg->c;
	transparent.setAlpha(0);
	auto grad = QLinearGradient(0, 0, 0, fadeHeight);
	grad.setColorAt(0, transparent);
	grad.setColorAt(1, bg->c);
	p.fillRect(QRect(0, 0, outerWidth, fadeHeight), grad);
}

int PillRadius() {
	const auto padding = st::msgReplyPadding.top();
	return (padding
		+ st::semiboldTextStyle.font->height
		+ st::dialogsTopBarSuggestionAboutStyle.font->height
		+ padding) / 2;
}

int PaintSuggestionBubbleBackground(
		QPainter &p,
		QRect outer,
		const Ui::BoxShadow &shadow,
		int cornerRadius) {
	const auto &margins = st::dialogsTopBarSuggestionMargins;
	const auto pill = outer - margins;
	PaintTopFade(
		p,
		outer.width(),
		margins.top() + pill.height() / 2,
		st::dialogsBg->c);
	if (pill.isEmpty()) {
		return 0;
	}
	const auto radius = std::min({
		cornerRadius ? cornerRadius : PillRadius(),
		pill.width() / 2,
		pill.height() / 2,
	});
	shadow.paint(p, pill, radius);
	auto hq = PainterHighQualityEnabler(p);
	p.setBrush(st::dialogsBg);
	p.setPen(Qt::NoPen);
	p.drawRoundedRect(pill, radius, radius);
	PaintPillOutline(p, pill, radius);
	return radius;
}

UnconfirmedAuthWrap::UnconfirmedAuthWrap(
	not_null<Ui::RpWidget*> parent,
	object_ptr<Ui::VerticalLayout> &&child)
: Ui::SlideWrap<Ui::VerticalLayout>(parent, std::move(child))
, _shadow(st::dialogsTopBarSuggestionShadow) {
	paintRequest() | rpl::on_next([=] {
		if (!_collapseSnapshot.isNull()) {
			auto p = QPainter(this);
			p.drawPixmap(0, 0, _collapseSnapshot);
		}
	}, lifetime());
}

rpl::producer<int> UnconfirmedAuthWrap::desiredHeightValue() const {
	return entity()->heightValue();
}

void UnconfirmedAuthWrap::setCollapseProgress(
		rpl::producer<float64> progress) {
	std::move(progress) | rpl::on_next([=](float64 value) {
		if (_collapseProgress == value) {
			return;
		}
		_collapseProgress = value;
		if (value == 0.) {
			releaseCollapseSnapshot();
		}
		resizeToWidth(width());
		update();
	}, lifetime());
}

void UnconfirmedAuthWrap::prepareCollapseSnapshot() {
	_collapseSnapshot = Ui::GrabWidget(this);
	if (const auto w = wrapped()) {
		w->hide();
	}
	update();
}

void UnconfirmedAuthWrap::releaseCollapseSnapshot() {
	if (_collapseSnapshot.isNull()) {
		return;
	}
	_collapseSnapshot = QPixmap();
	if (const auto w = wrapped()) {
		w->show();
	}
}

int UnconfirmedAuthWrap::resizeGetHeight(int newWidth) {
	if (!_collapseSnapshot.isNull()) {
		const auto fullHeight = int(_collapseSnapshot.height()
			/ _collapseSnapshot.devicePixelRatio());
		return int(base::SafeRound(
			fullHeight * (1. - _collapseProgress)));
	}
	const auto w = wrapped();
	if (TopBarSuggestionNarrow(newWidth)) {
		if (w) {
			w->hide();
		}
		return 0;
	}
	if (w) {
		w->show();
		w->resizeToWidth(newWidth);
	}
	return w ? w->height() : 0;
}

not_null<UnconfirmedAuthWrap*> CreateUnconfirmedAuthContent(
		not_null<Ui::RpWidget*> parent,
		rpl::producer<std::vector<Data::UnreviewedAuth>> list,
		Fn<void(bool)> callback,
		rpl::producer<float64> collapseProgress) {
	const auto wrap = Ui::CreateChild<UnconfirmedAuthWrap>(
		parent,
		object_ptr<Ui::VerticalLayout>(parent));
	wrap->setCollapseProgress(std::move(collapseProgress));
	const auto content = wrap->entity();
	const auto &margins = st::dialogsTopBarSuggestionMargins;
	content->paintOn([=](QPainter &p) {
		PaintSuggestionBubbleBackground(p, content->rect(), wrap->shadow());
	});

	const auto &basePadding = st::dialogsUnconfirmedAuthPadding;
	const auto padding = QMargins(
		margins.left() + basePadding.left(),
		basePadding.top(),
		margins.right() + basePadding.right(),
		basePadding.bottom());

	Ui::AddSkip(content, margins.top());
	Ui::AddSkip(content);

	content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			tr::lng_unconfirmed_auth_title(),
			st::dialogsUnconfirmedAuthTitle),
		padding,
		style::al_top);

	Ui::AddSkip(content);

	content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			std::move(list) | rpl::map(FormatUnconfirmedAuthMessage),
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
	Ui::AddSkip(content, margins.bottom());

	return wrap;
}

TopBarSuggestionContent::TopBarSuggestionContent(
	not_null<Ui::RpWidget*> parent,
	Fn<bool()> emojiPaused)
: Ui::RippleButton(parent, st::defaultRippleAnimationBgOver)
, _titleSt(st::semiboldTextStyle)
, _contentTitleSt(st::dialogsTopBarSuggestionTitleStyle)
, _contentTextSt(st::dialogsTopBarSuggestionAboutStyle)
, _shadow(st::dialogsTopBarSuggestionShadow)
, _emojiPaused(std::move(emojiPaused)) {
	_leftPadding = st::dialogsTopBarLeftPadding;
	setRightIcon(RightIcon::Close);
	Ui::AbstractButton::setClickedCallback([=] {
		if (TopBarSuggestionNarrow(width())) {
			if (_narrowExpandCallback) {
				_narrowExpandCallback();
			}
		} else if (_suggestionClickCallback) {
			_suggestionClickCallback();
		}
	});
}

void TopBarSuggestionContent::setClickedCallback(Fn<void()> callback) {
	_suggestionClickCallback = std::move(callback);
}

void TopBarSuggestionContent::setNarrowExpandCallback(Fn<void()> callback) {
	_narrowExpandCallback = std::move(callback);
}

void TopBarSuggestionContent::setRightIcon(RightIcon icon) {
	_rightButton = nullptr;
	if (icon == _rightIcon) {
		return;
	}
	_rightHide = nullptr;
	_rightArrow = nullptr;
	_rightIcon = icon;
	const auto &margins = st::dialogsTopBarSuggestionMargins;
	if (icon == RightIcon::Close) {
		_rightHide = base::make_unique_q<Ui::IconButton>(
			this,
			st::dialogsCancelSearchInPeer);
		const auto rightHide = _rightHide.get();
		sizeValue() | rpl::filter_size(
		) | rpl::on_next([=](const QSize &s) {
			const auto &button = st::dialogsCancelSearchInPeer;
			const auto padding = PillRadius()
				- button.rippleAreaSize / 2;
			const auto pillHeight = s.height() - rect::m::sum::v(margins);
			rightHide->moveToRight(
				margins.right() + padding - button.rippleAreaPosition.x(),
				margins.top()
					+ (pillHeight - button.rippleAreaSize) / 2
					- button.rippleAreaPosition.y());
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
			const auto pillRight = s.width() - margins.right();
			const auto pillHeight = s.height() - rect::m::sum::v(margins);
			arrow->moveToLeft(
				pillRight - arrow->width(),
				margins.top()
					+ point.y()
					+ (pillHeight - arrow->height()) / 2);
		}, arrow->lifetime());
		arrow->show();
	}
	resizeToWidth(width());
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
	const auto &margins = st::dialogsTopBarSuggestionMargins;
	rpl::combine(
		sizeValue(),
		_rightButton->sizeValue()
	) | rpl::on_next([=](QSize outer, QSize inner) {
		const auto cardHeight = _geometry.cardInnerHeight
			? _geometry.cardInnerHeight
			: (outer.height() - rect::m::sum::v(margins));
		const auto verticalGap = (cardHeight - inner.height()) / 2;
		const auto rightInset = _geometry.rightInset
			? _geometry.rightInset
			: (margins.right() + verticalGap);
		_rightButton->moveToRight(
			rightInset,
			margins.top() + verticalGap,
			outer.width());
	}, _rightButton->lifetime());
	_rightButton->setFullRadius(true);
	_rightButton->setClickedCallback(std::move(callback));
	_rightButton->show();
}

void TopBarSuggestionContent::setRightBadge(rpl::producer<int> count) {
	_rightButton = nullptr;
	_rightHide = nullptr;
	_rightArrow = nullptr;
	_rightIcon = RightIcon::None;
	_rightBadgeLifetime.destroy();
	std::move(count) | rpl::on_next([=](int value) {
		const auto text = QString::number(value);
		if (_rightBadgeText == text && !_rightBadgeSize.isEmpty()) {
			return;
		}
		_rightBadgeText = text;
		auto st = Ui::UnreadBadgeStyle();
		_rightBadgeSize = Ui::CountUnreadBadgeSize(_rightBadgeText, st);
		resizeToWidth(width());
		update();
	}, _rightBadgeLifetime);
}

void TopBarSuggestionContent::draw(QPainter &p) {
	const auto outer = Ui::RpWidget::rect();
	const auto setControlsVisible = [&](bool visible) {
		const auto widgets = std::array<Ui::RpWidget*, 4>{
			_leadingWidget.data(),
			_rightHide.get(),
			_rightArrow.get(),
			_rightButton.get(),
		};
		for (const auto widget : widgets) {
			if (widget) {
				widget->setVisible(visible);
			}
		}
	};
	if (TopBarSuggestionNarrow(width())) {
		setControlsVisible(false);
		const auto &margins = st::dialogsTopBarSuggestionMargins;
		const auto pill = outer - margins;
		const auto radius = PaintSuggestionBubbleBackground(
			p,
			outer,
			_shadow,
			_geometry.cornerRadius);
		if (pill.isEmpty()) {
			return;
		}
		auto clipPath = QPainterPath();
		clipPath.addRoundedRect(pill, radius, radius);
		p.setClipPath(clipPath);
		Ui::RippleButton::paintRipple(p, 0, 0);
		p.setClipping(false);
		const auto accentSide = st::dialogsRequestsBubbleIconSize;
		const auto accent = QRect(
			pill.x() + (pill.width() - accentSide) / 2,
			pill.y() + (pill.height() - accentSide) / 2,
			accentSide,
			accentSide);
		auto hq = PainterHighQualityEnabler(p);
		auto background = Ui::RoundRect(
			st::dialogsRequestsBubbleIconRadius,
			st::dialogsRequestsBubbleIconBg);
		background.paint(p, accent);
		st::dialogsRequestsBubbleIcon.paintInCenter(p, accent);
		return;
	}
	setControlsVisible(true);
	const auto &margins = st::dialogsTopBarSuggestionMargins;
	const auto pill = outer - margins;

	const auto radius = PaintSuggestionBubbleBackground(
		p,
		outer,
		_shadow,
		_geometry.cornerRadius);
	if (pill.isEmpty()) {
		return;
	}

	auto clipPath = QPainterPath();
	clipPath.addRoundedRect(pill, radius, radius);
	p.setClipPath(clipPath);
	Ui::RippleButton::paintRipple(p, 0, 0);
	p.setClipping(false);

	const auto leftPadding = _leftPadding + margins.left();
	const auto rightPadding = margins.right();
	const auto centeredTop = margins.top()
		+ (_geometry.cardInnerHeight - _contentTitleSt.font->height) / 2;
	const auto topPadding = _geometry.centerSingleLineTitle
		? centeredTop
		: (st::msgReplyPadding.top() + margins.top());
	const auto availableWidthNoPhoto = outer.width()
		- (_rightArrow
			? (_rightArrow->width() / 4 * 3) // Takes full height.
			: 0)
		- leftPadding
		- rightPadding;
	const auto availableWidth = availableWidthNoPhoto
		- (_rightHide ? _rightHide->width() : 0)
		- (_rightBadgeText.isEmpty() ? 0 : _rightBadgeSize.width());
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
		const auto lineHeight = _contentTextSt.font->height;
		const auto lineLayout = [=](int line) -> Ui::Text::LineGeometry {
			line++;
			const auto diff = (st::sponsoredMessageBarMaxHeight)
				- line * lineHeight;
			if (diff < 3 * lineHeight) {
				return {
					.width = availableWidth,
					.elided = true,
				};
			} else if (diff < 2 * lineHeight) {
				return {};
			}
			return { .width = availableWidth };
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
	}
	if (!_rightBadgeText.isEmpty()) {
		auto st = Ui::UnreadBadgeStyle();
		const auto rightInset = _geometry.rightInset
			? _geometry.rightInset
			: (margins.right()
				+ (_geometry.cardInnerHeight - st.size) / 2);
		const auto badgeRight = outer.width() - rightInset;
		const auto badgeTop = margins.top()
			+ (_geometry.cardInnerHeight - st.size) / 2;
		Ui::PaintUnreadBadge(p, _rightBadgeText, badgeRight, badgeTop, st);
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
	resizeToWidth(width());
	update();
}

void TopBarSuggestionContent::paintEvent(QPaintEvent *) {
	auto p = QPainter(this);
	if (!_collapseSnapshot.isNull()) {
		p.drawPixmap(0, 0, _collapseSnapshot);
		return;
	}
	draw(p);
}

void TopBarSuggestionContent::prepareCollapseSnapshot() {
	_collapseSnapshot = Ui::GrabWidget(this);
	for (const auto child : children()) {
		if (const auto widget = qobject_cast<QWidget*>(child)) {
			widget->hide();
		}
	}
	update();
}

void TopBarSuggestionContent::releaseCollapseSnapshot() {
	if (_collapseSnapshot.isNull()) {
		return;
	}
	_collapseSnapshot = QPixmap();
	for (const auto child : children()) {
		if (const auto widget = qobject_cast<QWidget*>(child)) {
			widget->show();
		}
	}
}

int TopBarSuggestionContent::resizeGetHeight(int newWidth) {
	if (!_collapseSnapshot.isNull()) {
		const auto fullHeight = int(_collapseSnapshot.height()
			/ _collapseSnapshot.devicePixelRatio());
		return int(base::SafeRound(
			fullHeight * (1. - _collapseProgress)));
	}
	if (TopBarSuggestionNarrow(newWidth)) {
		const auto &cardMargins = st::dialogsTopBarSuggestionMargins;
		const auto inner = _geometry.cardInnerHeight
			? _geometry.cardInnerHeight
			: st::defaultDialogRow.photoSize;
		const auto withMargins = inner + rect::m::sum::v(cardMargins);
		return int(base::SafeRound(
			withMargins * (1. - _collapseProgress)));
	}
	const auto &margins = st::dialogsTopBarSuggestionMargins;
	if (_geometry.centerSingleLineTitle && _geometry.cardInnerHeight) {
		const auto withMargins = _geometry.cardInnerHeight
			+ rect::m::sum::v(margins);
		return int(base::SafeRound(
			withMargins * (1. - _collapseProgress)));
	}
	const auto topPadding = st::msgReplyPadding.top();
	const auto bottomPadding = st::msgReplyPadding.top();
	const auto availableWidthNoPhoto = newWidth
		- rect::m::sum::h(margins)
		- (_rightArrow
			? (_rightArrow->width() / 4 * 3) // Takes full height.
			: 0)
		- _leftPadding;
	const auto availableWidth = availableWidthNoPhoto
		- (_rightHide ? _rightHide->width() : 0);
	if (availableWidth <= 0) {
		return topPadding + bottomPadding + rect::m::sum::v(margins);
	}
	const auto hasSecondLineTitle
		= (availableWidth < _contentTitle.maxWidth());
	const auto textTop = hasSecondLineTitle
		? (topPadding
			+ _titleSt.font->height
			+ _contentTitleSt.font->height)
		: (topPadding + _titleSt.font->height);

	const auto lineHeight = _contentTextSt.font->height;
	auto lineLayout = [=](int line) -> Ui::Text::LineGeometry {
		line++;
		const auto diff = (st::sponsoredMessageBarMaxHeight)
			- line * lineHeight;
		if (diff < 3 * lineHeight) {
			return {
				.width = availableWidth,
				.elided = true,
			};
		} else if (diff < 2 * lineHeight) {
			return {};
		}
		return { .width = availableWidth };
	};
	const auto dims = _contentText.countDimensions(
		Ui::Text::GeometryDescriptor{ .layout = std::move(lineLayout) });
	const auto natural = textTop + dims.height + bottomPadding;
	const auto capped = std::min(
		natural,
		st::sponsoredMessageBarMaxHeight);
	const auto withMargins = capped + rect::m::sum::v(margins);
	return int(base::SafeRound(withMargins * (1. - _collapseProgress)));
}

void TopBarSuggestionContent::setCollapseProgress(
		rpl::producer<float64> progress) {
	std::move(progress) | rpl::on_next([=](float64 value) {
		if (_collapseProgress == value) {
			return;
		}
		_collapseProgress = value;
		if (value == 0.) {
			releaseCollapseSnapshot();
		}
		resizeToWidth(width());
		update();
	}, lifetime());
}

void TopBarSuggestionContent::setHideCallback(Fn<void()> hideCallback) {
	Expects(_rightHide != nullptr);
	_rightHide->setClickedCallback(std::move(hideCallback));
}

void TopBarSuggestionContent::setLeadingWidget(Ui::RpWidget *widget) {
	_leadingWidgetLifetime.destroy();
	if (_leadingWidget && _leadingWidget != widget) {
		_leadingWidget->deleteLater();
	}
	_leadingWidget = widget;
	const auto basePadding = st::dialogsTopBarLeftPadding;
	if (!widget) {
		if (_leftPadding != basePadding) {
			_leftPadding = basePadding;
			resizeToWidth(width());
			update();
		}
		return;
	}
	widget->setParent(this);
	widget->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto &margins = st::dialogsTopBarSuggestionMargins;
	const auto &row = st::defaultDialogRow;
	sizeValue() | rpl::filter_size(
	) | rpl::on_next([=](const QSize &s) {
		widget->raise();
		widget->show();
		const auto cardHeight = _geometry.cardInnerHeight
			? _geometry.cardInnerHeight
			: (s.height() - rect::m::sum::v(margins));
		const auto leftInset = _geometry.iconLeft
			? _geometry.iconLeft
			: (row.padding.left()
				+ (row.photoSize - widget->width()) / 2);
		widget->moveToLeft(
			leftInset,
			margins.top() + (cardHeight - widget->height()) / 2);
	}, _leadingWidgetLifetime);
	const auto padding = (_geometry.leadingTextSkip
		? _geometry.leadingTextSkip
		: row.nameLeft) - margins.left();
	if (_leftPadding != padding) {
		_leftPadding = padding;
		resizeToWidth(width());
		update();
	}
}

void TopBarSuggestionContent::setGeometryOverride(
		TopBarSuggestionGeometry geometry) {
	_geometry = geometry;
	resizeToWidth(width());
	update();
}

const style::TextStyle & TopBarSuggestionContent::contentTitleSt() const {
	return _contentTitleSt;
}

void MountTopBarSuggestion(MountTopBarSuggestionArgs args) {
	const auto scroll = args.scroll;
	const auto innerList = args.innerList;
	const auto wrap = args.wrap;
	const auto placeholder = args.placeholder;
	const auto heightChanged = std::move(args.heightChanged);
	if (placeholder) {
		placeholder->reset(innerList->insert(
			0,
			object_ptr<Ui::RpWidget>(innerList)));
		const auto raw = placeholder->get();
		raw->paintOn([raw](QPainter &p) {
			p.fillRect(raw->rect(), st::dialogsBg);
		});
	}
	wrap->setParent(scroll);
	wrap->raise();
	wrap->setVisible(wrap->toggled() || wrap->animating());
	const auto lastHeight = wrap->entity()->lifetime().make_state<int>(-1);
	const auto syncHeight = [=] {
		const auto h = wrap->height();
		if (*lastHeight == h) {
			return;
		}
		*lastHeight = h;
		if (placeholder) {
			if (const auto raw = placeholder->get()) {
				raw->resize(raw->width(), h);
			}
		}
		scroll->setBarTopInset(h);
		if (heightChanged) {
			heightChanged(h);
		}
	};
	wrap->heightValue() | rpl::to_empty | rpl::on_next(
		syncHeight,
		wrap->entity()->lifetime());
	const auto pinToScroll = [=] {
		wrap->resizeToWidth(scroll->width());
		Ui::SendPendingMoveResizeEvents(wrap);
		wrap->moveToLeft(0, 0);
		syncHeight();
	};
	rpl::merge(
		scroll->sizeValue() | rpl::to_empty,
		wrap->toggledValue() | rpl::filter([](bool shown) {
			return shown;
		}) | rpl::to_empty
	) | rpl::on_next(pinToScroll, wrap->entity()->lifetime());
	pinToScroll();
}

not_null<Ui::RpWidget*> CreateRequestsBubbleIcon(
		not_null<Ui::RpWidget*> parent) {
	const auto result = Ui::CreateChild<Ui::RpWidget>(parent);
	result->setAttribute(Qt::WA_TransparentForMouseEvents);
	result->resize(
		st::dialogsRequestsBubbleIconSize,
		st::dialogsRequestsBubbleIconSize);
	const auto background = result->lifetime().make_state<Ui::RoundRect>(
		st::dialogsRequestsBubbleIconRadius,
		st::dialogsRequestsBubbleIconBg);
	result->paintOn([=](QPainter &p) {
		auto hq = PainterHighQualityEnabler(p);
		background->paint(p, result->rect());
		st::dialogsRequestsBubbleIcon.paintInCenter(p, result->rect());
	});
	return result;
}

} // namespace Dialogs
