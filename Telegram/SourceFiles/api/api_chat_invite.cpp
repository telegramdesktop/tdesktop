/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_chat_invite.h"

#include "apiwrap.h"
#include "api/api_credits.h"
#include "boxes/premium_limits_box.h"
#include "core/application.h"
#include "data/components/credits.h"
#include "data/data_channel.h"
#include "data/data_file_origin.h"
#include "data/data_forum.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "info/channel_statistics/boosts/giveaway/boost_badge.h"
#include "info/profile/info_profile_badge.h"
#include "inline_bots/bot_attach_web_view.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_credits_graphics.h"
#include "ui/boxes/confirm_box.h"
#include "ui/controls/userpic_button.h"
#include "ui/effects/credits_graphics.h"
#include "ui/effects/premium_graphics.h"
#include "ui/effects/premium_stars_colored.h"
#include "ui/empty_userpic.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_color_indices.h"
#include "styles/style_credits.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_premium.h"

namespace Api {

namespace {

struct InviteParticipant {
	not_null<UserData*> user;
	Ui::PeerUserpicView userpic;
};

struct ChatInvite {
	QString title;
	QString about;
	PhotoData *photo = nullptr;
	int participantsCount = 0;
	std::vector<InviteParticipant> participants;
	bool isPublic = false;
	bool isChannel = false;
	bool isMegagroup = false;
	bool isBroadcast = false;
	bool isRequestNeeded = false;
	bool isFake = false;
	bool isScam = false;
	bool isVerified = false;
};

[[nodiscard]] ChatInvite ParseInvite(
		not_null<Main::Session*> session,
		const MTPDchatInvite &data) {
	auto participants = std::vector<InviteParticipant>();
	if (const auto list = data.vparticipants()) {
		participants.reserve(list->v.size());
		for (const auto &participant : list->v) {
			if (const auto user = session->data().processUser(participant)) {
				participants.push_back(InviteParticipant{ user });
			}
		}
	}
	const auto photo = session->data().processPhoto(data.vphoto());
	return {
		.title = qs(data.vtitle()),
		.about = data.vabout().value_or_empty(),
		.photo = (photo->isNull() ? nullptr : photo.get()),
		.participantsCount = data.vparticipants_count().v,
		.participants = std::move(participants),
		.isPublic = data.is_public(),
		.isChannel = data.is_channel(),
		.isMegagroup = data.is_megagroup(),
		.isBroadcast = data.is_broadcast(),
		.isRequestNeeded = data.is_request_needed(),
		.isFake = data.is_fake(),
		.isScam = data.is_scam(),
		.isVerified = data.is_verified(),
	};
}

[[nodiscard]] Info::Profile::BadgeType BadgeForInvite(
		const ChatInvite &invite) {
	using Type = Info::Profile::BadgeType;
	return invite.isVerified
		? Type::Verified
		: invite.isScam
		? Type::Scam
		: invite.isFake
		? Type::Fake
		: Type::None;
}

void SubmitChatInvite(
		base::weak_ptr<Window::SessionController> weak,
		not_null<Main::Session*> session,
		const QString &hash,
		bool isGroup) {
	session->api().request(MTPmessages_ImportChatInvite(
		MTP_string(hash)
	)).done([=](const MTPmessages_ChatInviteJoinResult &result) {
		const auto strongController = weak.get();
		if (strongController) {
			strongController->hideLayer();
		}

		ProcessChatInviteJoinResult(
			session,
			strongController ? strongController->uiShow() : nullptr,
			result,
			[=](const MTPUpdates &updates) {
				session->api().applyUpdates(updates);
				if (!strongController) {
					return;
				}
				const auto handleChats = [&](
						const MTPVector<MTPChat> &chats) {
					if (chats.v.isEmpty()) {
						return;
					}
					const auto peerId = chats.v[0].match(
						[](const MTPDchat &data) {
							return peerFromChat(data.vid().v);
						},
						[](const MTPDchannel &data) {
							return peerFromChannel(data.vid().v);
						},
						[](auto&&) {
							return PeerId(0);
						});
					if (const auto peer = session->data().peerLoaded(peerId)) {
						strongController->showPeerHistory(
							peer,
							Window::SectionShow::Way::Forward);
					}
				};
				updates.match([&](const MTPDupdates &data) {
					handleChats(data.vchats());
				}, [&](const MTPDupdatesCombined &data) {
					handleChats(data.vchats());
				}, [&](auto &&) {
					LOG(("API Error: unexpected update cons %1 "
						"(ApiWrap::importChatInvite)").arg(updates.type()));
				});
			},
			weak);
	}).fail([=](const MTP::Error &error) {
		const auto &type = error.type();

		const auto strongController = weak.get();
		if (!strongController) {
			return;
		} else if (type == u"CHANNELS_TOO_MUCH"_q) {
			strongController->show(
				Box(ChannelsLimitBox, &strongController->session()));
			return;
		}

		strongController->hideLayer();
		strongController->showToast([&] {
			if (type == u"INVITE_REQUEST_SENT"_q) {
				return isGroup
					? tr::lng_group_request_sent(tr::now)
					: tr::lng_group_request_sent_channel(tr::now);
			} else if (type == u"USERS_TOO_MUCH"_q) {
				return tr::lng_group_invite_no_room(tr::now);
			} else {
				return tr::lng_group_invite_bad_link(tr::now);
			}
		}(), ApiWrap::kJoinErrorDuration);
	}).send();
}

void ConfirmSubscriptionBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		const QString &hash,
		const MTPDchatInvite *data) {
	box->setWidth(st::boxWideWidth);
	const auto amount = data->vsubscription_pricing()->data().vamount().v;
	const auto formId = data->vsubscription_form_id()->v;
	const auto name = qs(data->vtitle());
	const auto maybePhoto = session->data().processPhoto(data->vphoto());
	const auto photo = maybePhoto->isNull() ? nullptr : maybePhoto.get();

	struct State final {
		std::shared_ptr<Data::PhotoMedia> photoMedia;
		std::unique_ptr<Ui::EmptyUserpic> photoEmpty;
		QImage frame;

		std::optional<MTP::Sender> api;
		Ui::RpWidget* saveButton = nullptr;
		rpl::variable<bool> loading;
	};
	const auto state = box->lifetime().make_state<State>();

	const auto content = box->verticalLayout();

	Ui::AddSkip(content, st::confirmInvitePhotoTop);
	const auto userpic = content->add(
		object_ptr<Ui::RpWidget>(content),
		style::al_top);
	const auto photoSize = st::confirmInvitePhotoSize;
	userpic->resize(Size(photoSize));
	userpic->setNaturalWidth(photoSize);
	const auto creditsIconSize = photoSize / 3;
	const auto creditsIconCallback =
		Ui::PaintOutlinedColoredCreditsIconCallback(
			creditsIconSize,
			1.5);
	state->frame = QImage(
		Size(photoSize * style::DevicePixelRatio()),
		QImage::Format_ARGB32_Premultiplied);
	state->frame.setDevicePixelRatio(style::DevicePixelRatio());
	const auto options = Images::Option::RoundCircle;
	userpic->paintRequest(
	) | rpl::on_next([=, small = Data::PhotoSize::Small] {
		state->frame.fill(Qt::transparent);
		{
			auto p = QPainter(&state->frame);
			if (state->photoMedia) {
				if (const auto image = state->photoMedia->image(small)) {
					p.drawPixmap(
						0,
						0,
						image->pix(Size(photoSize), { .options = options }));
				}
			} else if (state->photoEmpty) {
				state->photoEmpty->paintCircle(
					p,
					0,
					0,
					userpic->width(),
					photoSize);
			}
			if (creditsIconCallback) {
				p.translate(
					photoSize - creditsIconSize,
					photoSize - creditsIconSize);
				creditsIconCallback(p);
			}
		}
		auto p = QPainter(userpic);
		p.drawImage(0, 0, state->frame);
	}, userpic->lifetime());
	userpic->setAttribute(Qt::WA_TransparentForMouseEvents);
	if (photo) {
		state->photoMedia = photo->createMediaView();
		state->photoMedia->wanted(Data::PhotoSize::Small, Data::FileOrigin());
		if (!state->photoMedia->image(Data::PhotoSize::Small)) {
			session->downloaderTaskFinished(
			) | rpl::on_next([=] {
				userpic->update();
			}, userpic->lifetime());
		}
	} else {
		state->photoEmpty = std::make_unique<Ui::EmptyUserpic>(
			Ui::EmptyUserpic::UserpicColor(st::colorIndexRed),
			name);
	}
	Ui::AddSkip(content);
	Ui::AddSkip(content);

	Settings::AddMiniStars(
		content,
		Ui::CreateChild<Ui::RpWidget>(content),
		photoSize,
		box->width(),
		2.);

	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_channel_invite_subscription_title(),
			st::inviteLinkSubscribeBoxTitle),
		style::al_top);
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_channel_invite_subscription_about(
				lt_channel,
				rpl::single(tr::bold(name)),
				lt_price,
				tr::lng_credits_summary_options_credits(
					lt_count,
					rpl::single(amount) | tr::to_count(),
					tr::bold),
				tr::marked),
			st::inviteLinkSubscribeBoxAbout),
		style::al_top);
	Ui::AddSkip(content);
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_channel_invite_subscription_terms(
				lt_link,
				rpl::combine(
					tr::lng_paid_react_agree_link(),
					tr::lng_group_invite_subscription_about_url()
				) | rpl::map([](const QString &text, const QString &url) {
					return tr::link(text, url);
				}),
				tr::rich),
			st::inviteLinkSubscribeBoxTerms),
		style::al_top);

	{
		const auto balance = Settings::AddBalanceWidget(
			content,
			session,
			session->credits().balanceValue(),
			true);
		session->credits().load(true);

		rpl::combine(
			balance->sizeValue(),
			content->sizeValue()
		) | rpl::on_next([=](const QSize &, const QSize &) {
			balance->moveToRight(
				st::creditsHistoryRightSkip * 2,
				st::creditsHistoryRightSkip);
			balance->update();
		}, balance->lifetime());
	}

	const auto sendCredits = [=, weak = base::make_weak(box)] {
		const auto show = box->uiShow();
		const auto buttonWidth = state->saveButton
			? state->saveButton->width()
			: 0;
		const auto finish = [=] {
			state->api = std::nullopt;
			state->loading.force_assign(false);
			if (const auto strong = weak.get()) {
				strong->closeBox();
			}
		};
		state->api->request(
			MTPpayments_SendStarsForm(
				MTP_long(formId),
				MTP_inputInvoiceChatInviteSubscription(MTP_string(hash)))
		).done([=](const MTPpayments_PaymentResult &result) {
			result.match([&](const MTPDpayments_paymentResult &data) {
				session->api().applyUpdates(data.vupdates());
			}, [](const MTPDpayments_paymentVerificationNeeded &data) {
			});
			const auto refill = session->data().activeCreditsSubsRebuilder();
			const auto strong = weak.get();
			if (!strong) {
				return;
			}
			if (!refill) {
				return finish();
			}
			const auto api
				= strong->lifetime().make_state<Api::CreditsHistory>(
					session->user(),
					true,
					true);
			api->requestSubscriptions({}, [=](Data::CreditsStatusSlice d) {
				refill->fire(std::move(d));
				finish();
			});
		}).fail([=](const MTP::Error &error) {
			const auto id = error.type();
			if (weak) {
				state->api = std::nullopt;
			}
			show->showToast(id);
			state->loading.force_assign(false);
		}).send();
		if (state->saveButton) {
			state->saveButton->resizeToWidth(buttonWidth);
		}
	};

	auto confirmText = tr::lng_channel_invite_subscription_button();
	state->saveButton = box->addButton(std::move(confirmText), [=] {
		if (state->api) {
			return;
		}
		state->api.emplace(&session->mtp());
		state->loading.force_assign(true);

		const auto done = [=](Settings::SmallBalanceResult result) {
			if (result == Settings::SmallBalanceResult::Success
				|| result == Settings::SmallBalanceResult::Already) {
				sendCredits();
			} else {
				state->api = std::nullopt;
				state->loading.force_assign(false);
			}
		};
		Settings::MaybeRequestBalanceIncrease(
			Main::MakeSessionShow(box->uiShow(), session),
			amount,
			Settings::SmallBalanceSubscription{ .name = name },
			done);
	});

	if (const auto saveButton = state->saveButton) {
		using namespace Info::Statistics;
		const auto loadingAnimation = InfiniteRadialAnimationWidget(
			saveButton,
			saveButton->height() / 2,
			&st::editStickerSetNameLoading);
		AddChildToWidgetCenter(saveButton, loadingAnimation);
		loadingAnimation->showOn(
			state->loading.value() | rpl::map(rpl::mappers::_1));
	}
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

void ConfirmInviteBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		const MTPDchatInvite *invitePtr,
		ChannelData *invitePeekChannel,
		Fn<void()> submit) {
	auto invite = ParseInvite(session, *invitePtr);
	const auto isChannel = invite.isChannel && !invite.isMegagroup;
	const auto requestApprove = invite.isRequestNeeded;
	const auto count = invite.participantsCount;

	struct State {
		std::shared_ptr<Data::PhotoMedia> photoMedia;
		std::unique_ptr<Ui::EmptyUserpic> photoEmpty;
		std::vector<InviteParticipant> participants;
	};
	const auto state = box->lifetime().make_state<State>();
	state->participants = std::move(invite.participants);

	const auto status = [&] {
		return invitePeekChannel
			? tr::lng_channel_invite_private(tr::now)
			: (!state->participants.empty()
				&& int(state->participants.size()) < count)
			? tr::lng_group_invite_members(tr::now, lt_count, count)
			: (count > 0 && isChannel)
			? tr::lng_chat_status_subscribers(
				tr::now,
				lt_count_decimal,
				count)
			: (count > 0)
			? tr::lng_chat_status_members(tr::now, lt_count_decimal, count)
			: isChannel
			? tr::lng_channel_status(tr::now)
			: tr::lng_group_status(tr::now);
	}();

	box->setNoContentMargin(true);
	box->setWidth(st::boxWideWidth);
	const auto content = box->verticalLayout();

	Ui::AddSkip(content, st::confirmInvitePhotoTop);
	const auto userpic = content->add(
		object_ptr<Ui::RpWidget>(content),
		style::al_top);
	const auto photoSize = st::confirmInvitePhotoSize;
	userpic->resize(Size(photoSize));
	userpic->setNaturalWidth(photoSize);
	userpic->paintRequest(
	) | rpl::on_next([=, small = Data::PhotoSize::Small] {
		auto p = QPainter(userpic);
		if (state->photoMedia) {
			if (const auto image = state->photoMedia->image(small)) {
				p.drawPixmap(
					0,
					0,
					image->pix(
						Size(photoSize),
						{ .options = Images::Option::RoundCircle }));
			}
		} else if (state->photoEmpty) {
			state->photoEmpty->paintCircle(
				p,
				0,
				0,
				userpic->width(),
				photoSize);
		}
	}, userpic->lifetime());
	userpic->setAttribute(Qt::WA_TransparentForMouseEvents);
	if (const auto photo = invite.photo) {
		state->photoMedia = photo->createMediaView();
		state->photoMedia->wanted(
			Data::PhotoSize::Small,
			Data::FileOrigin());
		if (!state->photoMedia->image(Data::PhotoSize::Small)) {
			session->downloaderTaskFinished(
			) | rpl::on_next([=] {
				userpic->update();
			}, userpic->lifetime());
		}
	} else {
		state->photoEmpty = std::make_unique<Ui::EmptyUserpic>(
			Ui::EmptyUserpic::UserpicColor(st::colorIndexRed),
			invite.title);
	}

	Ui::AddSkip(content);
	const auto title = box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			invite.title,
			st::confirmInviteTitle),
		style::al_top);

	const auto badgeType = BadgeForInvite(invite);
	if (badgeType != Info::Profile::BadgeType::None) {
		const auto badgeParent = title->parentWidget();
		const auto badge = box->lifetime().make_state<Info::Profile::Badge>(
			badgeParent,
			st::infoPeerBadge,
			session,
			rpl::single(Info::Profile::Badge::Content{ badgeType }),
			nullptr,
			[] { return false; });
		title->geometryValue(
		) | rpl::on_next([=](const QRect &r) {
			badge->move(r.x() + r.width(), r.y(), r.y() + r.height());
		}, title->lifetime());
	}

	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			status,
			st::confirmInviteStatus),
		style::al_top);

	if (!invite.about.isEmpty()) {
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				invite.about,
				st::confirmInviteAbout),
			st::confirmInviteAboutPadding,
			style::al_top);
	}

	if (requestApprove) {
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				(isChannel
					? tr::lng_group_request_about_channel(tr::now)
					: tr::lng_group_request_about(tr::now)),
				st::confirmInviteStatus),
			st::confirmInviteAboutRequestsPadding,
			style::al_top);
	}

	if (!state->participants.empty()) {
		while (state->participants.size() > 4) {
			state->participants.pop_back();
		}
		const auto padding = (st::confirmInviteUsersWidth
			- 4 * st::confirmInviteUserPhotoSize) / 10;
		const auto userWidth = st::confirmInviteUserPhotoSize + 2 * padding;

		auto strip = object_ptr<Ui::RpWidget>(content);
		const auto rawStrip = strip.data();
		rawStrip->resize(st::boxWideWidth, st::confirmInviteUserHeight);
		rawStrip->setNaturalWidth(st::boxWideWidth);

		const auto shown = int(state->participants.size());
		const auto sumWidth = shown * userWidth;
		const auto baseLeft = (st::boxWideWidth - sumWidth) / 2;
		for (auto i = 0; i != shown; ++i) {
			const auto &participant = state->participants[i];
			const auto name = Ui::CreateChild<Ui::FlatLabel>(
				rawStrip,
				st::confirmInviteUserName);
			name->resizeToWidth(
				st::confirmInviteUserPhotoSize + padding);
			name->setText(participant.user->firstName.isEmpty()
				? participant.user->name()
				: participant.user->firstName);
			name->moveToLeft(
				baseLeft + i * userWidth + (padding / 2),
				st::confirmInviteUserNameTop - st::confirmInviteUserPhotoTop);
		}

		rawStrip->paintRequest(
		) | rpl::on_next([=] {
			auto p = Painter(rawStrip);
			const auto total = int(state->participants.size());
			const auto totalWidth = total * userWidth;
			auto left = (rawStrip->width() - totalWidth) / 2;
			for (auto &participant : state->participants) {
				participant.user->paintUserpicLeft(
					p,
					participant.userpic,
					left + (userWidth - st::confirmInviteUserPhotoSize) / 2,
					0,
					rawStrip->width(),
					st::confirmInviteUserPhotoSize);
				left += userWidth;
			}
		}, rawStrip->lifetime());

		Ui::AddSkip(content, st::boxPadding.bottom());
		content->add(std::move(strip), style::margins());
	}

	box->addButton((requestApprove
		? tr::lng_group_request_to_join()
		: isChannel
		? tr::lng_profile_join_channel()
		: tr::lng_profile_join_group()), submit);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

} // namespace

void ProcessChatInviteJoinResult(
		not_null<Main::Session*> session,
		std::shared_ptr<Ui::Show> show,
		const MTPmessages_ChatInviteJoinResult &result,
		Fn<void(const MTPUpdates &updates)> done,
		base::weak_ptr<Window::SessionController> controller) {
	result.match([&](const MTPDmessages_chatInviteJoinResultOk &data) {
		done(data.vupdates());
	}, [&](const MTPDmessages_chatInviteJoinResultWebView &data) {
		session->data().processUsers(data.vusers());
		const auto bot = session->data().userLoaded(UserId(data.vbot_id().v));
		if (!bot || !show) {
			LOG(("API Error: guard bot %1 not loaded "
				"(Api::ProcessChatInviteJoinResult)").arg(data.vbot_id().v));
			return;
		}
		session->attachWebView().open({
			.bot = not_null<UserData*>{ bot },
			.parentShow = std::move(show),
			.context = {
				.controller = controller,
				.maySkipConfirmation = false,
			},
			.source = InlineBots::WebViewSourceJoinChat{
				.queryId = data.vquery_id().v,
			},
		});
	});
}

void CheckChatInvite(
		not_null<Window::SessionController*> controller,
		const QString &hash,
		ChannelData *invitePeekChannel,
		Fn<void()> loaded) {
	const auto session = &controller->session();
	const auto weak = base::make_weak(controller);
	session->api().checkChatInvite(hash, [=](const MTPChatInvite &result) {
		const auto strong = weak.get();
		if (!strong) {
			return;
		}
		if (loaded) {
			loaded();
		}
		Core::App().hideMediaView();
		const auto show = [&](not_null<PeerData*> chat) {
			const auto way = Window::SectionShow::Way::Forward;
			if (const auto forum = chat->forum()) {
				strong->showForum(forum, way);
			} else {
				strong->showPeerHistory(chat, way);
			}
		};
		result.match([=](const MTPDchatInvite &data) {
			const auto isGroup = !data.is_broadcast();
			const auto hasPricing = !!data.vsubscription_pricing();
			const auto canRefulfill = data.is_can_refulfill_subscription();
			if (hasPricing
				&& !canRefulfill
				&& !data.vsubscription_form_id()) {
				strong->uiShow()->showToast(
					tr::lng_confirm_phone_link_invalid(tr::now));
				return;
			}
			const auto box = (hasPricing && !canRefulfill)
				? strong->show(Box(
					ConfirmSubscriptionBox,
					session,
					hash,
					&data))
				: strong->show(Box(
					ConfirmInviteBox,
					session,
					&data,
					invitePeekChannel,
					[=] { SubmitChatInvite(weak, session, hash, isGroup); }));
			if (invitePeekChannel) {
				box->boxClosing(
				) | rpl::filter([=] {
					return !invitePeekChannel->amIn();
				}) | rpl::on_next([=] {
					if (const auto strong = weak.get()) {
						strong->clearSectionStack(Window::SectionShow(
							Window::SectionShow::Way::ClearStack,
							anim::type::normal,
							anim::activation::background));
					}
				}, box->lifetime());
			}
		}, [=](const MTPDchatInviteAlready &data) {
			if (const auto chat = session->data().processChat(data.vchat())) {
				if (const auto channel = chat->asChannel()) {
					channel->clearInvitePeek();
				}
				show(chat);
			}
		}, [=](const MTPDchatInvitePeek &data) {
			if (const auto chat = session->data().processChat(data.vchat())) {
				if (const auto channel = chat->asChannel()) {
					channel->setInvitePeek(hash, data.vexpires().v);
					show(chat);
				}
			}
		});
	}, [=](const MTP::Error &error) {
		if (MTP::IsFloodError(error)) {
			if (const auto strong = weak.get()) {
				strong->show(Ui::MakeInformBox(tr::lng_flood_error()));
			}
			return;
		}
		if (error.code() != 400) {
			return;
		}
		Core::App().hideMediaView();
		if (const auto strong = weak.get()) {
			strong->show(Ui::MakeInformBox(tr::lng_group_invite_bad_link()));
		}
	});
}

} // namespace Api
