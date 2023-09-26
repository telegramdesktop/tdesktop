/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_message_reaction_id.h"
#include "ui/effects/animations.h"

namespace Data {
class DocumentMedia;
struct ReactionId;
class Session;
class Story;
struct SuggestedReaction;
} // namespace Data

namespace HistoryView::Reactions {
class Selector;
struct ChosenReaction;
enum class AttachSelectorResult;
} // namespace HistoryView::Reactions

namespace Ui {
class RpWidget;
struct ReactionFlyAnimationArgs;
struct ReactionFlyCenter;
class EmojiFlyAnimation;
class PopupMenu;
} // namespace Ui

namespace Media::Stories {

class Controller;

enum class ReactionsMode {
	Message,
	Reaction,
};

class SuggestedReactionView {
public:
	virtual ~SuggestedReactionView() = default;

	virtual void setAreaGeometry(QRect geometry) = 0;
	virtual void updateCount(int count) = 0;
	virtual void playEffect() = 0;
};

class Reactions final {
public:
	explicit Reactions(not_null<Controller*> controller);
	~Reactions();

	using Mode = ReactionsMode;

	template <typename Reaction>
	struct ChosenWrap {
		Reaction reaction;
		Mode mode;
	};
	using Chosen = ChosenWrap<HistoryView::Reactions::ChosenReaction>;

	[[nodiscard]] rpl::producer<bool> activeValue() const;
	[[nodiscard]] rpl::producer<Chosen> chosen() const;

	[[nodiscard]] Data::ReactionId liked() const;
	[[nodiscard]] rpl::producer<Data::ReactionId> likedValue() const;
	void showLikeFrom(Data::Story *story);

	void hide();
	void outsidePressed();
	void toggleLiked();
	void applyLike(Data::ReactionId id);
	void ready();

	[[nodiscard]] auto makeSuggestedReactionWidget(
		const Data::SuggestedReaction &reaction)
	-> std::unique_ptr<SuggestedReactionView>;

	void setReplyFieldState(
		rpl::producer<bool> focused,
		rpl::producer<bool> hasSendText);
	void attachToReactionButton(not_null<Ui::RpWidget*> button);
	void setReactionIconWidget(Ui::RpWidget *widget);

	using AttachStripResult = HistoryView::Reactions::AttachSelectorResult;
	[[nodiscard]] AttachStripResult attachToMenu(
		not_null<Ui::PopupMenu*> menu,
		QPoint desiredPosition);

private:
	class Panel;

	void animateAndProcess(Chosen &&chosen);

	void assignLikedId(Data::ReactionId id);
	[[nodiscard]] Fn<void(Ui::ReactionFlyCenter)> setLikedIdIconInit(
		not_null<Data::Session*> owner,
		Data::ReactionId id,
		bool force = false);
	void setLikedIdFrom(Data::Story *story);
	void setLikedId(
		not_null<Data::Session*> owner,
		Data::ReactionId id,
		bool force = false);
	void startReactionAnimation(
		Ui::ReactionFlyAnimationArgs from,
		not_null<QWidget*> target,
		Fn<void(Ui::ReactionFlyCenter)> done = nullptr);
	void waitForLikeIcon(
		not_null<Data::Session*> owner,
		Data::ReactionId id);
	void initLikeIcon(
		not_null<Data::Session*> owner,
		Data::ReactionId id,
		Ui::ReactionFlyCenter center);

	const not_null<Controller*> _controller;
	const std::unique_ptr<Panel> _panel;

	rpl::event_stream<Chosen> _chosen;
	bool _replyFocused = false;
	bool _hasSendText = false;

	Ui::RpWidget *_likeIconWidget = nullptr;
	rpl::variable<Data::ReactionId> _liked;
	base::has_weak_ptr _likeIconGuard;
	std::unique_ptr<Ui::RpWidget> _likeIcon;
	std::shared_ptr<Data::DocumentMedia> _likeIconMedia;

	std::unique_ptr<Ui::EmojiFlyAnimation> _reactionAnimation;

	rpl::lifetime _likeIconWaitLifetime;
	rpl::lifetime _likeFromLifetime;
	rpl::lifetime _lifetime;

};

} // namespace Media::Stories
