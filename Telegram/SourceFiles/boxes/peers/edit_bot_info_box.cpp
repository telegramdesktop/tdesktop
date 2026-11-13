/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_bot_info_box.h"

#include "api/api_bot_info.h"
#include "apiwrap.h"
#include "base/options.h"
#include "boxes/compose_ai_box.h"
#include "boxes/peers/bot_info_emoji_panel.h"
#include "boxes/peers/bot_info_field.h"
#include "boxes/peers/bot_intro_preview.h"
#include "boxes/peers/edit_bot_info_box_content.h"
#include "chat_helpers/compose/compose_show.h"
#include "core/file_utilities.h"
#include "data/data_user.h"
#include "history/view/controls/history_view_compose_ai_button.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/controls/compose_ai_button_factory.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/fields/input_field.h"

#include "styles/style_chat_helpers.h"

namespace {

void AddAiButton(
		not_null<Ui::GenericBox*> box,
		not_null<UserData*> bot,
		not_null<Ui::InputField*> field,
		Fn<QString()> source) {
	using AiButton = HistoryView::Controls::ComposeAiButton;
	const auto button = Ui::CreateChild<AiButton>(
		field.get(),
		st::historyAiComposeButton);
	button->setAccessibleName(tr::lng_bot_info_action_ai(tr::now));
	button->setClickedCallback([=] {
		const auto text = source();
		if (text.isEmpty()) {
			field->showError();
			return;
		}
		HistoryView::Controls::ShowComposeAiBox(box->uiShow(), {
			.session = &bot->session(),
			.text = tr::marked(text),
			.apply = crl::guard(field, [=](TextWithEntities result) {
				field->setText(result.text);
			}),
			.allowPrompt = true,
		});
	});
	button->show();
	PlaceBotInfoFieldButton(field, button, 1);
}

} // namespace

rpl::producer<QString> BotInfoUnsavedValue(not_null<UserData*> bot) {
	const auto draft = &bot->session().api().botInfoLocalizations().get(
		bot)->draft();
	return rpl::single(
		rpl::empty
	) | rpl::then(draft->updates()) | rpl::map([=] {
		const auto changes = draft->changesCount();
		return changes
			? tr::lng_manage_peer_bot_info_unsaved(
				tr::now,
				lt_count,
				changes)
			: QString();
	});
}

void EditBotInfoBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<UserData*> bot) {
	using EmojiPanel = std::unique_ptr<BotInfoEmojiPanel>;
	using AddAi = Fn<void(not_null<Ui::InputField*>, Fn<QString()>)>;
	const auto emoji = box->lifetime().make_state<EmojiPanel>();
	const auto addAiButton = AddAi([=](
			not_null<Ui::InputField*> field,
			Fn<QString()> source) {
		AddAiButton(box, bot, field, std::move(source));
	});
	const auto hideAi = base::options::value<bool>(Ui::kOptionHideAiButton);
	EditBotInfoBoxContent(box, {
		.localization = bot->session().api().botInfoLocalizations().get(bot),
		.username = bot->username(),
		.introPreview = [=](
				not_null<QWidget*> parent,
				rpl::producer<QString> text) {
			return CreateBotIntroPreview(parent, bot, std::move(text));
		},
		.addEmojiButton = [=](not_null<Ui::InputField*> field) {
			if (!*emoji) {
				*emoji = std::make_unique<BotInfoEmojiPanel>(box, show, bot);
			}
			(*emoji)->attach(field);
		},
		.addAiButton = hideAi ? AddAi() : addAiButton,
		.chooseImportFile = [=](Fn<void(QByteArray, QString)> done) {
			FileDialog::GetOpenPath(
				box.get(),
				tr::lng_bot_languages_import(tr::now),
				u"JSON files (*.json);;"_q + FileDialog::AllFilesFilter(),
				[=](FileDialog::OpenResult &&result) {
					done(
						result.remoteContent,
						result.paths.isEmpty()
							? QString()
							: result.paths.front());
				});
		},
		.chooseExportFile = [=](QString name, Fn<void(QString)> done) {
			FileDialog::GetWritePath(
				box.get(),
				tr::lng_bot_languages_export(tr::now),
				u"JSON files (*.json)"_q,
				name,
				done);
		},
	});
}
