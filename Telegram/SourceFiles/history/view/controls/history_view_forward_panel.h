/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/history.h"
#include "ui/text/text.h"
#include "base/weak_ptr.h"

class Painter;
class HistoryItem;

namespace Ui {
class SpoilerAnimation;
} // namespace Ui

namespace Data {
class Thread;
struct WebPageDraft;
} // namespace Data

namespace Window {
class SessionController;
} // namespace Window

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace HistoryView::Controls {

class ForwardPanel final : public base::has_weak_ptr {
public:
	explicit ForwardPanel(Fn<void()> repaint);

	void update(Data::Thread *to, Data::ResolvedForwardDraft draft);
	void paint(
		Painter &p,
		int x,
		int y,
		int available,
		int outerWidth) const;

	[[nodiscard]] rpl::producer<> itemsUpdated() const;

	void editOptions(std::shared_ptr<ChatHelpers::Show> show);
	void editToNextOption();

	[[nodiscard]] const HistoryItemsList &items() const;
	[[nodiscard]] bool empty() const;

private:
	void checkTexts();
	void updateTexts();
	void refreshTexts();
	void itemRemoved(not_null<const HistoryItem*> item);

	Fn<void()> _repaint;

	Data::Thread *_to = nullptr;
	Data::ResolvedForwardDraft _data;
	rpl::lifetime _dataLifetime;

	rpl::event_stream<> _itemsUpdated;
	Ui::Text::String _from, _text;
	mutable std::unique_ptr<Ui::SpoilerAnimation> _spoiler;
	int _nameVersion = 0;

};

void ClearDraftReplyTo(
	not_null<History*> history,
	MsgId topicRootId,
	FullMsgId equalTo);

void EditWebPageOptions(
	std::shared_ptr<ChatHelpers::Show> show,
	not_null<WebPageData*> webpage,
	Data::WebPageDraft draft,
	Fn<void(Data::WebPageDraft)> done);

} // namespace HistoryView::Controls
