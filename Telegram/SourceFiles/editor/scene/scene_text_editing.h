/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "editor/photo_editor_inner_common.h"

class QGraphicsScene;
class QGraphicsTextItem;

namespace Editor {

class ItemText;
class NumberedItem;
class Scene;
struct TextLayoutSpec;

class TextEditController final {
public:
	explicit TextEditController(not_null<Scene*> scene);

	void setDefaults(
		const QColor &color,
		float64 fontSize,
		TextStyle style,
		TextTypeface typeface,
		TextAlignment alignment);
	void applyPrefs(const TextPrefs &prefs);
	void noteItemPrefs(not_null<ItemText*> item);
	void setColor(const QColor &color);

	void createAtCenter(int rotation, bool flipped);
	void startEditing(ItemText *item);
	void finishEditing(bool save, bool notify = true);

	[[nodiscard]] bool editing() const;
	[[nodiscard]] bool proxyContains(const QPointF &scenePos) const;

	[[nodiscard]] rpl::producer<QColor> colorRequests() const;
	[[nodiscard]] rpl::producer<bool> editStates() const;
	[[nodiscard]] rpl::producer<TextPrefs> prefsUsed() const;

private:
	void setEditingState(bool editing, bool notify = true);
	void firePrefs();
	void applyAutoShrink();
	[[nodiscard]] int sessionMaxTextWidth() const;
	[[nodiscard]] int sessionMinTextWidth() const;
	[[nodiscard]] int sessionWrapWidth() const;
	void setupProxy(
		QGraphicsTextItem *proxy,
		const QColor &color,
		const TextLayoutSpec &spec,
		TextAlignment alignment);
	[[nodiscard]] not_null<QGraphicsScene*> graphicsScene() const;

	const not_null<Scene*> _scene;

	QColor _defaultColor;
	float64 _defaultFontSize = 0.;
	TextStyle _defaultStyle = TextStyle::Plain;
	TextStyle _editStyle = TextStyle::Plain;
	TextTypeface _defaultTypeface = TextTypeface::Default;
	TextTypeface _editTypeface = TextTypeface::Default;
	TextAlignment _defaultAlignment = TextAlignment::Center;
	TextAlignment _editAlignment = TextAlignment::Center;

	struct {
		std::weak_ptr<NumberedItem> item;
		base::unique_qptr<QGraphicsTextItem> proxy;
		std::optional<QColor> color;
		float64 fontSize = 0.;
		int maxWidthFloor = 0;
		bool flipped = false;
	} _edit;

	rpl::event_stream<QColor> _colorRequests;
	rpl::event_stream<bool> _editStates;
	rpl::event_stream<TextPrefs> _prefsUsed;
	bool _editingState = false;
	int _generation = 0;

};

} // namespace Editor
