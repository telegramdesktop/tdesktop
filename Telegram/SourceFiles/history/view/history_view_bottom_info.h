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
} // namespace Ui

namespace Data {
class Reactions;
} // namespace Data

namespace HistoryView {
namespace Reactions {
class Animation;
} // namespace Reactions

using PaintContext = Ui::ChatPaintContext;

class Message;
struct TextState;
struct ReactionAnimationArgs;

class BottomInfo final : public Object {
public:
	struct Data {
		enum class Flag : uchar {
			Edited         = 0x01,
			OutLayout      = 0x02,
			Sending        = 0x04,
			RepliesContext = 0x08,
			Sponsored      = 0x10,
			Pinned         = 0x20,
			Imported       = 0x40,
			Recommended    = 0x80,
			//Unread, // We don't want to pass and update it in Date for now.
		};
		friend inline constexpr bool is_flag_type(Flag) { return true; };
		using Flags = base::flags<Flag>;

		QDateTime date;
		QString author;
		base::flat_map<QString, int> reactions;
		QString chosenReaction;
		std::optional<int> views;
		std::optional<int> replies;
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
		ReactionAnimationArgs &&args,
		Fn<void()> repaint);
	[[nodiscard]] auto takeReactionAnimations()
		-> base::flat_map<QString, std::unique_ptr<Reactions::Animation>>;
	void continueReactionAnimations(base::flat_map<
		QString,
		std::unique_ptr<Reactions::Animation>> animations);

private:
	struct Reaction {
		mutable std::unique_ptr<Reactions::Animation> animation;
		mutable QImage image;
		QString emoji;
		QString countText;
		int count = 0;
		int countTextWidth = 0;
	};

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
	[[nodiscard]] Reaction prepareReactionWithEmoji(const QString &emoji);
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
