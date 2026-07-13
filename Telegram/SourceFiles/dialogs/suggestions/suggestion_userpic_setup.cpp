/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/suggestions/suggestion.h"

#include "api/api_peer_photo.h"
#include "apiwrap.h"
#include "data/components/promo_suggestions.h"
#include "data/data_changes.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "dialogs/ui/dialogs_top_bar_suggestion_content.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/controls/userpic_button.h"
#include "ui/text/text_utilities.h"
#include "ui/ui_utility.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_dialogs.h"

namespace Dialogs::TopBarSuggestions {
namespace {

constexpr auto kSugSetUserpic = "USERPIC_SETUP"_cs;

bool Available(const Context &context) {
	const auto session = context.session.get();
	return session->promoSuggestions().current(kSugSetUserpic.utf8())
		&& !session->user()->userpicPhotoId();
}

void Activate(ActivateArgs args) {
	const auto session = args.context.session.get();
	const auto promo = &session->promoSuggestions();
	const auto content = args.context.ensureContent();
	const auto controller = args.context.findController();
	const auto recompute = args.recompute;
	using RightIcon = TopBarSuggestionContent::RightIcon;
	content->setRightIcon(RightIcon::Close);
	const auto upload = Ui::CreateChild<Ui::UserpicButton>(
		content.get(),
		&controller->window(),
		Ui::UserpicButton::Role::ChoosePhoto,
		st::uploadUserpicButton);
	content->setLeadingWidget(upload);
	upload->chosenImages() | rpl::on_next([=](
			Ui::UserpicButton::ChosenImage &&chosen) {
		if (chosen.type == Ui::UserpicButton::ChosenType::Set) {
			session->api().peerPhoto().upload(
				session->user(),
				{
					std::move(chosen.image),
					chosen.markup.documentId,
					chosen.markup.colors,
				});
		}
	}, upload->lifetime());

	session->changes().peerUpdates(
		session->user(),
		Data::PeerUpdate::Flag::Photo
	) | rpl::on_next([=] {
		if (session->user()->userpicPhotoId()) {
			recompute();
		}
	}, *args.lifetime);

	content->setHideCallback([=] {
		promo->dismiss(kSugSetUserpic.utf8());
		recompute();
	});

	content->setClickedCallback([=] {
		const auto syntetic = [=](QEvent::Type type) {
			Ui::SendSynteticMouseEvent(
				upload,
				type,
				Qt::LeftButton,
				upload->mapToGlobal(QPoint(0, 0)));
		};
		syntetic(QEvent::MouseMove);
		syntetic(QEvent::MouseButtonPress);
		syntetic(QEvent::MouseButtonRelease);
	});
	content->setContent(
		tr::lng_dialogs_suggestions_userpics_title(
			tr::now,
			tr::bold),
		tr::lng_dialogs_suggestions_userpics_about(
			tr::now,
			TextWithEntities::Simple));
	args.done(content, [content] { content->prepareCollapseSnapshot(); });
}

} // namespace

Spec MakeUserpicSetupSpec() {
	return {
		.priority = Priority::UserpicSetup,
		.available = Available,
		.activate = Activate,
	};
}

} // namespace Dialogs::TopBarSuggestions
