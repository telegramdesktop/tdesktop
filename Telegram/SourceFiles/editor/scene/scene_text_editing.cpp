/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/scene/scene_text_editing.h"

#include "editor/scene/scene.h"
#include "editor/scene/scene_emoji_document.h"
#include "editor/scene/scene_item_text.h"
#include "ui/painter.h"
#include "styles/style_editor.h"

#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

namespace Editor {
namespace {

constexpr auto kMinWidthFactor = 0.16;
constexpr auto kIdealWidthExtra = 2;
constexpr auto kScaleThreshold = 0.01;

class TextEditProxy final : public QGraphicsTextItem {
public:
	using QGraphicsTextItem::QGraphicsTextItem;

	Fn<void()> onFinish;
	Fn<void()> onCancel;

	void setStyleInfo(
			TextStyle style,
			const QColor &color,
			float64 fontSize) {
		prepareGeometryChange();
		_style = style;
		_color = color;
		_fontSize = fontSize;
		update();
	}

	void setStyleColor(const QColor &color) {
		_color = color;
		update();
	}

	QRectF boundingRect() const override {
		const auto pad = float64(TextBackgroundPadding(_fontSize, _style));
		return QGraphicsTextItem::boundingRect().adjusted(
			-pad,
			-pad,
			pad,
			pad);
	}

	void paint(
			QPainter *painter,
			const QStyleOptionGraphicsItem *option,
			QWidget *widget) override {
		paintBackground(painter);
		QGraphicsTextItem::paint(painter, option, widget);
	}

protected:
	void keyPressEvent(QKeyEvent *event) override {
		if (event->key() == Qt::Key_Escape) {
			fire(onCancel);
			return;
		}
		QGraphicsTextItem::keyPressEvent(event);
	}

	void focusOutEvent(QFocusEvent *event) override {
		QGraphicsTextItem::focusOutEvent(event);
		fire(onFinish);
	}

	void contextMenuEvent(QGraphicsSceneContextMenuEvent *event) override {
		event->accept();
	}

private:
	void paintBackground(QPainter *painter) {
		const auto bg = TextBackgroundColor(_color, _style);
		if (bg.alpha() <= 0) {
			return;
		}
		auto lines = std::vector<TextBackgroundLine>();
		const auto doc = document();
		for (auto block = doc->begin()
			; block.isValid()
			; block = block.next()) {
			const auto layout = block.layout();
			if (!layout) {
				continue;
			}
			const auto origin = layout->position();
			for (auto i = 0; i < layout->lineCount(); ++i) {
				// naturalTextRect includes draw-time alignment shift.
				const auto rect = layout->lineAt(i).naturalTextRect();
				lines.push_back({
					.left = origin.x() + rect.left(),
					.top = origin.y() + rect.top(),
					.right = origin.x() + rect.right(),
					.bottom = origin.y() + rect.bottom(),
				});
			}
		}
		if (lines.empty()) {
			return;
		}
		const auto path = BuildTextBackgroundPath(
			std::move(lines),
			_fontSize);
		painter->save();
		PainterHighQualityEnabler hq(*painter);
		painter->setPen(Qt::NoPen);
		painter->setBrush(bg);
		painter->drawPath(path);
		painter->restore();
	}

	void fire(Fn<void()> &callback) {
		if (!callback) {
			return;
		}
		const auto cb = std::exchange(callback, nullptr);
		onFinish = nullptr;
		onCancel = nullptr;
		crl::on_main(cb);
	}

	TextStyle _style = TextStyle::Plain;
	QColor _color;
	float64 _fontSize = 0.;
};

} // namespace

TextEditController::TextEditController(not_null<Scene*> scene)
: _scene(scene) {
}

not_null<QGraphicsScene*> TextEditController::graphicsScene() const {
	return _scene.get();
}

void TextEditController::setDefaults(
		const QColor &color,
		float64 fontSize,
		TextStyle style,
		TextTypeface typeface,
		TextAlignment alignment) {
	_defaultColor = color;
	_defaultFontSize = fontSize;
	_defaultStyle = style;
	_defaultTypeface = typeface;
	_defaultAlignment = alignment;
}

void TextEditController::applyPrefs(const TextPrefs &prefs) {
	_defaultStyle = prefs.style;
	_defaultTypeface = prefs.typeface;
	_defaultAlignment = prefs.alignment;
	if (prefs.sizeRatio > 0.) {
		const auto rect = _scene->sceneRect();
		const auto shortSide = std::min(rect.width(), rect.height());
		_defaultFontSize = std::max(prefs.sizeRatio * shortSide, 1.);
	}
}

void TextEditController::noteItemPrefs(not_null<ItemText*> item) {
	_defaultStyle = item->textStyle();
	_defaultTypeface = item->typeface();
	_defaultAlignment = item->alignment();
	_defaultFontSize = item->fontSize();
	firePrefs();
}

void TextEditController::firePrefs() {
	const auto rect = _scene->sceneRect();
	const auto shortSide = std::min(rect.width(), rect.height());
	_prefsUsed.fire({
		.style = _defaultStyle,
		.typeface = _defaultTypeface,
		.alignment = _defaultAlignment,
		.sizeRatio = (shortSide > 0.)
			? (_defaultFontSize / shortSide)
			: 0.,
	});
}

void TextEditController::setColor(const QColor &color) {
	if (_edit.proxy) {
		_edit.color = color;
		_edit.proxy->setDefaultTextColor(EffectiveTextColor(
			color,
			_editStyle));
		static_cast<TextEditProxy*>(_edit.proxy.get())->setStyleColor(color);
	} else {
		_defaultColor = color;
	}
}

bool TextEditController::editing() const {
	return _edit.proxy != nullptr;
}

bool TextEditController::proxyContains(const QPointF &scenePos) const {
	return _edit.proxy
		&& _edit.proxy->contains(_edit.proxy->mapFromScene(scenePos));
}

rpl::producer<QColor> TextEditController::colorRequests() const {
	return _colorRequests.events();
}

rpl::producer<bool> TextEditController::editStates() const {
	return _editStates.events();
}

rpl::producer<TextPrefs> TextEditController::prefsUsed() const {
	return _prefsUsed.events();
}

void TextEditController::setEditingState(bool editing, bool notify) {
	if (_editingState == editing) {
		return;
	}
	_editingState = editing;
	if (notify) {
		_editStates.fire_copy(editing);
	}
}

void TextEditController::setupProxy(
		QGraphicsTextItem *proxy,
		const QColor &color,
		const TextLayoutSpec &spec,
		TextAlignment alignment) {
	proxy->setTextInteractionFlags(Qt::TextEditorInteraction);
	proxy->setDefaultTextColor(color);

	auto *emojiDoc = new EmojiDocument(proxy);
	emojiDoc->setDocumentMargin(0);
	proxy->setDocument(emojiDoc);
	proxy->setFont(spec.font);

	{
		auto option = emojiDoc->defaultTextOption();
		option.setAlignment((alignment == TextAlignment::Left)
			? Qt::AlignLeft
			: (alignment == TextAlignment::Right)
			? Qt::AlignRight
			: Qt::AlignCenter);
		option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
		emojiDoc->setDefaultTextOption(option);
	}
}

void TextEditController::createAtCenter(int rotation, bool flipped) {
	if (_edit.proxy) {
		finishEditing(true);
	}

	const auto generation = ++_generation;

	_scene->clearSelection();
	_scene->cancelDrawing();
	setEditingState(true);
	_editStyle = _defaultStyle;
	_editTypeface = _defaultTypeface;
	_editAlignment = _defaultAlignment;
	_edit.flipped = flipped;

	const auto sceneRect = _scene->sceneRect();
	const auto spec = ComputeTextLayoutSpec(
		_defaultFontSize,
		sceneRect.size().toSize(),
		_editStyle,
		_editTypeface);

	_edit.proxy.reset(new TextEditProxy());
	const auto proxy = _edit.proxy.get();
	setupProxy(
		proxy,
		EffectiveTextColor(_defaultColor, _editStyle),
		spec,
		_editAlignment);
	static_cast<TextEditProxy*>(proxy)->setStyleInfo(
		_editStyle,
		_defaultColor,
		_defaultFontSize);

	const auto emojiDoc = proxy->document();
	const auto shortSide = std::min(
		sceneRect.width(),
		sceneRect.height());
	const auto maxTextWidth = spec.maxTextWidth;
	const auto minTextWidth = std::clamp(
		int(shortSide * kMinWidthFactor) - 2 * spec.padding,
		1,
		maxTextWidth);
	const auto sceneCenter = sceneRect.center();
	const auto adjustWidth = [=] {
		if (int(emojiDoc->textWidth()) != maxTextWidth) {
			emojiDoc->setTextWidth(maxTextWidth);
		}
		const auto ideal = int(std::ceil(emojiDoc->idealWidth()));
		const auto width = std::clamp(
			ideal + kIdealWidthExtra,
			minTextWidth,
			maxTextWidth);
		if (int(proxy->textWidth()) != width) {
			proxy->setTextWidth(width);
		}
		if (flipped) {
			proxy->setTransform(
				QTransform().translate(width, 0).scale(-1, 1));
		}
		const auto anchor = QPointF(width / 2., 0.);
		proxy->setTransformOriginPoint(anchor);
		proxy->setPos(sceneCenter - anchor);
	};
	adjustWidth();
	proxy->setRotation(flipped ? -rotation : rotation);

	QObject::connect(
		emojiDoc,
		&QTextDocument::contentsChange,
		[=](int position, int removed, int added) {
			if (!emojiDoc->availableRedoSteps()) {
				SanitizeRange(emojiDoc, position, position + added);
				ReplaceEmojiInRange(emojiDoc, position, position + added);
			}
			adjustWidth();
		});

	graphicsScene()->addItem(proxy);
	proxy->setZValue((*_scene->lastZ())++);
	proxy->setFocus();
	if (!_scene->views().isEmpty()) {
		_scene->views().first()->setFocus();
	}

	const auto raw = static_cast<TextEditProxy*>(proxy);
	raw->onFinish = crl::guard(_scene, [=] {
		if (generation == _generation) {
			finishEditing(true);
		}
	});
	raw->onCancel = crl::guard(_scene, [=] {
		if (generation == _generation) {
			finishEditing(false);
		}
	});

	_edit.item.reset();
	_colorRequests.fire_copy(_defaultColor);
}

void TextEditController::startEditing(ItemText *item) {
	if (_edit.proxy) {
		finishEditing(true);
	}
	if (!item) {
		return;
	}

	const auto generation = ++_generation;

	_scene->cancelDrawing();
	setEditingState(true);
	_editStyle = item->textStyle();
	_editTypeface = item->typeface();
	_editAlignment = item->alignment();
	_edit.flipped = item->flipped();

	const auto sceneRect = _scene->sceneRect();
	const auto spec = ComputeTextLayoutSpec(
		item->fontSize(),
		sceneRect.size().toSize(),
		item->textStyle(),
		item->typeface());

	_edit.proxy.reset(new TextEditProxy());
	const auto proxy = _edit.proxy.get();
	setupProxy(
		proxy,
		EffectiveTextColor(item->color(), item->textStyle()),
		spec,
		_editAlignment);
	static_cast<TextEditProxy*>(proxy)->setStyleInfo(
		item->textStyle(),
		item->color(),
		item->fontSize());

	proxy->setPlainText(item->text());
	ReplaceEmoji(proxy->document());

	const auto emojiDoc = proxy->document();
	const auto shortSide = std::min(
		sceneRect.width(),
		sceneRect.height());
	const auto maxTextWidth = spec.maxTextWidth;
	const auto minTextWidth = std::clamp(
		int(shortSide * kMinWidthFactor) - 2 * spec.padding,
		1,
		maxTextWidth);
	const auto anchor = item->scenePos();
	const auto flipped = item->flipped();
	const auto adjustWidth = [=] {
		if (int(emojiDoc->textWidth()) != maxTextWidth) {
			emojiDoc->setTextWidth(maxTextWidth);
		}
		const auto ideal = int(std::ceil(emojiDoc->idealWidth()));
		const auto width = std::clamp(
			ideal + kIdealWidthExtra,
			minTextWidth,
			maxTextWidth);
		if (int(proxy->textWidth()) != width) {
			proxy->setTextWidth(width);
		}
		if (flipped) {
			proxy->setTransform(
				QTransform().translate(width, 0).scale(-1, 1));
		}
		const auto center = proxy->boundingRect().center();
		proxy->setTransformOriginPoint(center);
		proxy->setPos(anchor - center);
	};
	adjustWidth();

	QObject::connect(
		emojiDoc,
		&QTextDocument::contentsChange,
		[=](int position, int removed, int added) {
			if (!emojiDoc->availableRedoSteps()) {
				SanitizeRange(emojiDoc, position, position + added);
				ReplaceEmojiInRange(emojiDoc, position, position + added);
			}
			adjustWidth();
		});

	const auto scale = item->scale() * item->editScale();
	proxy->setRotation(flipped ? -item->rotation() : item->rotation());
	if (std::abs(scale - 1.) > kScaleThreshold) {
		proxy->setScale(scale);
	}

	graphicsScene()->addItem(proxy);
	proxy->setZValue((*_scene->lastZ())++);
	proxy->setFocus();

	auto cursor = proxy->textCursor();
	cursor.select(QTextCursor::Document);
	proxy->setTextCursor(cursor);

	item->setVisible(false);

	const auto raw = static_cast<TextEditProxy*>(proxy);
	raw->onFinish = crl::guard(_scene, [=] {
		if ((generation != _generation) || !_edit.proxy) {
			return;
		}
		// Focus loss with cleared text cancels instead of removing.
		const auto empty = RecoverTextFromDocument(
			_edit.proxy->document()).trimmed().isEmpty();
		finishEditing(!empty);
	});
	raw->onCancel = crl::guard(_scene, [=] {
		if (generation == _generation) {
			finishEditing(false);
		}
	});

	_edit.item = _scene->itemShared(item);
	_colorRequests.fire_copy(item->color());
}

void TextEditController::finishEditing(bool save, bool notify) {
	if (!_edit.proxy) {
		return;
	}

	const auto text = save
		? RecoverTextFromDocument(_edit.proxy->document()).trimmed()
		: QString();
	const auto proxyRect = _edit.proxy->boundingRect();
	const auto proxyCenter = _edit.proxy->mapToScene(proxyRect.center());
	const auto flipped = std::exchange(_edit.flipped, false);
	const auto proxyRotation = int(flipped
		? -_edit.proxy->rotation()
		: _edit.proxy->rotation());
	const auto lockedItem = _edit.item.lock();
	auto *existingItem = lockedItem
		? static_cast<ItemText*>(lockedItem.get())
		: (ItemText*)(nullptr);

	const auto raw = static_cast<TextEditProxy*>(_edit.proxy.get());
	raw->onFinish = nullptr;
	raw->onCancel = nullptr;
	graphicsScene()->removeItem(_edit.proxy.get());
	_edit.proxy = nullptr;
	_edit.item.reset();
	const auto stagedColor = base::take(_edit.color);
	setEditingState(false, notify);

	const auto defaultStyle = _defaultStyle;

	if (!text.isEmpty()) {
		if (existingItem) {
			if (stagedColor) {
				existingItem->setColor(*stagedColor);
			}
			existingItem->setText(text);
			existingItem->setVisible(true);
		} else {
			const auto imageSize = _scene->sceneRect().size().toSize();
			const auto contentSize = ItemText::computeContentSize(
				text,
				_defaultFontSize,
				imageSize,
				defaultStyle,
				_defaultTypeface);
			const auto currentZoom = _scene->currentZoom();
			const auto zoom = (currentZoom > 0.) ? currentZoom : 1.;
			const auto handleInflate = int(
				std::ceil(st::photoEditorItemHandleSize / zoom));
			const auto size = std::max(
				contentSize.width() + handleInflate,
				1);
			auto data = ItemBase::Data{
				.initialZoom = zoom,
				.zPtr = _scene->lastZ(),
				.size = size,
				.x = int(proxyCenter.x()),
				.y = int(proxyCenter.y()),
				.flipped = flipped,
				.rotation = proxyRotation,
				.imageSize = imageSize,
			};
			auto item = std::make_shared<ItemText>(
				text,
				stagedColor.value_or(_defaultColor),
				_defaultFontSize,
				defaultStyle,
				_defaultTypeface,
				_defaultAlignment,
				imageSize,
				std::move(data));
			_scene->addItem(item);
			firePrefs();
		}
	} else if (existingItem) {
		if (save) {
			_scene->removeItem(existingItem);
		} else {
			existingItem->setVisible(true);
		}
	}
}

} // namespace Editor
