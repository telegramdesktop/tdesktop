/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/send_credits_box.h"

#include "api/api_credits.h"
#include "core/ui_integration.h" // Core::MarkedTextContext.
#include "data/data_credits.h"
#include "data/data_file_origin.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/stickers/data_custom_emoji.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "payments/payments_form.h"
#include "settings/settings_credits.h"
#include "ui/controls/userpic_button.h"
#include "ui/effects/premium_graphics.h"
#include "ui/effects/premium_top_bar.h" // Ui::Premium::ColorizedSvg.
#include "ui/image/image_prepare.h"
#include "ui/layers/generic_box.h"
#include "ui/rect.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "styles/style_boxes.h"
#include "styles/style_credits.h"
#include "styles/style_giveaway.h"
#include "styles/style_layers.h"
#include "styles/style_premium.h"
#include "styles/style_settings.h"

namespace Ui {
namespace {
} // namespace

void SendCreditsBox(
		not_null<Ui::GenericBox*> box,
		not_null<HistoryItem*> item) {
	const auto media = item->media();
	const auto invoice = media ? media->invoice() : nullptr;
	if (!invoice) {
		return;
	}
	box->setStyle(st::giveawayGiftCodeBox);
	box->setNoContentMargin(true);

	const auto session = &item->history()->owner().session();

	const auto photoSize = st::defaultUserpicButton.photoSize;

	const auto content = box->verticalLayout();
	Ui::AddSkip(content, photoSize / 2);

	{
		const auto ministarsContainer = Ui::CreateChild<Ui::RpWidget>(box);
		const auto fullHeight = photoSize * 2;
		using MiniStars = Ui::Premium::ColoredMiniStars;
		const auto ministars = box->lifetime().make_state<MiniStars>(
			ministarsContainer,
			false,
			Ui::Premium::MiniStars::Type::BiStars);
		ministars->setColorOverride(Ui::Premium::CreditsIconGradientStops());

		ministarsContainer->paintRequest(
		) | rpl::start_with_next([=] {
			auto p = QPainter(ministarsContainer);
			ministars->paint(p);
		}, ministarsContainer->lifetime());

		box->widthValue(
		) | rpl::start_with_next([=](int width) {
			ministarsContainer->resize(width, fullHeight);
			const auto w = fullHeight / 3 * 2;
			ministars->setCenter(QRect(
				(width - w) / 2,
				(fullHeight - w) / 2,
				w,
				w));
		}, ministarsContainer->lifetime());
	}

	if (false && invoice->photo) {
		struct State {
			std::shared_ptr<Data::PhotoMedia> view;
			Image *image = nullptr;
			rpl::lifetime downloadLifetime;
		};
		const auto state = content->lifetime().make_state<State>();
		const auto widget = box->addRow(
			object_ptr<Ui::CenterWrap<>>(
				content,
				object_ptr<Ui::RpWidget>(content)))->entity();
		state->view = invoice->photo->createMediaView();
		state->view->wanted(Data::PhotoSize::Large, item->fullId());

		widget->resize(Size(photoSize));

		rpl::single(rpl::empty_value()) | rpl::then(
			session->downloaderTaskFinished()
		) | rpl::start_with_next([=] {
			using Size = Data::PhotoSize;
			if (const auto large = state->view->image(Size::Large)) {
				state->image = large;
			} else if (const auto small = state->view->image(Size::Small)) {
				state->image = small;
			} else if (const auto t = state->view->image(Size::Thumbnail)) {
				state->image = t;
			}
			widget->update();
			if (state->view->loaded()) {
				state->downloadLifetime.destroy();
			}
		}, state->downloadLifetime);

		widget->paintRequest(
		) | rpl::start_with_next([=] {
			auto p = QPainter(widget);
			if (state->image) {
				p.drawPixmap(0, 0, state->image->pix(widget->width(), {
					.options = Images::Option::RoundCircle,
				}));
			}
		}, widget->lifetime());
	} else {
		const auto widget = box->addRow(
			object_ptr<Ui::CenterWrap<>>(
				content,
				object_ptr<Ui::UserpicButton>(
					content,
					item->author(),
					st::defaultUserpicButton)));
		widget->setAttribute(Qt::WA_TransparentForMouseEvents);
	}

	const auto asd = box->lifetime().make_state<uint64>();

	Ui::AddSkip(content);
	box->addRow(object_ptr<Ui::CenterWrap<>>(
		box,
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_credits_box_out_title(),
			st::settingsPremiumUserTitle)));
	Ui::AddSkip(content);
	box->addRow(object_ptr<Ui::CenterWrap<>>(
		box,
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_credits_box_out_sure(
				lt_count,
				rpl::single(invoice->amount) | tr::to_count(),
				lt_text,
				rpl::single(TextWithEntities{ invoice->title }),
				lt_bot,
				rpl::single(TextWithEntities{ item->author()->name() }),
				Ui::Text::RichLangValue),
			st::creditsBoxAbout)));
	Ui::AddSkip(content);
	Ui::AddSkip(content);

	const auto button = box->addButton(rpl::single(QString()), [=] {
	});
	{
		const auto emojiMargin = QMargins(
			0,
			-st::moderateBoxExpandInnerSkip,
			0,
			0);
		const auto buttonEmoji = Ui::Text::SingleCustomEmoji(
			session->data().customEmojiManager().registerInternalEmoji(
				st::settingsPremiumIconStar,
				emojiMargin,
				true));
		auto buttonText = tr::lng_credits_box_out_confirm(
			lt_count,
			rpl::single(invoice->amount) | tr::to_count(),
			lt_emoji,
			rpl::single(buttonEmoji),
			Ui::Text::RichLangValue);
		const auto buttonLabel = Ui::CreateChild<Ui::FlatLabel>(
			button,
			rpl::single(QString()),
			st::defaultFlatLabel);
		std::move(
			buttonText
		) | rpl::start_with_next([=](const TextWithEntities &text) {
			buttonLabel->setMarkedText(
				text,
				Core::MarkedTextContext{
					.session = session,
					.customEmojiRepaint = [=] { buttonLabel->update(); },
				});
		}, buttonLabel->lifetime());
		buttonLabel->setTextColorOverride(
			box->getDelegate()->style().button.textFg->c);
		button->sizeValue(
		) | rpl::start_with_next([=](const QSize &size) {
			buttonLabel->moveToLeft(
				(size.width() - buttonLabel->width()) / 2,
				(size.height() - buttonLabel->height()) / 2);
		}, buttonLabel->lifetime());
		buttonLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
	}

	const auto buttonWidth = st::boxWidth
		- rect::m::sum::h(st::giveawayGiftCodeBox.buttonPadding);
	button->widthValue() | rpl::filter([=] {
		return (button->widthNoMargins() != buttonWidth);
	}) | rpl::start_with_next([=] {
		button->resizeToWidth(buttonWidth);
	}, button->lifetime());

	{
		const auto close = Ui::CreateChild<Ui::IconButton>(
			box.get(),
			st::boxTitleClose);
		close->setClickedCallback([=] {
			box->closeBox();
		});
		box->widthValue(
		) | rpl::start_with_next([=](int width) {
			close->moveToRight(0, 0);
			close->raise();
		}, close->lifetime());
	}

	{
		const auto balance = Settings::AddBalanceWidget(
			content,
			session->creditsValue(),
			false);
		const auto api = balance->lifetime().make_state<Api::CreditsStatus>(
			session->user());
		api->request({}, [=](Data::CreditsStatusSlice slice) {
			session->setCredits(slice.balance);
		});
		rpl::combine(
			balance->sizeValue(),
			content->sizeValue()
		) | rpl::start_with_next([=](const QSize &, const QSize &) {
			balance->moveToLeft(
				st::creditsHistoryRightSkip * 2,
				st::creditsHistoryRightSkip);
			balance->update();
		}, balance->lifetime());
	}
}

bool IsCreditsInvoice(not_null<HistoryItem*> item) {
	const auto media = item->media();
	const auto invoice = media ? media->invoice() : nullptr;
	return invoice && (invoice->currency == "XTR");
}

} // namespace Ui
