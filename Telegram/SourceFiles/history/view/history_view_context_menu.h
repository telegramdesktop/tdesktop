/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "history/view/history_view_element.h"

namespace Data {
class Session;
struct ReactionId;
} // namespace Data

namespace Main {
class Session;
class SessionShow;
} // namespace Main

namespace Ui {
class PopupMenu;
class Show;
enum class ReportReason;
} // namespace Ui

namespace Window {
class SessionNavigation;
class SessionController;
} // namespace Main

namespace HistoryView {

enum class Context : char;
enum class PointState : char;
class ListWidget;
class Element;
struct SelectedItem;
using SelectedItems = std::vector<SelectedItem>;

struct ContextMenuRequest {
	explicit ContextMenuRequest(
		not_null<Window::SessionNavigation*> navigation);

	const not_null<Window::SessionNavigation*> navigation;
	ClickHandlerPtr link;
	Element *view = nullptr;
	HistoryItem *item = nullptr;
	SelectedItems selectedItems;
	TextForMimeData selectedText;
	SelectedQuote quote;
	bool overSelection = false;
	PointState pointState = PointState();
};

base::unique_qptr<Ui::PopupMenu> FillContextMenu(
	not_null<ListWidget*> list,
	const ContextMenuRequest &request);

void InsertPollHiddenResultsLabel(not_null<Ui::PopupMenu*> menu);
void InsertPollVoteRestrictionsLabel(
	not_null<Ui::PopupMenu*> menu,
	not_null<HistoryItem*> item,
	not_null<PollData*> poll);

void CopyPostLink(
	not_null<Window::SessionController*> controller,
	FullMsgId itemId,
	Context context,
	std::optional<TimeId> videoTimestamp = {});
void CopyPostLink(
	std::shared_ptr<Main::SessionShow> show,
	FullMsgId itemId,
	Context context,
	std::optional<TimeId> videoTimestamp = {});
void CopyStoryLink(
	std::shared_ptr<Main::SessionShow> show,
	FullStoryId storyId);
void FillPollOptionPage(
	not_null<Ui::PopupMenu*> menu,
	not_null<Data::Session*> owner,
	FullMsgId itemId,
	const QByteArray &pollOption,
	not_null<Window::SessionController*> controller,
	Fn<void()> replyToOption = nullptr);

void AttachPollOptionTabs(
	not_null<Ui::PopupMenu*> menu,
	QPoint desiredPosition);

[[nodiscard]] std::optional<QString> CurrentVoiceTimecode(FullMsgId itemId);
[[nodiscard]] rpl::producer<QString> VoiceTimecodeUpdates(FullMsgId itemId);

void AddPollActions(
	not_null<Ui::PopupMenu*> menu,
	not_null<PollData*> poll,
	not_null<HistoryItem*> item,
	Context context,
	not_null<Window::SessionController*> controller,
	bool skipRetractVote = false,
	bool skipViewStats = false);
void AddSaveSoundForNotifications(
	not_null<Ui::PopupMenu*> menu,
	not_null<HistoryItem*> item,
	not_null<DocumentData*> document,
	not_null<Window::SessionController*> controller);
void AddWhoReactedAction(
	not_null<Ui::PopupMenu*> menu,
	not_null<QWidget*> context,
	not_null<HistoryItem*> item,
	not_null<Window::SessionController*> controller);
void MaybeAddWhenEditedForwardedAction(
	not_null<Ui::PopupMenu*> menu,
	not_null<HistoryItem*> item,
	not_null<Window::SessionController*> controller);
void ShowWhoReactedMenu(
	not_null<base::unique_qptr<Ui::PopupMenu>*> menu,
	QPoint position,
	not_null<QWidget*> context,
	not_null<HistoryItem*> item,
	const Data::ReactionId &id,
	not_null<Window::SessionController*> controller,
	rpl::lifetime &lifetime);
void ShowTagInListMenu(
	not_null<base::unique_qptr<Ui::PopupMenu>*> menu,
	QPoint position,
	not_null<QWidget*> context,
	const Data::ReactionId &id,
	not_null<Window::SessionController*> controller);
void AddCopyFilename(
	not_null<Ui::PopupMenu*> menu,
	not_null<DocumentData*> document,
	Fn<bool()> showCopyRestrictionForSelected);

enum class EmojiPacksSource {
	Message,
	Reaction,
	Reactions,
	Tag,
	PollOption,
};
[[nodiscard]] std::vector<StickerSetIdentifier> CollectEmojiPacks(
	not_null<HistoryItem*> item,
	EmojiPacksSource source);
void AddEmojiPacksAction(
	not_null<Ui::PopupMenu*> menu,
	std::vector<StickerSetIdentifier> packIds,
	EmojiPacksSource source,
	not_null<Window::SessionController*> controller);
void AddEmojiPacksAction(
	not_null<Ui::PopupMenu*> menu,
	not_null<HistoryItem*> item,
	EmojiPacksSource source,
	not_null<Window::SessionController*> controller);
void AddSelectRestrictionAction(
	not_null<Ui::PopupMenu*> menu,
	not_null<HistoryItem*> item,
	bool addIcon);
void AddEphemeralMessageActions(
	not_null<Ui::PopupMenu*> menu,
	std::shared_ptr<Ui::Show> show,
	not_null<HistoryItem*> item);

[[nodiscard]] TextWithEntities TransribedText(not_null<HistoryItem*> item);

[[nodiscard]] bool ItemHasTtl(HistoryItem *item);

} // namespace HistoryView
