/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/qt/qt_compare.h"
#include "base/timer.h"
#include "chat_helpers/compose/compose_features.h"
#include "ui/widgets/fields/input_field.h"

#ifndef TDESKTOP_DISABLE_SPELLCHECK
#include "boxes/dictionaries_manager.h"
#include "spellcheck/spelling_highlighter.h"
#endif // TDESKTOP_DISABLE_SPELLCHECK

#include <QtGui/QClipboard>

namespace tr {
struct now_t;
} // namespace tr

namespace Main {
class Session;
class SessionShow;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window

namespace ChatHelpers {
enum class PauseReason;
class Show;
} // namespace ChatHelpers

namespace HistoryView::Controls {
struct WriteRestriction;
} // namespace HistoryView::Controls

namespace Ui {
class GenericBox;
class PopupMenu;
class Show;
} // namespace Ui

[[nodiscard]] QString PrepareMentionTag(not_null<UserData*> user);
[[nodiscard]] TextWithTags PrepareEditText(not_null<HistoryItem*> item);
[[nodiscard]] bool EditTextChanged(
	not_null<HistoryItem*> item,
	TextWithTags updated);

Fn<bool(
	Ui::InputField::EditLinkSelection selection,
	TextWithTags text,
	QString link,
	Ui::InputField::EditLinkAction action)> DefaultEditLinkCallback(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::InputField*> field,
		const style::InputField *fieldStyle = nullptr);
Fn<void(QString now, Fn<void(QString)> save)> DefaultEditLanguageCallback(
	std::shared_ptr<Ui::Show> show);

struct MessageFieldHandlersArgs {
	not_null<Main::Session*> session;
	std::shared_ptr<Main::SessionShow> show; // may be null
	not_null<Ui::InputField*> field;
	Fn<bool()> customEmojiPaused;
	Fn<bool(not_null<DocumentData*>)> allowPremiumEmoji;
	const style::InputField *fieldStyle = nullptr;
	base::flat_set<QString> allowMarkdownTags;
};
void InitMessageFieldHandlers(MessageFieldHandlersArgs &&args);

void InitMessageFieldHandlers(
	not_null<Window::SessionController*> controller,
	not_null<Ui::InputField*> field,
	ChatHelpers::PauseReason pauseReasonLevel,
	Fn<bool(not_null<DocumentData*>)> allowPremiumEmoji = nullptr);
void InitMessageField(
	std::shared_ptr<ChatHelpers::Show> show,
	not_null<Ui::InputField*> field,
	Fn<bool(not_null<DocumentData*>)> allowPremiumEmoji);
void InitMessageField(
	not_null<Window::SessionController*> controller,
	not_null<Ui::InputField*> field,
	Fn<bool(not_null<DocumentData*>)> allowPremiumEmoji);

void InitSpellchecker(
	std::shared_ptr<Main::SessionShow> show,
	not_null<Ui::InputField*> field,
	bool skipDictionariesManager = false);

[[nodiscard]] Fn<void(not_null<Ui::InputField*>)> FactcheckFieldIniter(
	std::shared_ptr<Main::SessionShow> show);

bool HasSendText(not_null<const Ui::InputField*> field);

void InitMessageFieldFade(
	not_null<Ui::InputField*> field,
	const style::color &bg);

struct InlineBotQuery {
	QString query;
	QString username;
	UserData *bot = nullptr;
	bool lookingUpBot = false;
};
InlineBotQuery ParseInlineBotQuery(
	not_null<Main::Session*> session,
	not_null<const Ui::InputField*> field);

struct AutocompleteQuery {
	QString query;
	bool fromStart = false;
};
AutocompleteQuery ParseMentionHashtagBotCommandQuery(
	not_null<const Ui::InputField*> field,
	ChatHelpers::ComposeFeatures features);

struct MessageLinkRange {
	int start = 0;
	int length = 0;
	QString custom;

	friend inline auto operator<=>(
		const MessageLinkRange&,
		const MessageLinkRange&) = default;
	friend inline bool operator==(
		const MessageLinkRange&,
		const MessageLinkRange&) = default;
};

class MessageLinksParser final : private QObject {
public:
	MessageLinksParser(not_null<Ui::InputField*> field);

	void parseNow();
	void setDisabled(bool disabled);

	[[nodiscard]] const rpl::variable<QStringList> &list() const {
		return _list;
	}
	[[nodiscard]] const std::vector<MessageLinkRange> &ranges() const {
		return _ranges;
	}

private:
	bool eventFilter(QObject *object, QEvent *event) override;

	void parse();
	void applyRanges(const QString &text);

	not_null<Ui::InputField*> _field;
	rpl::variable<QStringList> _list;
	std::vector<MessageLinkRange> _ranges;
	int _lastLength = 0;
	bool _disabled = false;
	base::Timer _timer;
	rpl::lifetime _lifetime;

};

[[nodiscard]] base::unique_qptr<Ui::RpWidget> CreateDisabledFieldView(
	QWidget *parent,
	not_null<PeerData*> peer);
[[nodiscard]] std::unique_ptr<Ui::RpWidget> TextErrorSendRestriction(
	QWidget *parent,
	const QString &text);
[[nodiscard]] std::unique_ptr<Ui::RpWidget> PremiumRequiredSendRestriction(
	QWidget *parent,
	not_null<UserData*> user,
	not_null<Window::SessionController*> controller);
[[nodiscard]] auto BoostsToLiftWriteRestriction(
	not_null<QWidget*> parent,
	std::shared_ptr<ChatHelpers::Show> show,
	not_null<PeerData*> peer,
	int boosts)
-> std::unique_ptr<Ui::AbstractButton>;

struct FreezeInfoStyleOverride {
	const style::Box *box = nullptr;
	const style::FlatLabel *title = nullptr;
	const style::FlatLabel *subtitle = nullptr;
	const style::icon *violationIcon = nullptr;
	const style::icon *readOnlyIcon = nullptr;
	const style::icon *appealIcon = nullptr;
	const style::FlatLabel *infoTitle = nullptr;
	const style::FlatLabel *infoAbout = nullptr;
};
[[nodiscard]] FreezeInfoStyleOverride DarkFreezeInfoStyle();

enum class FrozenWriteRestrictionType {
	MessageField,
	DialogsList,
};
[[nodiscard]] std::unique_ptr<Ui::AbstractButton> FrozenWriteRestriction(
	not_null<QWidget*> parent,
	std::shared_ptr<ChatHelpers::Show> show,
	FrozenWriteRestrictionType type,
	FreezeInfoStyleOverride st = {});

void SelectTextInFieldWithMargins(
	not_null<Ui::InputField*> field,
	const TextSelection &selection);

[[nodiscard]] TextWithEntities PaidSendButtonText(tr::now_t, int stars);
[[nodiscard]] rpl::producer<TextWithEntities> PaidSendButtonText(
	rpl::producer<int> stars,
	rpl::producer<QString> fallback = nullptr);

void FrozenInfoBox(
	not_null<Ui::GenericBox*> box,
	not_null<Main::Session*> session,
	FreezeInfoStyleOverride st);
