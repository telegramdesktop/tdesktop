/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"
#include "ui/effects/round_area_with_shadow.h"
#include "data/data_message_reaction_id.h"

class HistoryItem;

namespace Data {
struct Reaction;
class DocumentMedia;
} // namespace Data

namespace Lottie {
class Icon;
} // namespace Lottie

namespace HistoryView::Reactions {

struct ChosenReaction {
	FullMsgId context;
	Data::ReactionId id;
	std::shared_ptr<Lottie::Icon> icon;
	QRect geometry;

	explicit operator bool() const {
		return context && !id.empty();
	}
};

using IconFactory = Fn<std::shared_ptr<Lottie::Icon>(
	not_null<Data::DocumentMedia*>,
	int)>;

class Strip final {
public:
	using ReactionId = Data::ReactionId;

	Strip(QRect inner, Fn<void()> update, IconFactory iconFactory);

	enum class AddedButton : uchar {
		None,
		Expand,
		Premium,
	};
	void applyList(
		const std::vector<not_null<const Data::Reaction*>> &list,
		AddedButton button);

	void paint(
		QPainter &p,
		QPoint position,
		QPoint shift,
		QRect clip,
		float64 scale,
		bool hiding);

	[[nodiscard]] bool empty() const;
	[[nodiscard]] int count() const;
	void setSelected(int index) const;
	[[nodiscard]] std::variant<AddedButton, ReactionId> selected() const;
	[[nodiscard]] int computeOverSize() const;

	void clearAppearAnimations(bool mainAppeared = true);

	int fillChosenIconGetIndex(ChosenReaction &chosen) const;

	[[nodiscard]] bool onlyAddedButton() const;
	[[nodiscard]] bool onlyMainEmojiVisible() const;
	Ui::ImageSubrect validateEmoji(int frameIndex, float64 scale);

private:
	static constexpr auto kFramesCount
		= Ui::RoundAreaWithShadow::kFramesCount;

	using ReactionId = ::Data::ReactionId;

	struct ReactionDocument {
		std::shared_ptr<Data::DocumentMedia> media;
		std::shared_ptr<Lottie::Icon> icon;
	};
	struct ReactionIcons {
		ReactionId id;
		DocumentData *appearAnimation = nullptr;
		DocumentData *selectAnimation = nullptr;
		std::shared_ptr<Lottie::Icon> appear;
		std::shared_ptr<Lottie::Icon> select;
		mutable Ui::Animations::Simple selectedScale;
		AddedButton added = AddedButton::None;
		bool appearAnimated = false;
		mutable bool selected = false;
		mutable bool selectAnimated = false;
	};

	void clearStateForHidden(ReactionIcons &icon);
	void paintPremiumIcon(QPainter &p, QPoint position, QRectF target) const;
	void paintExpandIcon(QPainter &p, QPoint position, QRectF target) const;
	void clearStateForSelectFinished(ReactionIcons &icon);

	[[nodiscard]] bool checkIconLoaded(ReactionDocument &entry) const;
	void loadIcons();
	void checkIcons();

	void resolveMainReactionIcon();
	void setMainReactionIcon();

	const IconFactory _iconFactory;
	const QRect _inner;
	const int _finalSize = 0;
	Fn<void()> _update;

	std::vector<ReactionIcons> _icons;
	AddedButton _button = AddedButton::None;
	base::flat_map<not_null<DocumentData*>, ReactionDocument> _loadCache;
	std::optional<ReactionIcons> _premiumIcon;
	rpl::lifetime _loadCacheLifetime;

	mutable int _selectedIcon = -1;

	std::shared_ptr<Data::DocumentMedia> _mainReactionMedia;
	std::shared_ptr<Lottie::Icon> _mainReactionIcon;
	QImage _mainReactionImage;
	rpl::lifetime _mainReactionLifetime;

	QImage _emojiParts;
	std::array<bool, kFramesCount> _validEmoji = { { false } };

};

class CachedIconFactory final {
public:
	CachedIconFactory() = default;
	CachedIconFactory(const CachedIconFactory &other) = delete;
	CachedIconFactory &operator=(const CachedIconFactory &other) = delete;

	[[nodiscard]] IconFactory createMethod();

private:
	base::flat_map<
		std::shared_ptr<Data::DocumentMedia>,
		std::shared_ptr<Lottie::Icon>> _cache;

};

[[nodiscard]] std::shared_ptr<Lottie::Icon> DefaultIconFactory(
	not_null<Data::DocumentMedia*> media,
	int size);

} // namespace HistoryView
