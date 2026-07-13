/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/suggestions/suggestion.h"

#include "boxes/star_gift_box.h"
#include "data/components/promo_suggestions.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "dialogs/ui/dialogs_top_bar_suggestion_content.h"
#include "history/view/history_view_group_call_bar.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/controls/userpic_button.h"
#include "ui/painter.h"
#include "ui/text/text_utilities.h"
#include "ui/ui_utility.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"

namespace Dialogs::TopBarSuggestions {
namespace {

constexpr auto kSugBirthdayContacts = "BIRTHDAY_CONTACTS_TODAY"_cs;

bool Available(const Context &context) {
	const auto session = context.session.get();
	return session->premiumCanBuy()
		&& session->promoSuggestions().current(kSugBirthdayContacts.utf8());
}

void Activate(ActivateArgs args) {
	const auto session = args.context.session.get();
	const auto promo = &session->promoSuggestions();
	const auto content = args.context.ensureContent();
	const auto findController = args.context.findController;
	const auto recompute = args.recompute;
	const auto done = args.done;

	const auto alive = args.lifetime->make_state<base::has_weak_ptr>();
	auto ready = crl::guard(content.get(), [=] {
		const auto users = promo->knownBirthdaysToday().value_or(
			std::vector<UserId>());
		if (users.empty()) {
			recompute();
			return;
		}
		const auto controller = findController();
		const auto isSingle = users.size() == 1;
		const auto first = session->data().user(users.front());
		using RightIcon = TopBarSuggestionContent::RightIcon;
		content->setRightIcon(RightIcon::Close);
		content->setClickedCallback([=] {
			if (isSingle) {
				Ui::ShowStarGiftBox(controller, first);
			} else {
				Ui::ChooseStarGiftRecipient(controller);
			}
		});
		content->setHideCallback([=] {
			promo->dismiss(kSugBirthdayContacts.utf8());
			controller->showToast(
				tr::lng_dialogs_suggestions_birthday_contact_dismiss(
					tr::now));
			recompute();
		});
		auto title = isSingle
			? tr::lng_dialogs_suggestions_birthday_contact_title(
				tr::now,
				lt_text,
				{ first->shortName() },
				tr::rich)
			: tr::lng_dialogs_suggestions_birthday_contacts_title(
				tr::now,
				lt_count,
				users.size(),
				tr::rich);
		auto text = isSingle
			? tr::lng_dialogs_suggestions_birthday_contact_about(
				tr::now,
				TextWithEntities::Simple)
			: tr::lng_dialogs_suggestions_birthday_contacts_about(
				tr::now,
				TextWithEntities::Simple);
		content->setContent(std::move(title), std::move(text));
		if (!isSingle) {
			struct UserViews {
				std::vector<HistoryView::UserpicInRow> inRow;
				QImage userpics;
			};
			auto inRow = std::vector<HistoryView::UserpicInRow>();
			for (const auto &id : users) {
				if (inRow.size() >= 3) {
					break;
				}
				if (const auto user = session->data().user(id)) {
					inRow.push_back({ .peer = user });
				}
			}
			const auto &userpicsSt = st::historyCommentsUserpics;
			const auto rowCount = int(inRow.size());
			const auto rowWidth = rowCount * userpicsSt.size
				- userpicsSt.shift;
			const auto rowHeight = userpicsSt.size;
			const auto widget = Ui::CreateChild<Ui::RpWidget>(content.get());
			widget->resize(rowWidth, rowHeight);
			const auto s = widget->lifetime().make_state<UserViews>();
			s->inRow = std::move(inRow);
			widget->paintOn([=](QPainter &p) {
				if (HistoryView::NeedRegenerateUserpics(
						s->userpics,
						s->inRow)) {
					HistoryView::GenerateUserpicsInRow(
						s->userpics,
						s->inRow,
						st::historyCommentsUserpics,
						3);
				}
				p.drawImage(0, 0, s->userpics);
			});
			content->setLeadingWidget(widget);
		} else {
			const auto fake = Ui::CreateChild<Ui::UserpicButton>(
				content.get(),
				first,
				st::uploadUserpicButton);
			content->setLeadingWidget(fake);
		}
		done(content, [content] { content->prepareCollapseSnapshot(); });
	});
	promo->requestContactBirthdays(crl::guard(alive, std::move(ready)));
}

} // namespace

Spec MakeBirthdayContactsSpec() {
	return {
		.priority = Priority::BirthdayContacts,
		.available = Available,
		.activate = Activate,
		.dayDependent = true,
	};
}

} // namespace Dialogs::TopBarSuggestions
