/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class QMimeData;

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class ChatStyle;
class InputField;
class Show;
} // namespace Ui

namespace HistoryView::Controls {
class ComposeAiButton;
} // namespace HistoryView::Controls

namespace Ui {

struct PreparedList;

extern const char kOptionHideAiButton[];

[[nodiscard]] bool HasEnoughLinesForAi(
	not_null<Main::Session*> session,
	not_null<Ui::InputField*> field);

struct SetupCaptionAiButtonArgs {
	not_null<QWidget*> parent;
	not_null<Ui::InputField*> field;
	not_null<Main::Session*> session;
	std::shared_ptr<Ui::Show> show;
	std::shared_ptr<Ui::ChatStyle> chatStyle;
};

[[nodiscard]] auto SetupCaptionAiButton(SetupCaptionAiButtonArgs &&args)
-> not_null<HistoryView::Controls::ComposeAiButton*>;

void UpdateCaptionAiButtonGeometry(
	not_null<HistoryView::Controls::ComposeAiButton*> button,
	not_null<Ui::InputField*> field);

[[nodiscard]] PreparedList PrepareTextAsFile(const QString &text);
[[nodiscard]] int SendAsFilePasteThreshold();

struct LargeTextPasteResult {
	bool exceeds = false;
	QString resultingText;
};

[[nodiscard]] LargeTextPasteResult CheckLargeTextPaste(
	not_null<Ui::InputField*> field,
	not_null<const QMimeData*> data);

} // namespace Ui
