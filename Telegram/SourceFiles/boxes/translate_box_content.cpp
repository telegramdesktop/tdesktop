/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/translate_box_content.h"

#include "lang/lang_keys.h"
#include "ui/boxes/choose_language_box.h"
#include "ui/effects/loading_element.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/power_saving.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_layers.h"

namespace Ui {
namespace {

class ShowButton final : public RpWidget {
public:
	ShowButton(not_null<Ui::RpWidget*> parent);

	[[nodiscard]] rpl::producer<Qt::MouseButton> clicks() const;

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	LinkButton _button;

};

ShowButton::ShowButton(not_null<Ui::RpWidget*> parent)
: RpWidget(parent)
, _button(this, tr::lng_usernames_activate_confirm(tr::now)) {
	_button.sizeValue(
	) | rpl::on_next([=](const QSize &s) {
		resize(
			s.width() + st::defaultEmojiSuggestions.fadeRight.width(),
			s.height());
		_button.moveToRight(0, 0);
	}, lifetime());
	_button.show();
}

void ShowButton::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	const auto clip = e->rect();

	const auto &icon = st::defaultEmojiSuggestions.fadeRight;
	const auto fade = QRect(0, 0, icon.width(), height());
	if (fade.intersects(clip)) {
		icon.fill(p, fade);
	}
	const auto fill = clip.intersected(
		{ icon.width(), 0, width() - icon.width(), height() });
	if (!fill.isEmpty()) {
		p.fillRect(fill, st::boxBg);
	}
}

rpl::producer<Qt::MouseButton> ShowButton::clicks() const {
	return _button.clicks();
}

} // namespace

void TranslateBoxContent(
		not_null<GenericBox*> box,
		TranslateBoxContentArgs &&args) {
	box->setWidth(st::boxWideWidth);
	box->addButton(tr::lng_box_ok(), [=] { box->closeBox(); });
	const auto container = box->verticalLayout();

	const auto text = std::move(args.text);
	const auto hasCopyRestriction = args.hasCopyRestriction;
	const auto textContext = std::move(args.textContext);
	const auto chooseTo = std::make_shared<Fn<void()>>(
		std::move(args.chooseTo));
	const auto request = std::make_shared<
		Fn<void(LanguageId, Fn<void(TranslateBoxContentResult)>)>>(
			std::move(args.request));

	auto to = std::move(args.to) | rpl::start_spawning(box->lifetime());
	const auto toTitle = rpl::duplicate(to) | rpl::map(LanguageName);
	const auto toDirection = rpl::duplicate(to) | rpl::map([=](
			LanguageId id) {
		return id.locale().textDirection() == Qt::RightToLeft;
	});

	const auto &stLabel = st::aboutLabel;
	const auto lineHeight = stLabel.style.lineHeight;

	Ui::AddSkip(container);

	const auto animationsPaused = [] {
		using Which = FlatLabel::WhichAnimationsPaused;
		const auto emoji = On(PowerSaving::kEmojiChat);
		const auto spoiler = On(PowerSaving::kChatSpoiler);
		return emoji
			? (spoiler ? Which::All : Which::CustomEmoji)
			: (spoiler ? Which::Spoiler : Which::None);
	};
	const auto original = box->addRow(object_ptr<SlideWrap<FlatLabel>>(
		box,
		object_ptr<FlatLabel>(box, stLabel)));
	{
		if (hasCopyRestriction) {
			original->entity()->setContextMenuHook([](auto&&) {
			});
		}
		original->entity()->setAnimationsPausedCallback(animationsPaused);
		original->entity()->setMarkedText(text, textContext);
		original->setMinimalHeight(lineHeight);
		original->hide(anim::type::instant);

		const auto show = Ui::CreateChild<FadeWrap<ShowButton>>(
			container.get(),
			object_ptr<ShowButton>(container));
		show->hide(anim::type::instant);
		rpl::combine(
			container->widthValue(),
			original->geometryValue()
		) | rpl::on_next([=](int width, const QRect &rect) {
			show->moveToLeft(
				width - show->width() - st::boxRowPadding.right(),
				rect.y() + std::abs(lineHeight - show->height()) / 2);
		}, show->lifetime());
		container->widthValue(
		) | rpl::filter([](int width) {
			return width > 0;
		}) | rpl::take(1) | rpl::on_next([=](int width) {
			if (original->entity()->textMaxWidth()
				> (width - rect::m::sum::h(st::boxRowPadding))) {
				show->show(anim::type::instant);
			}
		}, show->lifetime());
		show->toggleOn(show->entity()->clicks() | rpl::map_to(false));
		original->toggleOn(show->entity()->clicks() | rpl::map_to(true));
	}
	Ui::AddSkip(container);
	Ui::AddSkip(container);
	Ui::AddDivider(container);
	Ui::AddSkip(container);

	{
		const auto padding = st::defaultSubsectionTitlePadding;
		const auto subtitle = Ui::AddSubsectionTitle(container, std::move(toTitle));

		rpl::duplicate(to) | rpl::on_next([=] {
			subtitle->resizeToWidth(container->width()
				- padding.left()
				- padding.right());
		}, subtitle->lifetime());
	}

	const auto translated = box->addRow(object_ptr<SlideWrap<FlatLabel>>(
		box,
		object_ptr<FlatLabel>(box, stLabel)));
	translated->entity()->setSelectable(!hasCopyRestriction);
	translated->entity()->setAnimationsPausedCallback(animationsPaused);

	constexpr auto kMaxLines = 3;
	container->resizeToWidth(box->width());
	const auto loading = box->addRow(object_ptr<SlideWrap<RpWidget>>(
		box,
		CreateLoadingTextWidget(
			box,
			st::aboutLabel.style,
			std::min(original->entity()->height() / lineHeight, kMaxLines),
			std::move(toDirection))));

	struct State {
		int requestId = 0;
	};
	const auto state = box->lifetime().make_state<State>();

	const auto showText = [=](TranslateBoxContentResult result) {
		using UiError = TranslateBoxContentError;
		auto value = result.text.value_or(
			tr::italic(((result.error == UiError::LocalLanguagePackMissing)
				? tr::lng_translate_box_error_language_pack_not_installed
				: tr::lng_translate_box_error)(tr::now)));
		translated->entity()->setMarkedText(value, textContext);
		translated->show(anim::type::instant);
		loading->hide(anim::type::instant);
	};
	const auto send = [=](LanguageId id) {
		const auto requestId = ++state->requestId;
		loading->show(anim::type::instant);
		translated->hide(anim::type::instant);
		(*request)(id, [=](TranslateBoxContentResult result) {
			if (state->requestId != requestId) {
				return;
			}
			showText(std::move(result));
		});
	};
	std::move(to) | rpl::on_next(send, box->lifetime());

	box->addLeftButton(tr::lng_settings_language(), [=] {
		if (loading->toggled()) {
			return;
		}
		(*chooseTo)();
	});
}

} // namespace Ui
