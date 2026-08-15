/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {
struct AiComposeTone;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class AbstractButton;
class FlatLabel;
class GenericBox;
class InputField;
class Show;
class VerticalLayout;
} // namespace Ui

// Turns an Ui::InputField into a tone-style island: a rounded opaque
// background painted behind the field plus a gray placeholder label that
// fades and shifts on typing. Returns the placeholder label so callers can
// observe its height (e.g. for auto-grow min-height computations).
not_null<Ui::FlatLabel*> AddAiComposeFieldDecor(
	not_null<Ui::InputField*> field,
	rpl::producer<QString> placeholder);

not_null<Ui::AbstractButton*> AddAiToneIconPreview(
	not_null<Ui::VerticalLayout*> container,
	not_null<Main::Session*> session,
	rpl::producer<DocumentId> emojiIdValue,
	Fn<void(DocumentId)> emojiIdChosen = nullptr);

void CreateAiToneBox(
	not_null<Ui::GenericBox*> box,
	not_null<Main::Session*> session,
	Fn<void(Data::AiComposeTone)> saved = nullptr);

void EditAiToneBox(
	not_null<Ui::GenericBox*> box,
	not_null<Main::Session*> session,
	const Data::AiComposeTone &tone,
	Fn<void(Data::AiComposeTone)> saved = nullptr);

void ConfirmDeleteAiTone(
	std::shared_ptr<Ui::Show> show,
	not_null<Main::Session*> session,
	const Data::AiComposeTone &tone,
	Fn<void()> done = nullptr);

void ShowAiComposeToneLimitError(
	std::shared_ptr<Ui::Show> show,
	not_null<Main::Session*> session);
