/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/choose_theme_controller.h"

#include "ui/rp_widget.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/chat/chat_theme.h"
#include "ui/chat/message_bubble.h"
#include "ui/wrap/vertical_layout.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"
#include "window/themes/window_theme.h"
#include "data/data_session.h"
#include "data/data_peer.h"
#include "data/data_cloud_themes.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "lang/lang_keys.h"
#include "apiwrap.h"
#include "styles/style_widgets.h"
#include "styles/style_layers.h" // boxTitle.
#include "styles/style_settings.h"
#include "styles/style_window.h"

#include <QtWidgets/QApplication>

namespace Ui {
namespace {

constexpr auto kDisableElement = "disable"_cs;

[[nodiscard]] QImage GeneratePreview(not_null<Ui::ChatTheme*> theme) {
	const auto &background = theme->background();
	const auto &colors = background.colors;
	const auto size = st::settingsThemePreviewSize;
	auto prepared = background.prepared;
	const auto paintPattern = [&](QPainter &p, bool inverted) {
		if (prepared.isNull()) {
			return;
		}
		const auto w = prepared.width();
		const auto h = prepared.height();
		const auto scaled = size.scaled(
			st::windowMinWidth / 2,
			st::windowMinHeight / 2,
			Qt::KeepAspectRatio);
		const auto use = (scaled.width() > w || scaled.height() > h)
			? scaled.scaled({ w, h }, Qt::KeepAspectRatio)
			: scaled;
		const auto good = QSize(
			std::max(use.width(), 1),
			std::max(use.height(), 1));
		auto small = prepared.copy(QRect(
			QPoint(
				(w - good.width()) / 2,
				(h - good.height()) / 2),
			good));
		if (inverted) {
			small = Ui::InvertPatternImage(std::move(small));
		}
		p.drawImage(
			QRect(QPoint(), size * style::DevicePixelRatio()),
			small);
	};
	const auto fullsize = size * style::DevicePixelRatio();
	auto result = background.waitingForNegativePattern()
		? QImage(
			fullsize,
			QImage::Format_ARGB32_Premultiplied)
		: Ui::GenerateBackgroundImage(
			fullsize,
			colors.empty() ? std::vector{ 1, QColor(0, 0, 0) } : colors,
			background.gradientRotation,
			background.patternOpacity,
			paintPattern);
	if (background.waitingForNegativePattern()) {
		result.fill(Qt::black);
	}
	result.setDevicePixelRatio(style::DevicePixelRatio());
	{
		auto p = QPainter(&result);
		const auto sent = QRect(
			QPoint(
				(size.width()
					- st::settingsThemeBubbleSize.width()
					- st::settingsThemeBubblePosition.x()),
				st::settingsThemeBubblePosition.y()),
			st::settingsThemeBubbleSize);
		const auto received = QRect(
			st::settingsThemeBubblePosition.x(),
			sent.y() + sent.height() + st::settingsThemeBubbleSkip,
			sent.width(),
			sent.height());
		const auto radius = st::settingsThemeBubbleRadius;

		PainterHighQualityEnabler hq(p);
		p.setPen(Qt::NoPen);
		if (const auto pattern = theme->bubblesBackgroundPattern()) {
			auto bubble = pattern->pixmap.toImage().scaled(
				sent.size() * style::DevicePixelRatio(),
				Qt::IgnoreAspectRatio,
				Qt::SmoothTransformation
			).convertToFormat(QImage::Format_ARGB32_Premultiplied);
			const auto corners = Images::CornersMask(radius);
			p.drawImage(sent, Images::Round(std::move(bubble), corners));
		} else {
			p.setBrush(theme->palette()->msgOutBg()->c);
			p.drawRoundedRect(sent, radius, radius);
		}
		p.setBrush(theme->palette()->msgInBg()->c);
		p.drawRoundedRect(received, radius, radius);
	}
	return Images::Round(std::move(result), ImageRoundRadius::Large);
}

[[nodiscard]] QImage GenerateEmptyPreview() {
	auto result = QImage(
		st::settingsThemePreviewSize * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	result.fill(st::settingsThemeNotSupportedBg->c);
	result.setDevicePixelRatio(style::DevicePixelRatio());
	{
		auto p = QPainter(&result);
		p.setPen(st::menuIconFg);
		p.setFont(st::semiboldFont);
		const auto top = st::normalFont->height / 2;
		const auto width = st::settingsThemePreviewSize.width();
		const auto height = st::settingsThemePreviewSize.height() - top;
		p.drawText(
			QRect(0, top, width, height),
			tr::lng_chat_theme_none(tr::now),
			style::al_top);
	}
	return Images::Round(std::move(result), ImageRoundRadius::Large);
}

} // namespace

struct ChooseThemeController::Entry {
	Ui::ChatThemeKey key;
	std::shared_ptr<Ui::ChatTheme> theme;
	std::shared_ptr<Data::DocumentMedia> media;
	QImage preview;
	EmojiPtr emoji = nullptr;
	QRect geometry;
	bool chosen = false;
};

ChooseThemeController::ChooseThemeController(
	not_null<RpWidget*> parent,
	not_null<Window::SessionController*> window,
	not_null<PeerData*> peer)
: _controller(window)
, _peer(peer)
, _wrap(std::make_unique<VerticalLayout>(parent))
, _topShadow(std::make_unique<PlainShadow>(parent))
, _content(_wrap->add(object_ptr<RpWidget>(_wrap.get())))
, _inner(CreateChild<RpWidget>(_content.get()))
, _dark(Window::Theme::IsThemeDarkValue()) {
	init(parent->sizeValue());
}

ChooseThemeController::~ChooseThemeController() {
	_controller->clearPeerThemeOverride(_peer);
}

void ChooseThemeController::init(rpl::producer<QSize> outer) {
	using namespace rpl::mappers;

	const auto themes = &_controller->session().data().cloudThemes();
	const auto &list = themes->chatThemes();
	if (!list.empty()) {
		fill(list);
	} else {
		themes->refreshChatThemes();
		themes->chatThemesUpdated(
		) | rpl::take(1) | rpl::start_with_next([=] {
			fill(themes->chatThemes());
		}, lifetime());
	}

	const auto skip = st::normalFont->spacew * 4;
	const auto titleWrap = _wrap->insert(
		0,
		object_ptr<FixedHeightWidget>(
			_wrap.get(),
			skip + st::boxTitle.style.font->height + skip));
	auto title = CreateChild<FlatLabel>(
		titleWrap,
		tr::lng_chat_theme_title(),
		st::boxTitle);
	_wrap->paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		QPainter(_wrap.get()).fillRect(clip, st::windowBg);
	}, lifetime());

	initButtons();
	initList();

	_inner->positionValue(
	) | rpl::start_with_next([=](QPoint position) {
		title->move(std::max(position.x(), skip) + skip, skip);
	}, title->lifetime());

	std::move(
		outer
	) | rpl::start_with_next([=](QSize outer) {
		_wrap->resizeToWidth(outer.width());
		_wrap->move(0, outer.height() - _wrap->height());
		const auto line = st::lineWidth;
		_topShadow->setGeometry(0, _wrap->y() - line, outer.width(), line);
	}, lifetime());

	rpl::combine(
		_shouldBeShown.value(),
		_forceHidden.value(),
		_1 && !_2
	) | rpl::start_with_next([=](bool shown) {
		_wrap->setVisible(shown);
		_topShadow->setVisible(shown);
	}, lifetime());
}

void ChooseThemeController::initButtons() {
	const auto controls = _wrap->add(object_ptr<RpWidget>(_wrap.get()));
	const auto cancel = CreateChild<RoundButton>(
		controls,
		tr::lng_cancel(),
		st::defaultLightButton);
	const auto apply = CreateChild<RoundButton>(
		controls,
		tr::lng_chat_theme_apply(),
		st::defaultActiveButton);
	const auto skip = st::normalFont->spacew * 2;
	controls->resize(
		skip + cancel->width() + skip + apply->width() + skip,
		apply->height() + skip * 2);
	rpl::combine(
		controls->widthValue(),
		cancel->widthValue(),
		apply->widthValue()
	) | rpl::start_with_next([=](
			int outer,
			int cancelWidth,
			int applyWidth) {
		const auto inner = skip + cancelWidth + skip + applyWidth + skip;
		const auto left = (outer - inner) / 2;
		cancel->moveToLeft(left, 0);
		apply->moveToRight(left, 0);
	}, controls->lifetime());

	cancel->setClickedCallback([=] { close(); });
	apply->setClickedCallback([=] {
		if (const auto chosen = findChosen()) {
			if (Ui::Emoji::Find(_peer->themeEmoji()) != chosen->emoji) {
				const auto now = chosen->key ? _chosen : QString();
				_peer->setThemeEmoji(now);
				if (chosen->theme) {
					// Remember while changes propagate through event loop.
					_controller->pushLastUsedChatTheme(chosen->theme);
				}
				const auto api = &_peer->session().api();
				api->request(MTPmessages_SetChatTheme(
					_peer->input,
					MTP_string(now)
				)).done([=](const MTPUpdates &result) {
					api->applyUpdates(result);
				}).send();
			}
		}
		_controller->toggleChooseChatTheme(_peer);
	});
}

void ChooseThemeController::paintEntry(QPainter &p, const Entry &entry) {
	const auto geometry = entry.geometry;
	p.drawImage(geometry, entry.preview);

	const auto size = Ui::Emoji::GetSizeLarge();
	const auto factor = style::DevicePixelRatio();
	const auto emojiLeft = geometry.x()
		+ (geometry.width() - (size / factor)) / 2;
	const auto emojiTop = geometry.y()
		+ geometry.height()
		- (size / factor)
		- (st::normalFont->spacew * 2);
	Ui::Emoji::Draw(p, entry.emoji, size, emojiLeft, emojiTop);

	if (entry.chosen) {
		auto hq = PainterHighQualityEnabler(p);
		auto pen = st::activeLineFg->p;
		const auto width = st::defaultFlatInput.borderWidth;
		pen.setWidth(width);
		p.setPen(pen);
		const auto add = st::lineWidth + width;
		p.drawRoundedRect(
			entry.geometry.marginsAdded({ add, add, add, add }),
			st::roundRadiusLarge + add,
			st::roundRadiusLarge + add);
	}
}

void ChooseThemeController::initList() {
	_content->resize(
		_content->width(),
		8 * st::normalFont->spacew + st::settingsThemePreviewSize.height());
	_inner->setMouseTracking(true);

	_inner->paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		auto p = QPainter(_inner.get());
		for (const auto &entry : _entries) {
			if (entry.preview.isNull() || !clip.intersects(entry.geometry)) {
				continue;
			}
			paintEntry(p, entry);
		}
	}, lifetime());
	const auto byPoint = [=](QPoint position) -> Entry* {
		for (auto &entry : _entries) {
			if (entry.geometry.contains(position)) {
				return &entry;
			}
		}
		return nullptr;
	};
	const auto chosenText = [=](const Entry *entry) {
		if (!entry) {
			return QString();
		} else if (entry->key) {
			return entry->emoji->text();
		} else {
			return kDisableElement.utf16();
		}
	};
	_inner->events(
	) | rpl::start_with_next([=](not_null<QEvent*> event) {
		const auto type = event->type();
		if (type == QEvent::MouseMove) {
			const auto mouse = static_cast<QMouseEvent*>(event.get());
			const auto skip = _inner->width() - _content->width();
			if (skip <= 0) {
				_dragStartPosition = _pressPosition = std::nullopt;
			} else if (_pressPosition.has_value()
				&& ((mouse->globalPos() - *_pressPosition).manhattanLength()
					>= QApplication::startDragDistance())) {
				_dragStartPosition = base::take(_pressPosition);
				_dragStartInnerLeft = _inner->x();
			}
			if (_dragStartPosition.has_value()) {
				const auto shift = mouse->globalPos().x()
					- _dragStartPosition->x();
				updateInnerLeft(_dragStartInnerLeft + shift);
			} else {
				_inner->setCursor(byPoint(mouse->pos())
					? style::cur_pointer
					: style::cur_default);
			}
		} else if (type == QEvent::MouseButtonPress) {
			const auto mouse = static_cast<QMouseEvent*>(event.get());
			if (mouse->button() == Qt::LeftButton) {
				_pressPosition = mouse->globalPos();
			}
			_pressed = chosenText(byPoint(mouse->pos()));
		} else if (type == QEvent::MouseButtonRelease) {
			_pressPosition = _dragStartPosition = std::nullopt;
			const auto mouse = static_cast<QMouseEvent*>(event.get());
			const auto entry = byPoint(mouse->pos());
			const auto chosen = chosenText(entry);
			if (entry && chosen == _pressed && chosen != _chosen) {
				clearCurrentBackgroundState();
				if (const auto was = findChosen()) {
					was->chosen = false;
				}
				_chosen = chosen;
				entry->chosen = true;
				if (entry->theme || !entry->key) {
					_controller->overridePeerTheme(_peer, entry->theme);
				}
				_inner->update();
			}
			_pressed = QString();
		} else if (type == QEvent::Wheel) {
			const auto wheel = static_cast<QWheelEvent*>(event.get());
			const auto was = _inner->x();
			updateInnerLeft((wheel->angleDelta().x() != 0)
				? (was + (wheel->pixelDelta().x()
					? wheel->pixelDelta().x()
					: wheel->angleDelta().x()))
				: (wheel->angleDelta().y() != 0)
				? (was + (wheel->pixelDelta().y()
					? wheel->pixelDelta().y()
					: wheel->angleDelta().y()))
				: was);
		}
	}, lifetime());

	_content->events(
	) | rpl::start_with_next([=](not_null<QEvent*> event) {
		const auto type = event->type();
		if (type == QEvent::KeyPress) {
			const auto key = static_cast<QKeyEvent*>(event.get());
			if (key->key() == Qt::Key_Escape) {
				close();
			}
		}
	}, lifetime());

	rpl::combine(
		_content->widthValue(),
		_inner->widthValue()
	) | rpl::start_with_next([=](int content, int inner) {
		if (!content || !inner) {
			return;
		} else if (!_entries.empty() && !_initialInnerLeftApplied) {
			applyInitialInnerLeft();
		} else {
			updateInnerLeft(_inner->x());
		}
	}, lifetime());
}

void ChooseThemeController::applyInitialInnerLeft() {
	if (const auto chosen = findChosen()) {
		updateInnerLeft(
			_content->width() / 2 - chosen->geometry.center().x());
	}
	_initialInnerLeftApplied = true;
}

void ChooseThemeController::updateInnerLeft(int now) {
	const auto skip = _content->width() - _inner->width();
	const auto clamped = (skip >= 0)
		? (skip / 2)
		: std::clamp(now, skip, 0);
	_inner->move(clamped, 0);
}

void ChooseThemeController::close() {
	if (const auto chosen = findChosen()) {
		if (Ui::Emoji::Find(_peer->themeEmoji()) != chosen->emoji) {
			clearCurrentBackgroundState();
		}
	}
	_controller->toggleChooseChatTheme(_peer);
}

void ChooseThemeController::clearCurrentBackgroundState() {
	if (const auto entry = findChosen()) {
		if (entry->theme) {
			entry->theme->clearBackgroundState();
		}
	}
}

auto ChooseThemeController::findChosen() -> Entry* {
	if (_chosen.isEmpty()) {
		return nullptr;
	}
	for (auto &entry : _entries) {
		if (!entry.key && _chosen == kDisableElement.utf16()) {
			return &entry;
		} else if (_chosen == entry.emoji->text()) {
			return &entry;
		}
	}
	return nullptr;
}

auto ChooseThemeController::findChosen() const -> const Entry* {
	return const_cast<ChooseThemeController*>(this)->findChosen();
}

void ChooseThemeController::fill(
		const std::vector<Data::CloudTheme> &themes) {
	if (themes.empty()) {
		return;
	}
	const auto count = int(themes.size()) + 1;
	const auto single = st::settingsThemePreviewSize;
	const auto skip = st::normalFont->spacew * 2;
	const auto full = single.width() * count + skip * (count + 3);
	_inner->resize(full, skip + single.height() + skip);

	const auto initial = Ui::Emoji::Find(_peer->themeEmoji());

	_dark.value(
	) | rpl::start_with_next([=](bool dark) {
		clearCurrentBackgroundState();
		if (_chosen.isEmpty() && initial) {
			_chosen = initial->text();
		}

		_cachingLifetime.destroy();
		const auto old = base::take(_entries);
		auto x = skip * 2;
		_entries.push_back({
			.preview = GenerateEmptyPreview(),
			.emoji = Ui::Emoji::Find(QString::fromUtf8("\xe2\x9d\x8c")),
			.geometry = QRect(QPoint(x, skip), single),
			.chosen = (_chosen == kDisableElement.utf16()),
		});
		Assert(_entries.front().emoji != nullptr);
		style::PaletteChanged(
		) | rpl::start_with_next([=] {
			_entries.front().preview = GenerateEmptyPreview();
		}, _cachingLifetime);

		const auto type = dark
			? Data::CloudThemeType::Dark
			: Data::CloudThemeType::Light;

		x += single.width() + skip;
		for (const auto &theme : themes) {
			const auto emoji = Ui::Emoji::Find(theme.emoticon);
			if (!emoji || !theme.settings.contains(type)) {
				continue;
			}
			const auto key = ChatThemeKey{ theme.id, dark };
			const auto isChosen = (_chosen == emoji->text());
			_entries.push_back({
				.key = key,
				.emoji = emoji,
				.geometry = QRect(QPoint(x, skip), single),
				.chosen = isChosen,
			});
			_controller->cachedChatThemeValue(
				theme,
				type
			) | rpl::filter([=](const std::shared_ptr<ChatTheme> &data) {
				return data && (data->key() == key);
			}) | rpl::take(
				1
			) | rpl::start_with_next([=](std::shared_ptr<ChatTheme> &&data) {
				const auto key = data->key();
				const auto i = ranges::find(_entries, key, &Entry::key);
				if (i == end(_entries)) {
					return;
				}
				const auto theme = data.get();
				i->theme = std::move(data);
				i->preview = GeneratePreview(theme);
				if (_chosen == i->emoji->text()) {
					_controller->overridePeerTheme(_peer, i->theme);
				}
				_inner->update();

				if (!theme->background().isPattern
					|| !theme->background().prepared.isNull()) {
					return;
				}
				// Subscribe to pattern loading if needed.
				theme->repaintBackgroundRequests(
				) | rpl::filter([=] {
					const auto i = ranges::find(
						_entries,
						key,
						&Entry::key);
					return (i == end(_entries))
						|| !i->theme->background().prepared.isNull();
				}) | rpl::take(1) | rpl::start_with_next([=] {
					const auto i = ranges::find(
						_entries,
						key,
						&Entry::key);
					if (i == end(_entries)) {
						return;
					}
					i->preview = GeneratePreview(theme);
					_inner->update();
				}, _cachingLifetime);
			}, _cachingLifetime);
			x += single.width() + skip;
		}

		if (!_initialInnerLeftApplied && _content->width() > 0) {
			applyInitialInnerLeft();
		}
	}, lifetime());
	_shouldBeShown = true;
}

bool ChooseThemeController::shouldBeShown() const {
	return _shouldBeShown.current();
}

rpl::producer<bool> ChooseThemeController::shouldBeShownValue() const {
	return _shouldBeShown.value();
}

int ChooseThemeController::height() const {
	return shouldBeShown() ? _wrap->height() : 0;
}

void ChooseThemeController::hide() {
	_forceHidden = true;
}

void ChooseThemeController::show() {
	_forceHidden = false;
}

void ChooseThemeController::raise() {
	_wrap->raise();
	_topShadow->raise();
}

void ChooseThemeController::setFocus() {
	_content->setFocus();
}

rpl::lifetime &ChooseThemeController::lifetime() {
	return _wrap->lifetime();
}

} // namespace Ui
