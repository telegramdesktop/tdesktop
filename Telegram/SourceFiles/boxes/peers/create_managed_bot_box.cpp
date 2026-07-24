/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/create_managed_bot_box.h"

#include "base/timer.h"
#include "boxes/peers/edit_peer_common.h"
#include "chat_helpers/compose/compose_show.h"
#include "data/data_premium_limits.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/session/session_show.h"
#include "main/main_session.h"
#include "mtproto/sender.h"
#include "settings/sections/settings_premium.h"
#include "ui/controls/userpic_button.h"
#include "ui/layers/generic_box.h"
#include "ui/toast/toast.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/fields/special_fields.h"
#include "ui/widgets/labels.h"
#include "ui/vertical_list.h"

#include "styles/style_boxes.h"
#include "styles/style_layers.h"

void CreateManagedBotBox(
		not_null<Ui::GenericBox*> box,
		CreateManagedBotDescriptor &&descriptor) {
	struct State {
		base::Timer checkTimer;
		mtpRequestId checkRequestId = 0;
		mtpRequestId createRequestId = 0;
		QString checkUsername;
		QString errorText;
		QString goodText;
		bool created = false;
		bool hadOccupiedError = false;
	};
	const auto show = descriptor.show;
	const auto session = &show->session();
	const auto viaDeeplink = descriptor.viaDeeplink;
	const auto done = std::move(descriptor.done);
	const auto cancelled = std::move(descriptor.cancelled);
	const auto manager = descriptor.manager;

	const auto api = box->lifetime().make_state<MTP::Sender>(
		&session->mtp());
	const auto state = box->lifetime().make_state<State>();

	box->setStyle(st::createBotBox);
	box->setWidth(st::boxWidth);
	box->setNoContentMargin(true);
	box->addTopButton(st::boxTitleClose, [=] {
		box->closeBox();
	});

	auto userpic = object_ptr<Ui::UserpicButton>(
		box,
		manager,
		st::defaultUserpicButton);
	userpic->setAttribute(Qt::WA_TransparentForMouseEvents);
	box->addRow(
		std::move(userpic),
		st::boxRowPadding + st::createBotUserpicPadding,
		style::al_top);

	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_create_bot_title(),
			st::boxTitle),
		st::boxRowPadding + st::createBotTitlePadding,
		style::al_top);

	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_create_bot_subtitle(
				lt_bot,
				rpl::single(tr::bold(manager->name())),
				tr::rich),
			st::createBotCenteredText),
		st::boxRowPadding + st::createBotSubtitlePadding,
		style::al_top
	)->setTryMakeSimilarLines(true);

	const auto name = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		tr::lng_create_bot_name_placeholder(),
		descriptor.suggestedName));
	name->setMaxLength(Ui::EditPeer::kMaxGroupChannelTitle);

	Ui::AddSkip(box->verticalLayout(), st::createBotFieldSpacing);

	const auto botPrefixText = u"@"_q;
	const auto botSuffixText = u"bot"_q;
	const auto suffixWidth
		= st::createBotUsernameSuffix.style.font->width(botSuffixText);

	auto initialUsername = descriptor.suggestedUsername;
	while (initialUsername.startsWith(botPrefixText)) {
		initialUsername.remove(0, botPrefixText.size());
	}

	const auto fieldSt = box->lifetime().make_state<style::InputField>(
		st::createBotUsernameField);
	fieldSt->textMargins.setLeft(
		st::defaultFlatLabel.style.font->width(botPrefixText));
	fieldSt->textMargins.setRight(suffixWidth);
	fieldSt->placeholderMargins.setLeft(-fieldSt->textMargins.left());
	fieldSt->placeholderMargins.setRight(-fieldSt->textMargins.right());
	const auto usernameWrap = box->addRow(object_ptr<Ui::RpWidget>(box));
	const auto username = Ui::CreateChild<Ui::UsernameInput>(
		usernameWrap,
		*fieldSt,
		tr::lng_create_bot_username_placeholder(),
		initialUsername,
		QString());
	username->setPlaceholderHidden(true);
	username->setMaxLength(Ui::EditPeer::kMaxUsernameLength);
	usernameWrap->widthValue() | rpl::on_next([=](int width) {
		username->resizeToWidth(width);
	}, username->lifetime());
	username->heightValue() | rpl::on_next([=](int height) {
		usernameWrap->resize(usernameWrap->width(), height);
	}, username->lifetime());
	username->finishAnimating();

	const auto botPrefix = Ui::CreateChild<Ui::FlatLabel>(
		username,
		botPrefixText,
		st::createBotUsernamePrefix);
	const auto botSuffix = Ui::CreateChild<Ui::FlatLabel>(
		username,
		botSuffixText,
		st::createBotUsernameSuffix);
	botPrefix->setAttribute(Qt::WA_TransparentForMouseEvents);
	botPrefix->show();
	botSuffix->setAttribute(Qt::WA_TransparentForMouseEvents);
	botSuffix->show();

	const auto updatePositions = [=] {
		const auto &margin = fieldSt->textMargins;
		const auto &font = fieldSt->style.font;
		const auto text = username->getLastText();
		const auto textWidth = font->width(text);
		const auto maxX = username->width() - margin.right();
		const auto x = std::min(margin.left() + textWidth, maxX);
		botPrefix->move(0, margin.top());
		botSuffix->move(x, margin.top());
	};

	username->geometryValue(
	) | rpl::on_next(updatePositions, username->lifetime());

	const auto cleanedUsername = [=] {
		auto raw = username->getLastText().trimmed();
		while (raw.startsWith(botPrefixText)) {
			raw = raw.mid(botPrefixText.size());
		}
		return raw;
	};
	// How many trailing characters of `value` already spell the beginning
	// of "bot" (in any capitalization): "..b" -> 1, "..bo" -> 2, "..bot" -> 3.
	const auto botOverlap = [=](const QString &value) {
		const auto bound = std::min(
			int(value.size()),
			int(botSuffixText.size()));
		for (auto k = bound; k > 0; --k) {
			if (!value.right(k).compare(
					botSuffixText.left(k),
					Qt::CaseInsensitive)) {
				return k;
			}
		}
		return 0;
	};
	// The lowercase remainder of "bot" we will append on save: shown as the
	// non-editable label and empty once the value already ends with "bot".
	const auto missingSuffix = [=](const QString &value) {
		return botSuffixText.mid(botOverlap(value));
	};
	const auto fullUsername = [=] {
		const auto raw = cleanedUsername();
		return raw + missingSuffix(raw);
	};

	const auto refreshSuffix = [=] {
		const auto suffix = missingSuffix(cleanedUsername());
		botSuffix->setText(suffix);
		botSuffix->setVisible(!suffix.isEmpty());
	};

	// We keep as many entered characters as fit together with the minimal
	// auto-added suffix (so "..b" leaves room for "ot", "..bo" for "t",
	// "..bot" for nothing). setMaxLength can't express this: reserving space
	// for the full "bot" would block typing the very "b" that shrinks it.
	const auto enforceLength = [=] {
		const auto text = username->getLastText();
		auto fitted = text;
		while (!fitted.isEmpty()
			&& (int(fitted.size()) + int(missingSuffix(fitted).size())
				> Ui::EditPeer::kMaxUsernameLength)) {
			fitted.chop(1);
		}
		if (fitted != text) {
			username->setText(fitted);
			return true;
		}
		return false;
	};

	enforceLength();
	refreshSuffix();

	const auto statusWrapper = box->addRow(
		object_ptr<Ui::RpWidget>(box),
		st::boxRowPadding + QMargins(0, st::defaultVerticalListSkip, 0, 0));

	const auto statusLabel = Ui::CreateChild<Ui::FlatLabel>(
		statusWrapper,
		st::createBotStatusLabel);
	statusLabel->move(0, 0);

	const auto maxHeight = box->lifetime().make_state<int>(0);
	statusLabel->heightValue(
	) | rpl::on_next([=](int height) {
		const auto newMax = std::max({
			*maxHeight,
			height,
			st::createBotStatusLabel.style.font->height,
		});
		if (*maxHeight != newMax) {
			*maxHeight = newMax;
			statusWrapper->resize(statusWrapper->width(), newMax);
		}
	}, statusWrapper->lifetime());

	statusWrapper->widthValue(
	) | rpl::on_next([=](int width) {
		statusLabel->resizeToWidth(width);
	}, statusWrapper->lifetime());

	box->setFocusCallback([=] { name->setFocusFast(); });

	const auto setError = [=](const QString &text) {
		state->errorText = text;
		state->goodText = QString();
		statusLabel->setText(text);
		statusLabel->setTextColorOverride(
			text.isEmpty()
				? std::optional<QColor>()
				: st::boxTextFgError->c);
		state->checkTimer.cancel();
	};

	const auto showLinkInfo = [=] {
		const auto full = fullUsername();
		const auto text = tr::lng_create_bot_username_link(
			tr::now,
			lt_link,
			u"t.me/"_q + full);
		state->errorText = QString();
		state->goodText = text;
		statusLabel->setText(text);
		statusLabel->setTextColorOverride(st::usernameDefaultFg->c);
	};

	const auto checkUsername = [=] {
		api->request(base::take(state->checkRequestId)).cancel();

		const auto value = fullUsername();
		if (value.size() < Ui::EditPeer::kMinUsernameLength) {
			return;
		}
		state->checkUsername = value;
		state->checkRequestId = api->request(MTPbots_CheckUsername(
			MTP_string(value)
		)).done([=](const MTPBool &result) {
			state->checkRequestId = 0;
			if (mtpIsTrue(result)) {
				if (state->hadOccupiedError) {
					state->errorText = QString();
					state->goodText = tr::lng_create_bot_username_available(
						tr::now,
						lt_username,
						state->checkUsername);
					statusLabel->setText(state->goodText);
					statusLabel->setTextColorOverride(st::boxTextFgGood->c);
				} else {
					showLinkInfo();
				}
			} else {
				state->hadOccupiedError = true;
				setError(tr::lng_create_bot_username_taken(tr::now));
			}
		}).fail([=](const MTP::Error &error) {
			state->checkRequestId = 0;
			state->hadOccupiedError = true;
			setError(tr::lng_create_bot_username_taken(tr::now));
		}).send();
	};

	state->checkTimer.setCallback(checkUsername);

	const auto usernameChanged = [=] {
		const auto value = username->getLastText().trimmed();
		if (value.isEmpty()) {
			setError(QString());
			return;
		}

		const auto len = int(value.size());
		for (auto i = 0; i < len; ++i) {
			const auto ch = value.at(i);
			if ((ch < 'A' || ch > 'Z')
				&& (ch < 'a' || ch > 'z')
				&& (ch < '0' || ch > '9')
				&& ch != '_') {
				setError(
					tr::lng_create_bot_username_bad_symbols(tr::now));
				return;
			}
		}

		if (fullUsername().size() < Ui::EditPeer::kMinUsernameLength) {
			setError(tr::lng_create_bot_username_too_short(tr::now));
			return;
		}

		showLinkInfo();
		state->checkTimer.callOnce(Ui::EditPeer::kUsernameCheckTimeout);
	};

	const auto submit = [=] {
		if (state->createRequestId) {
			return;
		}

		const auto nameValue = name->getLastText().trimmed();
		if (nameValue.isEmpty()) {
			name->showError();
			return;
		}

		if (cleanedUsername().isEmpty()) {
			username->showError();
			return;
		}
		const auto usernameValue = fullUsername();

		if (!state->errorText.isEmpty() || state->goodText.isEmpty()) {
			username->showError();
			return;
		}

		using Flag = MTPbots_CreateBot::Flag;
		const auto flags = viaDeeplink ? Flag::f_via_deeplink : Flag(0);
		const auto weak = base::make_weak(box);
		state->createRequestId = api->request(MTPbots_CreateBot(
			MTP_flags(flags),
			MTP_string(nameValue),
			MTP_string(usernameValue),
			manager->inputUser()
		)).done([=](const MTPUser &result) {
			state->createRequestId = 0;
			const auto user = session->data().processUser(result);
			state->created = true;
			if (done) {
				done(user);
			}
			if (const auto strong = weak.get()) {
				strong->closeBox();
			}
		}).fail([=](const MTP::Error &error) {
			state->createRequestId = 0;
			const auto type = error.type();
			if (type == u"USERNAME_OCCUPIED"_q
				|| type == u"USERNAME_INVALID"_q) {
				state->hadOccupiedError = true;
				username->showError();
				setError(tr::lng_create_bot_username_taken(tr::now));
			} else if (type == u"BOT_CREATE_LIMIT_EXCEEDED"_q) {
				const auto limits = Data::PremiumLimits(session);
				const auto premium = session->premium();
				const auto premiumPossible = session->premiumPossible();
				const auto defaultLimit = limits.botsCreateDefault();
				const auto premiumLimit = limits.botsCreatePremium();
				const auto current = premium ? premiumLimit : defaultLimit;
				const auto bot = tr::link(
					u"@BotFather"_q,
					u"https://t.me/botfather?start=deletebot"_q);
				if (premium || !premiumPossible) {
					using WeakToast = base::weak_ptr<Ui::Toast::Instance>;
					const auto toast = std::make_shared<WeakToast>();
					(*toast) = show->showToast({
						.text = tr::lng_bots_create_limit_final(
							tr::now,
							lt_count,
							current,
							lt_bot,
							tr::bold(bot),
							tr::rich),
						.filter = crl::guard(session, [=](
								const ClickHandlerPtr &,
								Qt::MouseButton button) {
							if (button == Qt::LeftButton) {
								if (const auto strong = toast->get()) {
									strong->hideAnimated();
									(*toast) = nullptr;
								}
							}
							return true;
						}),
					});
				} else {
					Settings::ShowPremiumPromoToast(
						show,
						ChatHelpers::ResolveWindowDefault(),
						tr::lng_bots_create_limit(
							tr::now,
							lt_count,
							current,
							lt_link,
							tr::bold(
								tr::lng_bots_create_limit_link(tr::now, tr::link)),
							lt_premium_count,
							tr::bold(QString::number(premiumLimit)),
							lt_bot,
							tr::bold(bot),
							tr::rich),
						u"managed_bots"_q);
				}
			} else if (MTP::IsFloodError(error)) {
				show->showToast(tr::lng_flood_error(tr::now));
			} else {
				show->showToast(type);
			}
		}).handleFloodErrors().send();
	};

	QObject::connect(username, &Ui::UsernameInput::changed, [=] {
		if (enforceLength()) {
			return;
		}
		refreshSuffix();
		usernameChanged();
		updatePositions();
	});

	name->submits(
	) | rpl::on_next([=](auto) {
		username->setFocus();
	}, name->lifetime());

	QObject::connect(username, &Ui::UsernameInput::submitted, submit);

	box->addButton(tr::lng_create_bot_button(), submit);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });

	if (!descriptor.suggestedUsername.isEmpty()) {
		usernameChanged();
	}

	box->boxClosing() | rpl::on_next([=] {
		if (!state->created && cancelled) {
			cancelled();
		}
	}, box->lifetime());
}

void ShowCreateManagedBotBox(CreateManagedBotDescriptor &&descriptor) {
	const auto show = descriptor.show;
	show->showBox(Box(CreateManagedBotBox, std::move(descriptor)));
}
