/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_media_common.h"

#include "api/api_sensitive_content.h"
#include "api/api_views.h"
#include "apiwrap.h"
#include "inline_bots/bot_attach_web_view.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/widgets/checkbox.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/painter.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "data/data_wall_paper.h"
#include "data/data_media_types.h"
#include "data/data_user.h"
#include "history/view/history_view_element.h"
#include "history/view/media/history_view_media_grouped.h"
#include "history/view/media/history_view_photo.h"
#include "history/view/media/history_view_gif.h"
#include "history/view/media/history_view_document.h"
#include "history/view/media/history_view_sticker.h"
#include "history/view/media/history_view_theme_document.h"
#include "history/history_item.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "mainwindow.h"
#include "media/streaming/media_streaming_utility.h"
#include "payments/payments_checkout_process.h"
#include "payments/payments_non_panel_process.h"
#include "settings/settings_common.h"
#include "webrtc/webrtc_environment.h"
#include "webview/webview_interface.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

namespace HistoryView {
namespace {

constexpr auto kMediaUnlockedTooltipDuration = 5 * crl::time(1000);
const auto kVerifyAgeAboutPrefix = "cloud_lng_age_verify_about_";

rpl::producer<TextWithEntities> AgeVerifyAbout(
		not_null<Main::Session*> session) {
	const auto appConfig = &session->appConfig();
	return rpl::single(
		rpl::empty
	) | rpl::then(
		Lang::Updated()
	) | rpl::map([=] {
		const auto country = appConfig->ageVerifyCountry().toLower();
		const auto age = appConfig->ageVerifyMinAge();
		const auto [shift, string] = Lang::Plural(
			Lang::kPluralKeyBaseForCloudValue,
			age,
			lt_count);
		const auto postfixes = {
			"#zero",
			"#one",
			"#two",
			"#few",
			"#many",
			"#other"
		};
		Assert(shift >= 0 && shift < postfixes.size());
		const auto postfix = *(begin(postfixes) + shift);
		return Ui::Text::RichLangValue(Lang::GetNonDefaultValue(
			kVerifyAgeAboutPrefix + country.toUtf8() + postfix
		).replace(u"{count}"_q, string));
	});
}

[[nodiscard]] object_ptr<Ui::RpWidget> AgeVerifyIcon(
		not_null<QWidget*> parent) {
	const auto padding = st::settingsAgeVerifyIconPadding;
	const auto full = st::settingsAgeVerifyIcon.size().grownBy(padding);
	auto result = object_ptr<Ui::RpWidget>(parent);
	const auto raw = result.data();
	raw->resize(full);
	raw->paintRequest() | rpl::start_with_next([=] {
		auto p = QPainter(raw);
		const auto x = (raw->width() - full.width()) / 2;
		auto hq = PainterHighQualityEnabler(p);
		p.setBrush(st::windowBgActive);
		p.setPen(Qt::NoPen);
		const auto inner = QRect(QPoint(x, 0), full);
		p.drawEllipse(inner);
		st::settingsAgeVerifyIcon.paintInCenter(p, inner);
	}, raw->lifetime());
	return result;
}

} // namespace

void PaintInterpolatedIcon(
		QPainter &p,
		const style::icon &a,
		const style::icon &b,
		float64 b_ratio,
		QRect rect) {
	PainterHighQualityEnabler hq(p);
	p.save();
	p.translate(rect.center());
	p.setOpacity(b_ratio);
	p.scale(b_ratio, b_ratio);
	b.paintInCenter(p, rect.translated(-rect.center()));
	p.restore();

	p.save();
	p.translate(rect.center());
	p.setOpacity(1. - b_ratio);
	p.scale(1. - b_ratio, 1. - b_ratio);
	a.paintInCenter(p, rect.translated(-rect.center()));
	p.restore();
}

std::unique_ptr<Media> CreateAttach(
		not_null<Element*> parent,
		DocumentData *document,
		PhotoData *photo) {
	return CreateAttach(parent, document, photo, {}, {});
}

std::unique_ptr<Media> CreateAttach(
		not_null<Element*> parent,
		DocumentData *document,
		PhotoData *photo,
		const std::vector<std::unique_ptr<Data::Media>> &collage,
		const QString &webpageUrl) {
	if (!collage.empty()) {
		return std::make_unique<GroupedMedia>(parent, collage);
	} else if (document) {
		const auto spoiler = false;
		if (document->sticker()) {
			const auto skipPremiumEffect = true;
			return std::make_unique<UnwrappedMedia>(
				parent,
				std::make_unique<Sticker>(
					parent,
					document,
					skipPremiumEffect));
		} else if (document->isAnimation() || document->isVideoFile()) {
			return std::make_unique<Gif>(
				parent,
				parent->data(),
				document,
				spoiler);
		} else if (document->isWallPaper() || document->isTheme()) {
			return std::make_unique<ThemeDocument>(
				parent,
				document,
				ThemeDocument::ParamsFromUrl(webpageUrl));
		}
		return std::make_unique<Document>(parent, parent->data(), document);
	} else if (photo) {
		const auto spoiler = false;
		return std::make_unique<Photo>(
			parent,
			parent->data(),
			photo,
			spoiler);
	} else if (const auto params = ThemeDocument::ParamsFromUrl(webpageUrl)) {
		return std::make_unique<ThemeDocument>(parent, nullptr, params);
	}
	return nullptr;
}

int UnitedLineHeight() {
	return std::max(st::semiboldFont->height, st::normalFont->height);
}

QImage PrepareWithBlurredBackground(
		QSize outer,
		::Media::Streaming::ExpandDecision resize,
		Image *large,
		Image *blurred) {
	return PrepareWithBlurredBackground(
		outer,
		resize,
		large ? large->original() : QImage(),
		blurred ? blurred->original() : QImage());
}

QImage PrepareWithBlurredBackground(
		QSize outer,
		::Media::Streaming::ExpandDecision resize,
		QImage large,
		QImage blurred) {
	const auto ratio = style::DevicePixelRatio();
	if (resize.expanding) {
		return Images::Prepare(std::move(large), resize.result * ratio, {
			.outer = outer,
		});
	}
	auto background = QImage(
		outer * ratio,
		QImage::Format_ARGB32_Premultiplied);
	background.setDevicePixelRatio(ratio);
	if (blurred.isNull()) {
		background.fill(Qt::black);
		if (large.isNull()) {
			return background;
		}
	}
	auto p = QPainter(&background);
	if (!blurred.isNull()) {
		using namespace ::Media::Streaming;
		FillBlurredBackground(p, outer, std::move(blurred));
	}
	if (!large.isNull()) {
		auto image = large.scaled(
			resize.result * ratio,
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation);
		image.setDevicePixelRatio(ratio);
		p.drawImage(
			(outer.width() - resize.result.width()) / 2,
			(outer.height() - resize.result.height()) / 2,
			image);
	}
	p.end();
	return background;
}

QSize CountDesiredMediaSize(QSize original) {
	return DownscaledSize(
		style::ConvertScale(original),
		{ st::maxMediaSize, st::maxMediaSize });
}

QSize CountMediaSize(QSize desired, int newWidth) {
	Expects(!desired.isEmpty());

	return (desired.width() <= newWidth)
		? desired
		: NonEmptySize(
			desired.scaled(newWidth, desired.height(), Qt::KeepAspectRatio));
}

QSize CountPhotoMediaSize(
		QSize desired,
		int newWidth,
		int maxWidth) {
	const auto media = CountMediaSize(desired, qMin(newWidth, maxWidth));
	return (media.height() <= newWidth)
		? media
		: NonEmptySize(
			media.scaled(media.width(), newWidth, Qt::KeepAspectRatio));
}

void ShowPaidMediaUnlockedToast(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item) {
	const auto media = item->media();
	const auto invoice = media ? media->invoice() : nullptr;
	if (!invoice || !invoice->isPaidMedia) {
		return;
	}
	const auto sender = item->originalSender();
	const auto broadcast = (sender && sender->isBroadcast())
		? sender
		: item->history()->peer.get();
	const auto user = item->viaBot()
		? item->viaBot()
		: item->originalSender()
		? item->originalSender()->asUser()
		: nullptr;
	auto text = tr::lng_credits_media_done_title(
		tr::now,
		Ui::Text::Bold
	).append('\n').append(user
		? tr::lng_credits_media_done_text_user(
			tr::now,
			lt_count,
			invoice->amount,
			lt_user,
			Ui::Text::Bold(user->shortName()),
			Ui::Text::RichLangValue)
		: tr::lng_credits_media_done_text(
			tr::now,
			lt_count,
			invoice->amount,
			lt_chat,
			Ui::Text::Bold(broadcast->name()),
			Ui::Text::RichLangValue));
	controller->showToast(std::move(text), kMediaUnlockedTooltipDuration);
}

ClickHandlerPtr MakePaidMediaLink(not_null<HistoryItem*> item) {
	return std::make_shared<LambdaClickHandler>([=](ClickContext context) {
		const auto my = context.other.value<ClickHandlerContext>();
		const auto controller = my.sessionWindow.get();
		const auto weak = my.sessionWindow;
		const auto itemId = item->fullId();
		const auto session = &item->history()->session();
		using Result = Payments::CheckoutResult;
		const auto done = crl::guard(session, [=](Result result) {
			if (result != Result::Paid) {
				return;
			} else if (const auto item = session->data().message(itemId)) {
				session->api().views().pollExtendedMedia(item, true);
				if (const auto strong = weak.get()) {
					ShowPaidMediaUnlockedToast(strong, item);
				}
			}
		});
		const auto reactivate = controller
			? crl::guard(
				controller,
				[=](auto) { controller->widget()->activate(); })
			: Fn<void(Payments::CheckoutResult)>();
		const auto credits = Payments::IsCreditsInvoice(item);
		const auto nonPanelPaymentFormProcess = (controller && credits)
			? Payments::ProcessNonPanelPaymentFormFactory(controller, done)
			: nullptr;
		Payments::CheckoutProcess::Start(
			item,
			Payments::Mode::Payment,
			reactivate,
			nonPanelPaymentFormProcess);
	});
}

void ShowAgeVerification(
		std::shared_ptr<Ui::Show> show,
		not_null<UserData*> bot,
		Fn<void()> reveal) {
	show->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setNoContentMargin(true);
		box->setStyle(st::settingsAgeVerifyBox);
		box->setWidth(st::boxWideWidth);

		box->addRow(AgeVerifyIcon(box), st::settingsAgeVerifyIconMargin);

		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_age_verify_title(),
				st::settingsAgeVerifyTitle),
			st::boxRowPadding + st::settingsAgeVerifyMargin,
			style::al_top);
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				AgeVerifyAbout(&bot->session()),
				st::settingsAgeVerifyText),
			st::boxRowPadding + st::settingsAgeVerifyMargin,
			style::al_top);
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_age_verify_here(Ui::Text::RichLangValue),
				st::settingsAgeVerifyText),
			st::boxRowPadding + st::settingsAgeVerifyMargin,
			style::al_top);

		const auto weak = QPointer<Ui::GenericBox>(box);
		const auto done = crl::guard(&bot->session(), [=](int age) {
			const auto min = bot->session().appConfig().ageVerifyMinAge();
			if (age >= min) {
				reveal();
				bot->session().api().sensitiveContent().update(true);
			} else {
				show->showToast({
					.title = tr::lng_age_verify_sorry_title(tr::now),
					.text = { tr::lng_age_verify_sorry_text(tr::now) },
					.duration = Ui::Toast::kDefaultDuration * 3,
				});
			}
			if (const auto strong = weak.data()) {
				strong->closeBox();
			}
		});
		const auto button = box->addButton(tr::lng_age_verify_button(), [=] {
			bot->session().attachWebView().open({
				.bot = bot,
				.parentShow = box->uiShow(),
				.context = { .maySkipConfirmation = true },
				.source = InlineBots::WebViewSourceAgeVerification{
					.done = done,
				},
			});
		});
		box->widthValue(
		) | rpl::start_with_next([=](int width) {
			const auto &padding = st::settingsAgeVerifyBox.buttonPadding;
			button->resizeToWidth(width
				- padding.left()
				- padding.right());
			button->moveToLeft(padding.left(), padding.top());
		}, button->lifetime());

		const auto close = Ui::CreateChild<Ui::IconButton>(
			box.get(),
			st::boxTitleClose);
		close->setClickedCallback([=] {
			box->closeBox();
		});
		box->widthValue(
		) | rpl::start_with_next([=](int width) {
			close->moveToRight(0, 0);
		}, box->lifetime());
		crl::on_main(close, [=] { close->raise(); });
	}));
}

void ShowAgeVerificationMobile(
		std::shared_ptr<Ui::Show> show,
		not_null<Main::Session*> session) {
	show->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_age_verify_title());
		box->setWidth(st::boxWideWidth);

		const auto size = st::settingsCloudPasswordIconSize;
		auto icon = Settings::CreateLottieIcon(
			box->verticalLayout(),
			{
				.name = u"phone"_q,
				.sizeOverride = { size, size },
			},
			st::peerAppearanceIconPadding);

		box->showFinishes(
		) | rpl::start_with_next([animate = std::move(icon.animate)] {
			animate(anim::repeat::once);
		}, box->lifetime());

		box->addRow(std::move(icon.widget));

		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				AgeVerifyAbout(session),
				st::settingsAgeVerifyText),
			st::boxRowPadding + st::settingsAgeVerifyMargin,
			style::al_top);
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_age_verify_mobile(Ui::Text::RichLangValue),
				st::settingsAgeVerifyText),
			st::boxRowPadding + st::settingsAgeVerifyMargin,
			style::al_top);

		box->addButton(tr::lng_box_ok(), [=] {
			box->closeBox();
		});
	}));
}

void ShowAgeVerificationRequired(
		std::shared_ptr<Ui::Show> show,
		not_null<Main::Session*> session,
		Fn<void()> reveal) {
	struct State {
		Fn<void()> check;
		rpl::lifetime lifetime;
		std::optional<PeerData*> bot;
	};
	const auto state = std::make_shared<State>();
	const auto username = session->appConfig().ageVerifyBotUsername();
	const auto bot = session->data().peerByUsername(username);
	if (username.isEmpty() || bot) {
		state->bot = bot;
	} else {
		session->api().request(MTPcontacts_ResolveUsername(
			MTP_flags(0),
			MTP_string(username),
			MTPstring()
		)).done([=](const MTPcontacts_ResolvedPeer &result) {
			const auto &data = result.data();
			session->data().processUsers(data.vusers());
			session->data().processChats(data.vchats());
			const auto botId = peerFromMTP(data.vpeer());
			state->bot = session->data().peerLoaded(botId);
			state->check();
		}).fail([=] {
			state->bot = nullptr;
			state->check();
		}).send();
	}
	state->check = [=] {
		const auto sensitive = &session->api().sensitiveContent();
		const auto ready = sensitive->loaded();
		if (!ready) {
			state->lifetime = sensitive->loadedValue(
			) | rpl::filter(
				rpl::mappers::_1
			) | rpl::take(1) | rpl::start_with_next(state->check);
			return;
		} else if (!state->bot.has_value()) {
			return;
		}
		const auto has = Core::App().mediaDevices().recordAvailability();
		const auto available = Webview::Availability();
		const auto bot = (*state->bot)->asUser();
		if (available.error == Webview::Available::Error::None
			&& has == Webrtc::RecordAvailability::VideoAndAudio
			&& sensitive->canChangeCurrent()
			&& bot
			&& bot->isBot()
			&& bot->botInfo->hasMainApp) {
			ShowAgeVerification(show, bot, reveal);
		} else {
			ShowAgeVerificationMobile(show, session);
		}
		state->lifetime.destroy();
		state->check = nullptr;
	};
	state->check();
}

void ShowSensitiveConfirm(
		std::shared_ptr<Ui::Show> show,
		not_null<Main::Session*> session,
		Fn<void()> reveal) {
	show->show(Box([=](not_null<Ui::GenericBox*> box) {
		struct State {
			rpl::variable<bool> canChange;
			Ui::Checkbox *checkbox = nullptr;
		};
		const auto state = box->lifetime().make_state<State>();
		const auto sensitive = &session->api().sensitiveContent();
		state->canChange = sensitive->canChange();
		const auto done = [=](Fn<void()> close) {
			if (state->canChange.current()
				&& state->checkbox->checked()) {
				show->showToast({
					.text = tr::lng_sensitive_toast(
						tr::now,
						Ui::Text::RichLangValue),
					.adaptive = true,
					.duration = 5 * crl::time(1000),
				});
				sensitive->update(true);
			} else {
				reveal();
			}
			close();
		};
		Ui::ConfirmBox(box, {
			.text = tr::lng_sensitive_text(Ui::Text::RichLangValue),
			.confirmed = done,
			.confirmText = tr::lng_sensitive_view(),
			.title = tr::lng_sensitive_title(),
		});
		const auto skip = st::defaultCheckbox.margin.bottom();
		const auto wrap = box->addRow(
			object_ptr<Ui::SlideWrap<Ui::Checkbox>>(
				box,
				object_ptr<Ui::Checkbox>(
					box,
					tr::lng_sensitive_always(tr::now),
					false)),
			st::boxRowPadding + QMargins(0, 0, 0, skip));
		wrap->toggleOn(state->canChange.value());
		wrap->finishAnimating();
		state->checkbox = wrap->entity();
	}));
}

ClickHandlerPtr MakeSensitiveMediaLink(
		ClickHandlerPtr reveal,
		not_null<HistoryItem*> item) {
	const auto session = &item->history()->session();
	session->api().sensitiveContent().preload();

	return std::make_shared<LambdaClickHandler>([=](ClickContext context) {
		const auto plain = [reveal, context] {
			if (const auto raw = reveal.get()) {
				raw->onClick(context);
			}
		};
		const auto my = context.other.value<ClickHandlerContext>();
		const auto controller = my.sessionWindow.get();
		const auto show = controller ? controller->uiShow() : my.show;
		if (!show) {
			plain();
		} else if (session->appConfig().ageVerifyNeeded()) {
			ShowAgeVerificationRequired(show, session, plain);
		} else {
			ShowSensitiveConfirm(show, session, plain);
		}
	});
}

} // namespace HistoryView
