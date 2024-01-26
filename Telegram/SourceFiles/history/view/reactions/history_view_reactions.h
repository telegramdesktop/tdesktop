/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/history_view_object.h"
#include "data/data_message_reaction_id.h"

namespace Data {
class Reactions;
} // namespace Data

namespace Ui {
struct ChatPaintContext;
struct ReactionFlyAnimationArgs;
class ReactionFlyAnimation;
} // namespace Ui

namespace Ui::Text {
class CustomEmoji;
} // namespace Ui::Text

namespace HistoryView {
using PaintContext = Ui::ChatPaintContext;
class Message;
struct TextState;
struct UserpicInRow;
} // namespace HistoryView

namespace HistoryView::Reactions {

using ::Data::ReactionId;
using ::Data::MessageReaction;

struct InlineListData {
	enum class Flag : uchar {
		InBubble  = 0x01,
		OutLayout = 0x02,
		Flipped   = 0x04,
		Tags      = 0x08,
	};
	friend inline constexpr bool is_flag_type(Flag) { return true; };
	using Flags = base::flags<Flag>;

	std::vector<MessageReaction> reactions;
	base::flat_map<ReactionId, std::vector<not_null<PeerData*>>> recent;
	Flags flags = {};
};

class InlineList final : public Object {
public:
	using Data = InlineListData;
	InlineList(
		not_null<::Data::Reactions*> owner,
		Fn<ClickHandlerPtr(ReactionId)> handlerFactory,
		Fn<void()> customEmojiRepaint,
		Data &&data);
	~InlineList();

	void update(Data &&data, int availableWidth);
	QSize countCurrentSize(int newWidth) override;
	[[nodiscard]] int countNiceWidth() const;
	[[nodiscard]] int placeAndResizeGetHeight(QRect available);
	void flipToRight();

	void updateSkipBlock(int width, int height);
	void removeSkipBlock();

	[[nodiscard]] bool areTags() const;
	[[nodiscard]] std::vector<ReactionId> computeTagsList() const;
	[[nodiscard]] bool hasCustomEmoji() const;
	void unloadCustomEmoji();

	void paint(
		Painter &p,
		const PaintContext &context,
		int outerWidth,
		const QRect &clip) const;
	[[nodiscard]] bool getState(
		QPoint point,
		not_null<TextState*> outResult) const;

	void animate(
		Ui::ReactionFlyAnimationArgs &&args,
		Fn<void()> repaint);
	[[nodiscard]] auto takeAnimations()
	-> base::flat_map<
		ReactionId,
		std::unique_ptr<Ui::ReactionFlyAnimation>>;
	void continueAnimations(base::flat_map<
		ReactionId,
		std::unique_ptr<Ui::ReactionFlyAnimation>> animations);

	[[nodiscard]] static float64 TagDotAlpha();
	[[nodiscard]] static QImage PrepareTagBg(QColor tagBg, QColor dotBg);

private:
	struct Userpics {
		QImage image;
		std::vector<UserpicInRow> list;
		bool someNotLoaded = false;
	};
	struct Button;

	void layout();
	void layoutButtons();

	void setButtonTag(Button &button, const QString &title);
	void setButtonCount(Button &button, int count);
	void setButtonUserpics(
		Button &button,
		const std::vector<not_null<PeerData*>> &peers);
	[[nodiscard]] Button prepareButtonWithId(const ReactionId &id);
	void resolveUserpicsImage(const Button &button) const;
	void paintCustomFrame(
		Painter &p,
		not_null<Ui::Text::CustomEmoji*> emoji,
		QPoint innerTopLeft,
		const PaintContext &context,
		const QColor &textColor) const;
	void paintSingleBg(
		Painter &p,
		const QRect &fill,
		const QColor &color,
		float64 opacity) const;

	void validateTagBg(const QColor &color) const;

	QSize countOptimalSize() override;

	const not_null<::Data::Reactions*> _owner;
	const Fn<ClickHandlerPtr(ReactionId)> _handlerFactory;
	const Fn<void()> _customEmojiRepaint;
	Data _data;
	std::vector<Button> _buttons;
	QSize _skipBlock;
	mutable QImage _tagBg;
	mutable QColor _tagBgColor;
	mutable QImage _customCache;
	mutable int _customSkip = 0;
	bool _hasCustomEmoji = false;

};

[[nodiscard]] InlineListData InlineListDataFromMessage(
	not_null<Message*> message);

} // namespace HistoryView
