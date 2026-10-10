/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "data/data_compose_stash.h"

class History;
class SendFilesBox;
struct SendFilesStashed;

namespace Api {
struct SendOptions;
} // namespace Api

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Main {
class Session;
} // namespace Main

namespace SendMenu {
struct Details;
} // namespace SendMenu

namespace HistoryView {
class CornerButtons;
} // namespace HistoryView

namespace HistoryView::Controls {

struct StashManagerDescriptor {
	not_null<Main::Session*> session;
	not_null<CornerButtons*> buttons;
	std::shared_ptr<ChatHelpers::Show> show;
	Fn<History*()> history;
	Fn<Data::DraftKey()> key;
	Fn<bool()> allowed;
	Fn<bool()> hasContent;
	Fn<bool()> canSendTexts;
	Fn<std::unique_ptr<Data::ComposeStash>()> take;
	Fn<void(Data::ComposeStash&&)> apply;
	Fn<SuggestOptions()> suggest;
	Fn<void()> clearComposer;
	Fn<SendFilesBox*(Ui::PreparedList&)> openFiles;
	Fn<bool(const Ui::PreparedList&)> filesError;
	Fn<SendMenu::Details()> menuDetails;
	Fn<void(Api::SendOptions)> send;
};

class StashManager final : public base::has_weak_ptr {
public:
	explicit StashManager(StashManagerDescriptor &&descriptor);
	~StashManager();

	[[nodiscard]] bool canExchange() const;
	bool exchange();

	[[nodiscard]] bool canTakeFromBox() const;
	void takeFromBox(SendFilesStashed &&stashed);

	void updateButton();

private:
	[[nodiscard]] Data::ComposeStash *current() const;
	[[nodiscard]] bool applicable(const Data::ComposeStash &stash) const;
	[[nodiscard]] bool checkApplicable(const Data::ComposeStash &stash) const;
	void applyStash(Data::ComposeStash &&stash);
	void keepFiles(Ui::PreparedList &&files);
	void remove();
	void sendStashed(Api::SendOptions options);

	const StashManagerDescriptor _data;
	QPointer<SendFilesBox> _openedBox;
	rpl::lifetime _lifetime;

};

} // namespace HistoryView::Controls
