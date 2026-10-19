/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/event_filter.h"
#include "base/weak_ptr.h"
#include "data/data_story.h"
#include "wallet/wallet_card_stickers.h"
#include "wallet/wallet_collectibles.h"

class QKeyEvent;
class QMouseEvent;

namespace Main {
class SessionShow;
} // namespace Main

namespace Ui {
class AbstractButton;
class RpWidget;
} // namespace Ui

namespace Wallet {

class OwnCard;
struct OwnCardState;

struct CardEditorArgs {
	std::shared_ptr<Main::SessionShow> show;
	CardHost host;
	not_null<Ui::AbstractButton*> input;
	not_null<Ui::RpWidget*> header;
	CollectiblesSquares squares;
	Fn<void(QRect)> repaint;
};

class CardStickerEditor final : public base::has_weak_ptr {
public:
	explicit CardStickerEditor(CardEditorArgs &&args);
	~CardStickerEditor();

	void updateInput();
	void paint(QPainter &p, const CardFold &fold);

private:
	struct Press;
	struct Travel;
	struct Placed {
		QString address;
		Data::StoryArea area;
	};

	[[nodiscard]] base::EventFilterResult filterInput(not_null<QEvent*> e);
	[[nodiscard]] base::EventFilterResult filterPanel(not_null<QEvent*> e);
	[[nodiscard]] base::EventFilterResult mousePress(
		not_null<QMouseEvent*> e);
	[[nodiscard]] base::EventFilterResult mouseMove(
		not_null<QMouseEvent*> e);
	[[nodiscard]] base::EventFilterResult mouseRelease(
		not_null<QMouseEvent*> e);
	[[nodiscard]] base::EventFilterResult escape(not_null<QKeyEvent*> e);
	void startPress(
		QPoint origin,
		QPointF point,
		const QString &address,
		bool handle);
	[[nodiscard]] bool updateHandle(
		QPointF global,
		Qt::KeyboardModifiers modifiers);
	void finishHandle(const Press &press);
	void finishClick(const Press &press, QPointF point);
	void startMove(QPointF global);
	[[nodiscard]] bool updateMove(QPointF global);
	void finishMove(const Press &press, QPointF global);
	void dropMoved(const Press &press, QPointF point);
	void removeMoved(bool collectibles);
	void returnMoved();
	void settleMoved(bool land);
	void settled(uint64 generation, bool land);
	void removed(uint64 generation);
	void retireTravel();
	void finishTravel();
	void showLeaving(QPointF center);
	void showRefusal(OwnCardChange change);
	[[nodiscard]] OwnCardChange saveArea(
		const QString &address,
		Data::StoryArea area);
	[[nodiscard]] OwnCardChange saveStacked(OwnCardState desired);
	void reconcile(const std::vector<Data::WalletCardSticker> &stickers);
	void select(const QString &address);
	void cancelPress();
	void clear();
	void abandon(std::unique_ptr<Press> press);
	void releaseGrab();
	void updateGrip();
	void refresh();
	[[nodiscard]] bool moving() const;
	[[nodiscard]] bool current(uint64 generation) const;
	[[nodiscard]] bool handleAt(const CardFold &fold, QPointF point) const;
	[[nodiscard]] QString stickerAt(const CardFold &fold, QPointF point) const;
	[[nodiscard]] std::optional<Data::StoryArea> shownArea(
		const QString &address) const;
	[[nodiscard]] std::optional<Data::StoryArea> decorationArea() const;
	[[nodiscard]] std::optional<FlightPose> cardPose(
		const QString &address) const;
	[[nodiscard]] QRectF listsArea() const;
	[[nodiscard]] QPointF bodyPoint(QPointF global) const;
	[[nodiscard]] OwnCard &ownCard() const;

	const std::shared_ptr<Main::SessionShow> _show;
	const CardHost _host;
	const not_null<Ui::AbstractButton*> _input;
	const not_null<Ui::RpWidget*> _header;
	const CollectiblesSquares _squares;
	const Fn<void(QRect)> _repaint;
	std::vector<Placed> _placed;
	std::unique_ptr<Press> _press;
	std::unique_ptr<Travel> _travel;
	QString _selected;
	QString _gripped;
	QRect _painted;
	uint64 _lastGeneration = 0;
	bool _transactions = false;
	bool _escapeHeld = false;
	rpl::lifetime _lifetime;

};

} // namespace Wallet
