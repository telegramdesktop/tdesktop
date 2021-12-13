/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/history_view_object.h"
#include "ui/effects/animations.h"
#include "ui/widgets/inner_dropdown.h"

class Image;

namespace Ui {
class ChatStyle;
struct ChatPaintContext;
} // namespace Ui

namespace Data {
struct Reaction;
class DocumentMedia;
} // namespace Data

namespace HistoryView {

using PaintContext = Ui::ChatPaintContext;
enum class PointState : char;
struct TextState;
struct StateRequest;
class Message;

class Reactions final : public Object {
public:
	struct Data {
		base::flat_map<QString, int> reactions;
		QString chosenReaction;
	};

	explicit Reactions(Data &&data);

	void update(Data &&data, int availableWidth);
	QSize countCurrentSize(int newWidth) override;

	void updateSkipBlock(int width, int height);
	void removeSkipBlock();

	void paint(
		Painter &p,
		const Ui::ChatStyle *st,
		int outerWidth,
		const QRect &clip) const;

private:
	void layout();
	void layoutReactionsText();

	QSize countOptimalSize() override;

	Data _data;
	Ui::Text::String _reactions;

};

[[nodiscard]] Reactions::Data ReactionsDataFromMessage(
	not_null<Message*> message);

class ReactButton final {
public:
	ReactButton(Fn<void()> update, Fn<void()> react, QRect bubble);

	void updateGeometry(QRect bubble);
	[[nodiscard]] int bottomOutsideMargin(int fullHeight) const;
	[[nodiscard]] std::optional<PointState> pointState(QPoint point) const;
	[[nodiscard]] std::optional<TextState> textState(
		QPoint point,
		const StateRequest &request) const;

	void paint(Painter &p, const PaintContext &context);

	void toggle(bool shown);
	[[nodiscard]] bool isHidden() const;
	void show(not_null<const Data::Reaction*> reaction);

private:
	const Fn<void()> _update;
	const ClickHandlerPtr _handler;
	QRect _geometry;
	bool _shown = false;
	Ui::Animations::Simple _shownAnimation;

	QImage _image;
	QPoint _imagePosition;
	std::shared_ptr<Data::DocumentMedia> _media;
	rpl::lifetime _downloadTaskLifetime;

};

class ReactionsMenu final {
public:
	ReactionsMenu(
		QWidget *parent,
		const std::vector<Data::Reaction> &list);

	void showAround(QRect area);
	void toggle(bool shown, anim::type animated);

	[[nodiscard]] rpl::producer<QString> chosen() const;

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	struct Element {
		QString emoji;
		QRect geometry;
	};
	Ui::InnerDropdown _dropdown;
	rpl::event_stream<QString> _chosen;
	std::vector<Element> _elements;
	bool _fromTop = true;
	bool _fromLeft = true;

};

class ReactionsMenuManager final {
public:
	explicit ReactionsMenuManager(QWidget *parent);
	~ReactionsMenuManager();

	struct Chosen {
		FullMsgId context;
		QString emoji;
	};

	void showReactionsMenu(
		FullMsgId context,
		QRect globalReactionArea,
		const std::vector<Data::Reaction> &list);
	void hideAll(anim::type animated);

	[[nodiscard]] rpl::producer<Chosen> chosen() const {
		return _chosen.events();
	}

private:
	QWidget *_parent = nullptr;
	rpl::event_stream<Chosen> _chosen;

	std::unique_ptr<ReactionsMenu> _menu;
	FullMsgId _context;
	std::vector<Data::Reaction> _list;
	std::vector<std::unique_ptr<ReactionsMenu>> _hiding;

};

} // namespace HistoryView
