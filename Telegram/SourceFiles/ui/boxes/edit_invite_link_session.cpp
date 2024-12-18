/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/boxes/edit_invite_link_session.h"

#include "data/components/credits.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/stickers/data_custom_emoji.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "ui/boxes/edit_invite_link.h" // InviteLinkSubscriptionToggle
#include "ui/effects/credits_graphics.h"
#include "ui/layers/generic_box.h"
#include "ui/rect.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/number_input.h"
#include "ui/widgets/label_with_custom_emoji.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_channel_earn.h"
#include "styles/style_chat.h"
#include "styles/style_settings.h"
#include "styles/style_layers.h"
#include "styles/style_info.h"

namespace Ui {

InviteLinkSubscriptionToggle FillCreateInviteLinkSubscriptionToggle(
		not_null<Ui::GenericBox*> box,
		not_null<PeerData*> peer) {
	struct State final {
		rpl::variable<float64> usdRate = 0;
	};
	const auto state = box->lifetime().make_state<State>();
	const auto currency = u"USD"_q;

	const auto container = box->verticalLayout();
	const auto toggle = container->add(
		object_ptr<SettingsButton>(
			container,
			tr::lng_group_invite_subscription(),
			st::settingsButtonNoIconLocked),
		style::margins{ 0, 0, 0, st::defaultVerticalListSkip });

	const auto maxCredits = peer->session().appConfig().get<int>(
		u"stars_subscription_amount_max"_q,
		2500);

	const auto &st = st::inviteLinkCreditsField;
	const auto skip = st.textMargins.top() / 2;
	const auto wrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	box->setShowFinishedCallback([=] {
		wrap->toggleOn(toggle->toggledValue());
		wrap->finishAnimating();
	});
	const auto inputContainer = wrap->entity()->add(
		CreateSkipWidget(container, st.heightMin - skip));
	const auto input = CreateChild<NumberInput>(
		inputContainer,
		st,
		tr::lng_group_invite_subscription_ph(),
		QString(),
		std::pow(QString::number(maxCredits).size(), 10));
	wrap->toggledValue() | rpl::start_with_next([=](bool shown) {
		if (shown) {
			input->setFocus();
		}
	}, input->lifetime());
	const auto icon = CreateSingleStarWidget(
		inputContainer,
		st.style.font->height);
	const auto priceOverlay = Ui::CreateChild<Ui::RpWidget>(inputContainer);
	priceOverlay->setAttribute(Qt::WA_TransparentForMouseEvents);
	inputContainer->sizeValue(
	) | rpl::start_with_next([=](const QSize &size) {
		input->resize(
			size.width() - rect::m::sum::h(st::boxRowPadding),
			st.heightMin);
		input->moveToLeft(st::boxRowPadding.left(), -skip);
		icon->moveToLeft(
			st::boxRowPadding.left(),
			input->pos().y() + st.textMargins.top());
		priceOverlay->resize(size);
	}, input->lifetime());
	ToggleChildrenVisibility(inputContainer, true);
	QObject::connect(input, &Ui::MaskedInputField::changed, [=] {
		const auto amount = input->getLastText().toDouble();
		if (amount > maxCredits) {
			input->setText(QString::number(maxCredits));
		}
		priceOverlay->update();
	});
	priceOverlay->paintRequest(
	) | rpl::start_with_next([=, right = st::boxRowPadding.right()] {
		if (state->usdRate.current() <= 0) {
			return;
		}
		const auto amount = input->getLastText().toDouble();
		if (amount <= 0) {
			return;
		}
		const auto text = tr::lng_group_invite_subscription_price(
			tr::now,
			lt_cost,
			Ui::FillAmountAndCurrency(
				amount * state->usdRate.current(),
				currency));
		auto p = QPainter(priceOverlay);
		p.setFont(st.placeholderFont);
		p.setPen(st.placeholderFg);
		p.setBrush(Qt::NoBrush);
		const auto m = QMargins(0, skip, right, 0);
		p.drawText(priceOverlay->rect() - m, text, style::al_right);
	}, priceOverlay->lifetime());

	state->usdRate = peer->session().credits().rateValue(peer);

	const auto arrow = Ui::Text::SingleCustomEmoji(
		peer->owner().customEmojiManager().registerInternalEmoji(
			st::topicButtonArrow,
			st::channelEarnLearnArrowMargins,
			true));
	auto about = Ui::CreateLabelWithCustomEmoji(
		container,
		tr::lng_group_invite_subscription_about(
			lt_link,
			tr::lng_group_invite_subscription_about_link(
				lt_emoji,
				rpl::single(arrow),
				Ui::Text::RichLangValue
			) | rpl::map([](TextWithEntities text) {
				return Ui::Text::Link(
					std::move(text),
					tr::lng_group_invite_subscription_about_url(tr::now));
			}),
			Ui::Text::RichLangValue),
		{ .session = &peer->session() },
		st::boxDividerLabel);
	Ui::AddSkip(wrap->entity());
	Ui::AddSkip(wrap->entity());
	container->add(object_ptr<Ui::DividerLabel>(
		container,
		std::move(about),
		st::defaultBoxDividerLabelPadding,
		RectPart::Top | RectPart::Bottom));
	return { toggle, input };
}

} // namespace Ui
