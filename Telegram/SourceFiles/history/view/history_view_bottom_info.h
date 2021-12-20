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
class Session;
class DocumentMedia;
} // namespace Data

namespace HistoryView {

using PaintContext = Ui::ChatPaintContext;

class Message;
struct TextState;

class BottomInfo final : public Object {
public:
	struct Data {
		enum class Flag : uchar {
			Edited         = 0x01,
			OutLayout      = 0x02,
			Sending        = 0x04,
			RepliesContext = 0x08,
			Sponsored      = 0x10,
			//Unread, // We don't want to pass and update it in Date for now.
		};
		friend inline constexpr bool is_flag_type(Flag) { return true; };
		using Flags = base::flags<Flag>;

		not_null<::Data::Session*> owner;
		QDateTime date;
		QString author;
		base::flat_map<QString, int> reactions;
		QString chosenReaction;
		std::optional<int> views;
		std::optional<int> replies;
		Flags flags;
	};
	explicit BottomInfo(Data &&data);

	void update(Data &&data, int availableWidth);

	[[nodiscard]] int firstLineWidth() const;
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

private:
	struct Reaction {
		QImage image;
		QString emoji;
		std::shared_ptr<::Data::DocumentMedia> media;
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
		int left,
		int top,
		int availableWidth) const;

	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

	void setReactionCount(Reaction &reaction, int count);
	void loadReactionImage(Reaction &reaction, not_null<DocumentData*> document);
	void setReactionImage(Reaction &reaction, QImage large);
	[[nodiscard]] Reaction prepareReactionWithEmoji(const QString &emoji);

	void reactionsListLoaded();
	void downloadTaskFinished();
	[[nodiscard]] bool assetsLoaded() const;

	Data _data;
	Ui::Text::String _authorEditedDate;
	Ui::Text::String _views;
	Ui::Text::String _replies;
	std::vector<Reaction> _reactions;
	int _reactionsMaxWidth = 0;
	int _dateWidth = 0;

	rpl::lifetime _assetsLoadLifetime;
	bool _waitingForReactionsList = false;
	bool _waitingForDownloadTask = false;

	bool _authorElided = false;

};

[[nodiscard]] BottomInfo::Data BottomInfoDataFromMessage(
	not_null<Message*> message);

} // namespace HistoryView
