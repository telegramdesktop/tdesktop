/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/themes/window_themes_cloud_list.h"

#include "window/themes/window_themes_embedded.h"
#include "window/themes/window_theme_editor_box.h"
#include "window/themes/window_theme.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "data/data_cloud_themes.h"
#include "data/data_file_origin.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_session.h"
#include "ui/image/image_prepare.h"
#include "ui/widgets/popup_menu.h"
#include "ui/toast/toast.h"
#include "boxes/confirm_box.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "core/application.h"
#include "styles/style_settings.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>

namespace Window {
namespace Theme {
namespace {

constexpr auto kFakeCloudThemeId = 0xFFFFFFFFFFFFFFFAULL;
constexpr auto kShowPerRow = 4;

[[nodiscard]] Data::CloudTheme FakeCloudTheme(const Object &object) {
	auto result = Data::CloudTheme();
	result.id = result.documentId = kFakeCloudThemeId;
	result.slug = object.pathAbsolute;
	return result;
}

[[nodiscard]] QImage ColorsBackgroundFromImage(const QImage &source) {
	if (source.isNull()) {
		return source;
	}
	const auto from = source.size();
	const auto to = st::settingsThemePreviewSize * cIntRetinaFactor();
	if (to.width() * from.height() > to.height() * from.width()) {
		const auto small = (from.width() > to.width())
			? source.scaledToWidth(to.width(), Qt::SmoothTransformation)
			: source;
		const auto takew = small.width();
		const auto takeh = std::max(
			takew * to.height() / to.width(),
			1);
		return (small.height() != takeh)
			? small.copy(0, (small.height() - takeh) / 2, takew, takeh)
			: small;
	} else {
		const auto small = (from.height() > to.height())
			? source.scaledToHeight(to.height(), Qt::SmoothTransformation)
			: source;
		const auto takeh = small.height();
		const auto takew = std::max(
			takeh * to.width() / to.height(),
			1);
		return (small.width() != takew)
			? small.copy((small.width() - takew) / 2, 0, takew, takeh)
			: small;
	}
}

[[nodiscard]] std::optional<CloudListColors> ColorsFromTheme(
		const QString &path,
		const QByteArray &theme) {
	const auto content = [&] {
		if (!theme.isEmpty()) {
			return theme;
		}
		auto file = QFile(path);
		return file.open(QIODevice::ReadOnly)
			? file.readAll()
			: QByteArray();
	}();
	if (content.isEmpty()) {
		return std::nullopt;
	}
	auto instance = Instance();
	if (!LoadFromContent(content, &instance, nullptr)) {
		return std::nullopt;
	}
	auto result = CloudListColors();
	result.background = ColorsBackgroundFromImage(instance.background);
	result.sent = st::msgOutBg[instance.palette]->c;
	result.received = st::msgInBg[instance.palette]->c;
	result.radiobuttonActive
		= result.radiobuttonInactive
		= st::msgServiceFg[instance.palette]->c;
	return result;
}

[[nodiscard]] CloudListColors ColorsFromCurrentTheme() {
	auto result = CloudListColors();
	auto background = Background()->createCurrentImage();
	result.background = ColorsBackgroundFromImage(background);
	result.sent = st::msgOutBg->c;
	result.received = st::msgInBg->c;
	result.radiobuttonActive
		= result.radiobuttonInactive
		= st::msgServiceFg->c;
	return result;
}

} // namespace

CloudListColors ColorsFromScheme(const EmbeddedScheme &scheme) {
	auto result = CloudListColors();
	result.sent = scheme.sent;
	result.received = scheme.received;
	result.radiobuttonActive = scheme.radiobuttonActive;
	result.radiobuttonInactive = scheme.radiobuttonInactive;
	result.background = QImage(
		QSize(1, 1) * cIntRetinaFactor(),
		QImage::Format_ARGB32_Premultiplied);
	result.background.fill(scheme.background);
	return result;
}

CloudListColors ColorsFromScheme(
		const EmbeddedScheme &scheme,
		const Colorizer &colorizer) {
	if (!colorizer) {
		return ColorsFromScheme(scheme);
	}
	auto copy = scheme;
	Colorize(copy, colorizer);
	return ColorsFromScheme(copy);
}

CloudListCheck::CloudListCheck(const Colors &colors, bool checked)
: CloudListCheck(checked) {
	setColors(colors);
}

CloudListCheck::CloudListCheck(bool checked)
: AbstractCheckView(st::defaultRadio.duration, checked, nullptr)
, _radio(st::defaultRadio, checked, [=] { update(); }) {
}

void CloudListCheck::setColors(const Colors &colors) {
	_colors = colors;
	if (!_colors->background.isNull()) {
		const auto size = st::settingsThemePreviewSize * cIntRetinaFactor();
		_backgroundFull = (_colors->background.size() == size)
			? _colors->background
			: _colors->background.scaled(
				size,
				Qt::IgnoreAspectRatio,
				Qt::SmoothTransformation);
		_backgroundCacheWidth = -1;

		ensureContrast();
		_radio.setToggledOverride(_colors->radiobuttonActive);
		_radio.setUntoggledOverride(_colors->radiobuttonInactive);
	}
	update();
}

void CloudListCheck::ensureContrast() {
	const auto radio = _radio.getSize();
	const auto x = (getSize().width() - radio.width()) / 2;
	const auto y = getSize().height()
		- radio.height()
		- st::settingsThemeRadioBottom;
	const auto under = QRect(
		QPoint(x, y) * cIntRetinaFactor(),
		radio * cIntRetinaFactor());
	const auto image = _backgroundFull.copy(under).convertToFormat(
		QImage::Format_ARGB32_Premultiplied);
	const auto active = style::internal::EnsureContrast(
		_colors->radiobuttonActive,
		CountAverageColor(image));
	_colors->radiobuttonInactive = _colors->radiobuttonActive = QColor(
		active.red(),
		active.green(),
		active.blue(),
		255);
	_colors->radiobuttonInactive.setAlpha(192);
}

QSize CloudListCheck::getSize() const {
	return st::settingsThemePreviewSize;
}

void CloudListCheck::validateBackgroundCache(int width) {
	if (_backgroundCacheWidth == width || width <= 0) {
		return;
	}
	_backgroundCacheWidth = width;
	const auto imageWidth = width * cIntRetinaFactor();
	_backgroundCache = (width == st::settingsThemePreviewSize.width())
		? _backgroundFull
		: _backgroundFull.copy(
			(_backgroundFull.width() - imageWidth) / 2,
			0,
			imageWidth,
			_backgroundFull.height());
	Images::prepareRound(_backgroundCache, ImageRoundRadius::Large);
	_backgroundCache.setDevicePixelRatio(cRetinaFactor());
}

void CloudListCheck::paint(Painter &p, int left, int top, int outerWidth) {
	if (!_colors) {
		return;
	} else if (_colors->background.isNull()) {
		paintNotSupported(p, left, top, outerWidth);
	} else {
		paintWithColors(p, left, top, outerWidth);
	}
}

void CloudListCheck::paintNotSupported(
		Painter &p,
		int left,
		int top,
		int outerWidth) {
	PainterHighQualityEnabler hq(p);
	p.setPen(Qt::NoPen);
	p.setBrush(st::settingsThemeNotSupportedBg);

	const auto height = st::settingsThemePreviewSize.height();
	const auto rect = QRect(0, 0, outerWidth, height);
	const auto radius = st::historyMessageRadius;
	p.drawRoundedRect(rect, radius, radius);
	st::settingsThemeNotSupportedIcon.paintInCenter(p, rect);
}

void CloudListCheck::paintWithColors(
		Painter &p,
		int left,
		int top,
		int outerWidth) {
	Expects(_colors.has_value());

	validateBackgroundCache(outerWidth);
	p.drawImage(
		QRect(0, 0, outerWidth, st::settingsThemePreviewSize.height()),
		_backgroundCache);

	const auto received = QRect(
		st::settingsThemeBubblePosition,
		st::settingsThemeBubbleSize);
	const auto sent = QRect(
		outerWidth - received.width() - st::settingsThemeBubblePosition.x(),
		received.y() + received.height() + st::settingsThemeBubbleSkip,
		received.width(),
		received.height());
	const auto radius = st::settingsThemeBubbleRadius;

	PainterHighQualityEnabler hq(p);
	p.setPen(Qt::NoPen);

	p.setBrush(_colors->received);
	p.drawRoundedRect(style::rtlrect(received, outerWidth), radius, radius);
	p.setBrush(_colors->sent);
	p.drawRoundedRect(style::rtlrect(sent, outerWidth), radius, radius);

	const auto skip = st::settingsThemeRadioBottom / 2;

	const auto radio = _radio.getSize();
	_radio.paint(
		p,
		(outerWidth - radio.width()) / 2,
		getSize().height() - radio.height() - st::settingsThemeRadioBottom,
		outerWidth);
}

QImage CloudListCheck::prepareRippleMask() const {
	return QImage();
}

bool CloudListCheck::checkRippleStartPosition(QPoint position) const {
	return false;
}

void CloudListCheck::checkedChangedHook(anim::type animated) {
	_radio.setChecked(checked(), animated);
}

CloudList::CloudList(
	not_null<QWidget*> parent,
	not_null<Window::SessionController*> window)
: _window(window)
, _owned(parent)
, _outer(_owned.data())
, _group(std::make_shared<Ui::RadiobuttonGroup>()) {
	setup();
}

void CloudList::showAll() {
	_showAll = true;
}

object_ptr<Ui::RpWidget> CloudList::takeWidget() {
	return std::move(_owned);
}

rpl::producer<bool> CloudList::empty() const {
	using namespace rpl::mappers;

	return _count.value() | rpl::map(_1 == 0);
}

rpl::producer<bool> CloudList::allShown() const {
	using namespace rpl::mappers;

	return rpl::combine(
		_showAll.value(),
		_count.value(),
		_1 || (_2 <= kShowPerRow));
}

void CloudList::setup() {
	_group->setChangedCallback([=](int selected) {
		const auto &object = Background()->themeObject();
		_group->setValue(groupValueForId(
			object.cloud.id ? object.cloud.id : kFakeCloudThemeId));
	});

	auto cloudListChanges = rpl::single(
		rpl::empty_value()
	) | rpl::then(
		_window->session().data().cloudThemes().updated()
	);

	auto themeChanges = rpl::single(BackgroundUpdate(
		BackgroundUpdate::Type::ApplyingTheme,
		Background()->tile()
	)) | rpl::then(base::ObservableViewer(
		*Background()
	)) | rpl::filter([](const BackgroundUpdate &update) {
		return (update.type == BackgroundUpdate::Type::ApplyingTheme);
	});

	rpl::combine(
		std::move(cloudListChanges),
		std::move(themeChanges),
		allShown()
	) | rpl::map([=] {
		return collectAll();
	}) | rpl::start_with_next([=](std::vector<Data::CloudTheme> &&list) {
		rebuildUsing(std::move(list));
	}, _outer->lifetime());

	_outer->widthValue(
	) | rpl::start_with_next([=](int width) {
		updateGeometry();
	}, _outer->lifetime());
}

std::vector<Data::CloudTheme> CloudList::collectAll() const {
	const auto &object = Background()->themeObject();
	const auto isDefault = IsEmbeddedTheme(object.pathAbsolute);
	auto result = _window->session().data().cloudThemes().list();
	if (!isDefault) {
		const auto i = ranges::find(
			result,
			object.cloud.id,
			&Data::CloudTheme::id);
		if (i == end(result)) {
			if (object.cloud.id) {
				result.push_back(object.cloud);
			} else {
				result.push_back(FakeCloudTheme(object));
			}
		}
	}
	return result;
}

void CloudList::rebuildUsing(std::vector<Data::CloudTheme> &&list) {
	const auto fullCount = int(list.size());
	const auto changed = applyChangesFrom(std::move(list));
	_count = fullCount;
	if (changed) {
		updateGeometry();
	}
}

bool CloudList::applyChangesFrom(std::vector<Data::CloudTheme> &&list) {
	if (list.empty()) {
		if (_elements.empty()) {
			return false;
		}
		_elements.clear();
		return true;
	}
	auto changed = false;
	const auto limit = _showAll.current() ? list.size() : kShowPerRow;
	const auto &object = Background()->themeObject();
	const auto id = object.cloud.id ? object.cloud.id : kFakeCloudThemeId;
	ranges::stable_sort(list, std::less<>(), [&](const Data::CloudTheme &t) {
		if (t.id == id) {
			return 0;
		} else if (t.documentId) {
			return 1;
		} else {
			return 2;
		}
	});
	if (list.front().id == id) {
		const auto j = ranges::find(_elements, id, &Element::id);
		if (j == end(_elements)) {
			insert(0, list.front());
			changed = true;
		} else if (j - begin(_elements) >= limit) {
			std::rotate(
				begin(_elements) + limit - 1,
				j,
				j + 1);
			changed = true;
		}
	}
	if (removeStaleUsing(list)) {
		changed = true;
	}
	if (insertTillLimit(list, limit)) {
		changed = true;
	}
	_group->setValue(groupValueForId(id));
	return changed;
}

bool CloudList::removeStaleUsing(const std::vector<Data::CloudTheme> &list) {
	const auto check = [&](Element &element) {
		const auto j = ranges::find(
			list,
			element.theme.id,
			&Data::CloudTheme::id);
		if (j == end(list)) {
			return true;
		}
		refreshElementUsing(element, *j);
		return false;
	};
	const auto from = ranges::remove_if(_elements, check);
	if (from == end(_elements)) {
		return false;
	}
	_elements.erase(from, end(_elements));
	return true;
}

bool CloudList::insertTillLimit(
		const std::vector<Data::CloudTheme> &list,
		int limit) {
	const auto insertCount = (limit - int(_elements.size()));
	if (insertCount < 0) {
		_elements.erase(end(_elements) + insertCount, end(_elements));
		return true;
	} else if (!insertCount) {
		return false;
	}
	const auto isGood = [](const Data::CloudTheme &theme) {
		return (theme.documentId != 0);
	};
	auto positionForGood = ranges::find_if(_elements, [&](const Element &e) {
		return !isGood(e.theme);
	}) - begin(_elements);
	auto positionForBad = end(_elements) - begin(_elements);

	auto insertElements = ranges::view::all(
		list
	) | ranges::view::filter([&](const Data::CloudTheme &theme) {
		const auto i = ranges::find(_elements, theme.id, &Element::id);
		return (i == end(_elements));
	}) | ranges::view::take(insertCount);

	for (const auto &theme : insertElements) {
		const auto good = isGood(theme);
		insert(good ? positionForGood : positionForBad, theme);
		if (good) {
			++positionForGood;
		}
		++positionForBad;
	}
	return true;
}

void CloudList::insert(int index, const Data::CloudTheme &theme) {
	const auto id = theme.id;
	const auto value = groupValueForId(id);
	const auto checked = _group->hasValue() && (_group->value() == value);
	auto check = std::make_unique<CloudListCheck>(checked);
	const auto raw = check.get();
	auto button = std::make_unique<Ui::Radiobutton>(
		_outer,
		_group,
		value,
		theme.title,
		st::settingsTheme,
		std::move(check));
	button->setCheckAlignment(style::al_top);
	button->setAllowTextLines(2);
	button->setTextBreakEverywhere();
	button->show();
	button->setAcceptBoth(true);
	button->addClickHandler([=](Qt::MouseButton button) {
		const auto i = ranges::find(_elements, id, &Element::id);
		if (i == end(_elements)
			|| id == kFakeCloudThemeId
			|| i->waiting) {
			return;
		}
		const auto &cloud = i->theme;
		if (button == Qt::RightButton) {
			showMenu(*i);
		} else if (cloud.documentId) {
			_window->session().data().cloudThemes().applyFromDocument(cloud);
		} else {
			_window->session().data().cloudThemes().showPreview(cloud);
		}
	});
	auto &element = *_elements.insert(
		begin(_elements) + index,
		Element{ theme, raw, std::move(button) });
	refreshColors(element);
}

void CloudList::refreshElementUsing(
		Element &element,
		const Data::CloudTheme &data) {
	const auto colorsChanged = (element.theme.documentId != data.documentId)
		|| ((element.id() == kFakeCloudThemeId)
			&& (element.theme.slug != data.slug));
	const auto titleChanged = (element.theme.title != data.title);
	element.theme = data;
	if (colorsChanged) {
		setWaiting(element, false);
		refreshColors(element);
	}
	if (titleChanged) {
		element.button->setText(data.title);
	}
}

void CloudList::refreshColors(Element &element) {
	const auto currentId = Background()->themeObject().cloud.id;
	const auto &theme = element.theme;
	const auto document = theme.documentId
		? _window->session().data().document(theme.documentId).get()
		: nullptr;
	if (element.id() == kFakeCloudThemeId
		|| ((element.id() == currentId)
			&& (!document || !document->isTheme()))) {
		element.check->setColors(ColorsFromCurrentTheme());
	} else if (document) {
		element.media = document ? document->createMediaView() : nullptr;
		document->save(
			Data::FileOriginTheme(theme.id, theme.accessHash),
			QString());
		if (element.media->loaded()) {
			refreshColorsFromDocument(element);
		} else {
			setWaiting(element, true);
			subscribeToDownloadFinished();
		}
	} else {
		element.check->setColors(CloudListColors());
	}
}

void CloudList::showMenu(Element &element) {
	if (_contextMenu) {
		_contextMenu = nullptr;
		return;
	}
	_contextMenu = base::make_unique_q<Ui::PopupMenu>(element.button.get());
	const auto cloud = element.theme;
	if (const auto slug = element.theme.slug; !slug.isEmpty()) {
		_contextMenu->addAction(tr::lng_theme_share(tr::now), [=] {
			QGuiApplication::clipboard()->setText(
				_window->session().createInternalLinkFull("addtheme/" + slug));
			Ui::Toast::Show(tr::lng_background_link_copied(tr::now));
		});
	}
	if (cloud.documentId
		&& cloud.createdBy == _window->session().userId()
		&& Background()->themeObject().cloud.id == cloud.id) {
		_contextMenu->addAction(tr::lng_theme_edit(tr::now), [=] {
			StartEditor(&_window->window(), cloud);
		});
	}
	const auto id = cloud.id;
	_contextMenu->addAction(tr::lng_theme_delete(tr::now), [=] {
		const auto remove = [=](Fn<void()> &&close) {
			close();
			if (Background()->themeObject().cloud.id == id
				|| id == kFakeCloudThemeId) {
				if (Background()->editingTheme().has_value()) {
					Background()->clearEditingTheme(
						ClearEditing::KeepChanges);
					_window->window().showRightColumn(nullptr);
				}
				ResetToSomeDefault();
				KeepApplied();
			}
			if (id != kFakeCloudThemeId) {
				_window->session().data().cloudThemes().remove(id);
			}
		};
		_window->window().show(Box<ConfirmBox>(
			tr::lng_theme_delete_sure(tr::now),
			tr::lng_theme_delete(tr::now),
			remove));
	});
	_contextMenu->popup(QCursor::pos());
}

void CloudList::setWaiting(Element &element, bool waiting) {
	element.waiting = waiting;
	element.button->setPointerCursor(
		!waiting && (element.theme.documentId || amCreator(element.theme)));
}

bool CloudList::amCreator(const Data::CloudTheme &theme) const {
	return (_window->session().userId() == theme.createdBy);
}

void CloudList::refreshColorsFromDocument(Element &element) {
	Expects(element.media != nullptr);
	Expects(element.media->loaded());

	const auto id = element.id();
	const auto path = element.media->owner()->filepath();
	const auto data = base::take(element.media)->bytes();
	crl::async([=, guard = element.generating.make_guard()]() mutable {
		crl::on_main(std::move(guard), [
			=,
			result = ColorsFromTheme(path, data)
		]() mutable {
			const auto i = ranges::find(_elements, id, &Element::id);
			if (i == end(_elements) || !result) {
				return;
			}
			auto &element = *i;
			if (result->background.isNull()) {
				result->background = ColorsFromCurrentTheme().background;
			}
			element.check->setColors(*result);
			setWaiting(element, false);
		});
	});
}

void CloudList::subscribeToDownloadFinished() {
	if (_downloadFinishedLifetime) {
		return;
	}
	_window->session().downloaderTaskFinished(
	) | rpl::start_with_next([=] {
		auto &&waiting = _elements | ranges::view::filter(&Element::waiting);
		const auto still = ranges::count_if(waiting, [&](Element &element) {
			if (!element.media) {
				element.waiting = false;
				return false;
			} else if (!element.media->loaded()) {
				return true;
			}
			refreshColorsFromDocument(element);
			element.waiting = false;
			return false;
		});
		if (!still) {
			_downloadFinishedLifetime.destroy();
		}
	}, _downloadFinishedLifetime);
}

int CloudList::groupValueForId(uint64 id) {
	const auto i = _groupValueById.find(id);
	if (i != end(_groupValueById)) {
		return i->second;
	}
	const auto result = int(_idByGroupValue.size());
	_groupValueById.emplace(id, result);
	_idByGroupValue.push_back(id);
	return result;
}

void CloudList::updateGeometry() {
	const auto width = _outer->width();
	if (!width) {
		return;
	}
	const auto height = resizeGetHeight(width);
	if (height != _outer->height()) {
		_outer->resize(width, height);
	}
}

int CloudList::resizeGetHeight(int newWidth) {
	const auto desired = st::settingsThemePreviewSize.width();
	const auto minSkip = st::settingsThemeMinSkip;
	const auto single = std::min(
		st::settingsThemePreviewSize.width(),
		(newWidth - minSkip * (kShowPerRow - 1)) / kShowPerRow);
	const auto skip = (newWidth - kShowPerRow * single)
		/ float64(kShowPerRow - 1);

	auto x = 0.;
	auto y = 0;

	auto index = 0;
	auto rowHeight = 0;
	for (const auto &element : _elements) {
		const auto button = element.button.get();
		button->resizeToWidth(single);
		button->moveToLeft(int(std::round(x)), y);
		accumulate_max(rowHeight, button->height());
		x += single + skip;
		if (++index == kShowPerRow) {
			x = 0.;
			index = 0;
			y += rowHeight + st::themesSmallSkip;
			rowHeight = 0;
		}
	}
	return rowHeight
		? (y + rowHeight)
		: (y > 0)
		? (y - st::themesSmallSkip)
		: 0;
}

} // namespace Theme
} // namespace Window
