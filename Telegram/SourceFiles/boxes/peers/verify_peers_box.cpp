/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/verify_peers_box.h"

#include "apiwrap.h"
#include "boxes/peer_list_controllers.h"
#include "data/data_user.h"
#include "history/history.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"

namespace {

constexpr auto kSetupVerificationToastDuration = 4 * crl::time(1000);

class Controller final : public ChatsListBoxController {
public:
	Controller(not_null<Main::Session*> session, not_null<UserData*> bot)
	: ChatsListBoxController(session)
	, _bot(bot) {
	}

	Main::Session &session() const override;

	void rowClicked(gsl::not_null<PeerListRow*> row) override;

private:
	std::unique_ptr<Row> createRow(not_null<History*> history) override;
	void prepareViewHook() override;

	void confirmAdd(not_null<PeerData*> peer);
	void confirmRemove(not_null<PeerData*> peer);

	const not_null<UserData*> _bot;

};

void Setup(
		not_null<UserData*> bot,
		not_null<PeerData*> peer,
		QString description,
		Fn<void(QString)> done) {
	using Flag = MTPbots_SetCustomVerification::Flag;
	bot->session().api().request(MTPbots_SetCustomVerification(
		MTP_flags(Flag::f_bot
			| Flag::f_enabled
			| (description.isEmpty() ? Flag() : Flag::f_custom_description)),
		bot->inputUser,
		peer->input,
		MTP_string(description)
	)).done([=] {
		done(QString());
	}).fail([=](const MTP::Error &error) {
		done(error.type());
	}).send();
}

void Remove(
		not_null<UserData*> bot,
		not_null<PeerData*> peer,
		Fn<void(QString)> done) {
	bot->session().api().request(MTPbots_SetCustomVerification(
		MTP_flags(MTPbots_SetCustomVerification::Flag::f_bot),
		bot->inputUser,
		peer->input,
		MTPstring()
	)).done([=] {
		done(QString());
	}).fail([=](const MTP::Error &error) {
		done(error.type());
	}).send();
}

Main::Session &Controller::session() const {
	return _bot->session();
}

void Controller::rowClicked(gsl::not_null<PeerListRow*> row) {
	const auto peer = row->peer();
	const auto details = peer->botVerifyDetails();
	const auto already = details && (details->botId == peerToUser(_bot->id));
	if (already) {
		confirmRemove(peer);
	} else {
		confirmAdd(peer);
	}
}

void Controller::confirmAdd(not_null<PeerData*> peer) {
	const auto bot = _bot;
	const auto show = delegate()->peerListUiShow();
	show->show(Box([=](not_null<Ui::GenericBox*> box) {
		struct State {
			Ui::InputField *field = nullptr;
			QString description;
			bool sent = false;
		};
		const auto settings = bot->botInfo
			? bot->botInfo->verifierSettings.get()
			: nullptr;
		const auto modify = settings && settings->canModifyDescription;
		const auto state = std::make_shared<State>(State{
			.description = settings ? settings->customDescription : QString()
		});

		const auto limit = session().appConfig().get<int>(
			u"bot_verification_description_length_limit"_q,
			70);
		const auto send = [=] {
			if (modify && state->description.size() > limit) {
				state->field->showError();
				return;
			} else if (state->sent) {
				return;
			}
			state->sent = true;
			const auto weak = Ui::MakeWeak(box);
			const auto description = modify ? state->description : QString();
			Setup(bot, peer, description, [=](QString error) {
				if (error.isEmpty()) {
					if (const auto strong = weak.data()) {
						strong->closeBox();
					}
					show->showToast({
						.text = PeerVerifyPhrases(peer).sent(
							tr::now,
							lt_name,
							Ui::Text::Bold(peer->shortName()),
							Ui::Text::WithEntities),
						.duration = kSetupVerificationToastDuration,
					});
				} else {
					state->sent = false;
					show->showToast(error);
				}
			});
		};

		const auto phrases = PeerVerifyPhrases(peer);
		Ui::ConfirmBox(box, {
			.text = phrases.text(
				lt_name,
				rpl::single(Ui::Text::Bold(peer->shortName())),
				Ui::Text::WithEntities),
			.confirmed = send,
			.confirmText = phrases.submit(),
			.title = phrases.title(),
		});
		if (!modify) {
			return;
		}

		Ui::AddSubsectionTitle(
			box->verticalLayout(),
			tr::lng_bot_verify_description_label(),
			QMargins(0, 0, 0, -st::defaultSubsectionTitlePadding.bottom()));

		const auto field = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::createPollField,
			Ui::InputField::Mode::NoNewlines,
			rpl::single(state->description),
			state->description
		), st::createPollFieldPadding);
		state->field = field;

		box->setFocusCallback([=] {
			field->setFocusFast();
		});

		Ui::AddSkip(box->verticalLayout());

		field->changes() | rpl::start_with_next([=] {
			state->description = field->getLastText();
		}, field->lifetime());

		field->setMaxLength(limit * 2);
		Ui::AddLengthLimitLabel(field, limit, std::nullopt);

		Ui::AddDividerText(box->verticalLayout(), phrases.about());
	}));
}

void Controller::confirmRemove(not_null<PeerData*> peer) {
	const auto bot = _bot;
	const auto show = delegate()->peerListUiShow();
	show->show(Box([=](not_null<Ui::GenericBox*> box) {
		const auto sent = std::make_shared<bool>();
		const auto send = [=] {
			if (*sent) {
				return;
			}
			*sent = true;
			const auto weak = Ui::MakeWeak(box);
			Remove(bot, peer, [=](QString error) {
				if (error.isEmpty()) {
					if (const auto strong = weak.data()) {
						strong->closeBox();
					}
					show->showToast(tr::lng_bot_verify_remove_done(tr::now));
				} else {
					*sent = false;
					show->showToast(error);
				}
			});
		};
		Ui::ConfirmBox(box, {
			.text = PeerVerifyPhrases(peer).remove(),
			.confirmed = send,
			.confirmText = tr::lng_bot_verify_remove_submit(),
			.confirmStyle = &st::attentionBoxButton,
			.title = tr::lng_bot_verify_remove_title(),
		});
	}));
}

auto Controller::createRow(not_null<History*> history)
-> std::unique_ptr<Row> {
	const auto peer = history->peer;
	const auto may = peer->isUser() || peer->isChannel();
	return may ? std::make_unique<Row>(history) : nullptr;
}

void Controller::prepareViewHook() {
}

} // namespace

object_ptr<Ui::BoxContent> MakeVerifyPeersBox(
		not_null<Window::SessionController*> window,
		not_null<UserData*> bot) {
	const auto session = &window->session();
	auto controller = std::make_unique<Controller>(session, bot);
	auto init = [=](not_null<PeerListBox*> box) {
		box->setTitle(tr::lng_bot_verify_title());
		box->addButton(tr::lng_box_done(), [=] {
			box->closeBox();
		});
	};
	return Box<PeerListBox>(std::move(controller), std::move(init));
}

BotVerifyPhrases PeerVerifyPhrases(not_null<PeerData*> peer) {
	if (const auto user = peer->asUser()) {
		if (user->isBot()) {
			return {
				.title = tr::lng_bot_verify_bot_title,
				.text = tr::lng_bot_verify_bot_text,
				.about = tr::lng_bot_verify_bot_about,
				.submit = tr::lng_bot_verify_bot_submit,
				.sent = tr::lng_bot_verify_bot_sent,
				.remove = tr::lng_bot_verify_bot_remove,
			};
		} else {
			return {
				.title = tr::lng_bot_verify_user_title,
				.text = tr::lng_bot_verify_user_text,
				.about = tr::lng_bot_verify_user_about,
				.submit = tr::lng_bot_verify_user_submit,
				.sent = tr::lng_bot_verify_user_sent,
				.remove = tr::lng_bot_verify_user_remove,
			};
		}
	} else if (peer->isBroadcast()) {
		return {
			.title = tr::lng_bot_verify_channel_title,
			.text = tr::lng_bot_verify_channel_text,
			.about = tr::lng_bot_verify_channel_about,
			.submit = tr::lng_bot_verify_channel_submit,
			.sent = tr::lng_bot_verify_channel_sent,
			.remove = tr::lng_bot_verify_channel_remove,
		};
	}
	return {
		.title = tr::lng_bot_verify_group_title,
		.text = tr::lng_bot_verify_group_text,
		.about = tr::lng_bot_verify_group_about,
		.submit = tr::lng_bot_verify_group_submit,
		.sent = tr::lng_bot_verify_group_sent,
		.remove = tr::lng_bot_verify_group_remove,
	};
}
