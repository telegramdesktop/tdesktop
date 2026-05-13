/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/compose_ai_button_factory.h"

#include "base/options.h"
#include "boxes/compose_ai_box.h"
#include "config.h"
#include "core/mime_type.h"
#include "history/view/controls/history_view_compose_ai_button.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/text/text.h"
#include "ui/text/text_entity.h"
#include "ui/widgets/fields/input_field.h"

#include "styles/style_chat_helpers.h"

namespace Ui {

const char kOptionHideAiButton[] = "hide-ai-button";

base::options::toggle HideAiButtonOption({
	.id = kOptionHideAiButton,
	.name = "Hide AI button",
	.description = "Hide the AI Tools button in message compose fields.",
});

bool HasEnoughLinesForAi(
		not_null<Main::Session*> session,
		not_null<Ui::InputField*> field) {
	if (HideAiButtonOption.value()
		|| session->appConfig().aiComposeStyles().empty()) {
		return false;
	}
	const auto &style = field->st().style;
	const auto lineHeight = style.lineHeight
		? style.lineHeight
		: style.font->height;
	const auto margins = field->fullTextMargins();
	const auto contentHeight = field->height()
		- margins.top()
		- margins.bottom();
	if (contentHeight < (3 * lineHeight)) {
		return false;
	}
	const auto &text = field->getLastText();
	if (text.size() > MaxMessageSize) {
		return false;
	}
	for (const auto &ch : text) {
		if (!Text::IsTrimmed(ch) && !Text::IsReplacedBySpace(ch)) {
			return true;
		}
	}
	return false;
}

PreparedList PrepareTextAsFile(const QString &text) {
	auto content = text.toUtf8();
	auto result = PreparedList();
	auto file = PreparedFile(QString());
	file.content = content;
	file.displayName = u"message.txt"_q;
	file.size = content.size();
	file.information = std::make_unique<PreparedFileInformation>();
	file.information->filemime = u"text/plain"_q;
	result.files.push_back(std::move(file));
	return result;
}

constexpr auto kSendAsFilePasteMultiplier = 8;

int SendAsFilePasteThreshold() {
	return kSendAsFilePasteMultiplier * MaxMessageSize;
}

LargeTextPasteResult CheckLargeTextPaste(
		not_null<Ui::InputField*> field,
		not_null<const QMimeData*> data) {
	const auto pasteText = Core::ReadMimeText(data);
	if (pasteText.isEmpty()) {
		return {};
	}
	const auto cursor = field->textCursor();
	const auto currentText = field->getLastText();
	const auto selStart = cursor.selectionStart();
	const auto selEnd = cursor.selectionEnd();
	const auto resultingSize = currentText.size()
		- (selEnd - selStart)
		+ pasteText.size();
	if (resultingSize < SendAsFilePasteThreshold()) {
		return {};
	}
	return {
		.exceeds = true,
		.resultingText = currentText.mid(0, selStart)
			+ pasteText
			+ currentText.mid(selEnd),
	};
}

void UpdateCaptionAiButtonGeometry(
		not_null<HistoryView::Controls::ComposeAiButton*> button,
		not_null<Ui::InputField*> field) {
	if (button->isHidden()) {
		return;
	}
	const auto &pos = st::boxAiComposeButtonPosition;
	const auto x = field->x()
		+ field->width()
		- button->width()
		+ pos.x();
	const auto y = field->y()
		+ field->height()
		- button->height()
		+ pos.y();
	button->moveToLeft(x, y);
}

auto SetupCaptionAiButton(SetupCaptionAiButtonArgs &&args)
-> not_null<HistoryView::Controls::ComposeAiButton*> {
	const auto button = Ui::CreateChild<
		HistoryView::Controls::ComposeAiButton
	>(args.parent.get(), st::historyAiComposeButton);
	const auto field = args.field;
	const auto session = args.session;
	const auto show = std::move(args.show);
	const auto chatStyle = std::move(args.chatStyle);

	button->hide();
	button->setAccessibleName(tr::lng_ai_compose_title(tr::now));

	button->setClickedCallback(crl::guard(field, [=] {
		const auto textWithTags = field->getTextWithAppliedMarkdown();
		if (textWithTags.text.isEmpty()) {
			return;
		}
		auto text = TextWithEntities{
			textWithTags.text,
			TextUtilities::ConvertTextTagsToEntities(textWithTags.tags),
		};
		HistoryView::Controls::ShowComposeAiBox(show, {
			.session = session,
			.text = std::move(text),
			.chatStyle = chatStyle,
			.apply = crl::guard(field, [=](TextWithEntities result) {
				field->setTextWithTags(
					{
						result.text,
						TextUtilities::ConvertEntitiesToTextTags(
							result.entities),
					},
					Ui::InputField::HistoryAction::NewEntry);
			}),
		});
	}));

	const auto updateVisibility = [=] {
		const auto visible = !field->isHidden()
			&& HasEnoughLinesForAi(session, field);
		button->setVisible(visible);
		if (visible) {
			UpdateCaptionAiButtonGeometry(button, field);
			button->raise();
		}
	};

	rpl::merge(
		field->heightChanges() | rpl::to_empty,
		field->changes() | rpl::to_empty,
		field->shownValue() | rpl::to_empty
	) | rpl::on_next([=] {
		updateVisibility();
	}, button->lifetime());

	return button;
}

} // namespace Ui
