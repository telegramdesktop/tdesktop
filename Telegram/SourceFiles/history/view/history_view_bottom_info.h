/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/history_view_object.h"
#include "ui/text/text.h"
#include "base/flags.h"

namespace Ui {
struct ChatPaintContext;
class AnimatedIcon;
struct ReactionFlyAnimationArgs;
class ReactionFlyAnimation;
} // namespace Ui

namespace Data {
class Reactions;
struct ReactionId;
struct MessageReaction;
} // namespace Data

namespace HistoryView {

using PaintContext = Ui::ChatPaintContext;

class Message;
struct TextState;

class BottomInfo final : public Object {
public:
	using ReactionId = ::Data::ReactionId;
	using MessageReaction = ::Data::MessageReaction;
	struct Data {
		enum class Flag : uchar {
			Edited         = 0x01,
			OutLayout      = 0x02,
			Sending        = 0x04,
			RepliesContext = 0x08,
			Sponsored      = 0x10,
			Pinned         = 0x20,
			Imported       = 0x40,
			Shortcut       = 0x80,
			//Unread, // We don't want to pass and update it in Date for now.
		};
		friend inline constexpr bool is_flag_type(Flag) { return true; };
		using Flags = base::flags<Flag>;

		QDateTime date;
		QString author;
		std::vector<MessageReaction> reactions;
		std::optional<int> views;
		std::optional<int> replies;
		std::optional<int> forwardsCount;
		Flags flags;
	};
	BottomInfo(not_null<::Data::Reactions*> reactionsOwner, Data &&data);
	~BottomInfo();

	void update(Data &&data, int availableWidth);

	[[nodiscard]] int firstLineWidth() const;
	[[nodiscard]] bool isWide() const;
	[[nodiscard]] TextState textState(
		not_null<const HistoryItem*> item,
		QPoint position) const;
	[[nodiscard]] bool isSignedAuthorElided() const;

	void paint(
		Painter &p,
		QPoint position,
		int outerWidth,
		bool unread,
		bool inverted,
		const PaintContext &context) const;

	void animateReaction(
		Ui::ReactionFlyAnimationArgs &&args,
		Fn<void()> repaint);
	[[nodiscard]] auto takeReactionAnimations()
		-> base::flat_map<
			ReactionId,
			std::unique_ptr<Ui::ReactionFlyAnimation>>;
	void continueReactionAnimations(base::flat_map<
		ReactionId,
		std::unique_ptr<Ui::ReactionFlyAnimation>> animations);

private:
	struct Reaction;

	void layout();
	void layoutDateText();
	void layoutViewsText();
	void layoutRepliesText();
	void layoutReactionsText();

	[[nodiscard]] int countReactionsMaxWidth() const;
	[[nodiscard]] int countReactionsHeight(int newWidth) const;
	void paintReactions(
		Painter &p,
		QPoint origin,
		int left,
		int top,
		int availableWidth,
		const PaintContext &context) const;

	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

	void setReactionCount(Reaction &reaction, int count);
	[[nodiscard]] Reaction prepareReactionWithId(
		const ReactionId &id);
	[[nodiscard]] ClickHandlerPtr revokeReactionLink(
		not_null<const HistoryItem*> item,
		QPoint position) const;
	[[nodiscard]] ClickHandlerPtr revokeReactionLink(
		not_null<const HistoryItem*> item) const;

	const not_null<::Data::Reactions*> _reactionsOwner;
	Data _data;
	Ui::Text::String _authorEditedDate;
	Ui::Text::String _views;
	Ui::Text::String _replies;
	std::vector<Reaction> _reactions;
	mutable ClickHandlerPtr _revokeLink;
	int _reactionsMaxWidth = 0;
	bool _authorElided = false;

};

[[nodiscard]] BottomInfo::Data BottomInfoDataFromMessage(
	not_null<Message*> message);

} // namespace HistoryView
