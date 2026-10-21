/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_cursor.h"

#include "renderer-litehtml/src/renderer.h"
#include "renderer-litehtml/src/theme.h"
#include "twidget/json.h"
#include "tw_ua_stylesheet.h"

#include <litehtml.h>

namespace Tgw {
namespace {

using Elements = std::unordered_map<int64, std::shared_ptr<litehtml::element>>;
using Chain = std::vector<std::shared_ptr<litehtml::element>>;

constexpr auto kFontHandle = litehtml::uint_ptr(1);
constexpr auto kDefaultFontSize = 16;
constexpr auto kDefaultFontName = "-apple-system, system-ui, sans-serif";
constexpr auto kLanguage = "en";
constexpr auto kCssDpi = 96.f;
constexpr auto kPointsPerInch = 72.f;
constexpr auto kColorBits = 8;
constexpr auto kColorIndex = 256;
constexpr auto kPointerCursor = "pointer";
constexpr auto kIdAttribute = "data-twid";
constexpr auto kIdSelector = "[data-twid]";

// Byte-for-byte copy of UA_EXTRA in renderer-litehtml/src/renderer.cpp.
constexpr auto kUaExtra = R"css(
button,input,select,textarea,meter,progress{box-sizing:border-box}
input,select,textarea,button{display:inline-block;border:1px solid #8f8f8f;border-radius:3px;background:#fff;color:#000;padding:2px 6px;font:13.333px -apple-system,system-ui,sans-serif;min-height:1.8em;min-width:2em;vertical-align:middle}
button{background:#efefef;padding:1px 6px;text-align:center}
input[type=checkbox],input[type=radio]{min-width:13px;width:13px;height:13px;min-height:13px;padding:0;margin:3px 3px 3px 4px;vertical-align:middle}
input[type=radio]{border-radius:50%}
input[type=range]{border:none;background:transparent;padding:0}
input[type=color]{width:50px;height:27px;padding:2px}
input[type=hidden]{display:none}
textarea{display:block;min-height:3em;white-space:pre-wrap}
progress,meter{display:inline-block;width:10em;height:1em;background:#e6e6e6;border-radius:2px}
canvas{display:inline-block;background:transparent}
svg{display:inline-block}
details>summary{display:block;cursor:pointer}
details:not([open])>*:not(summary){display:none}
dialog:not([open]){display:none}
[hidden]{display:none}
)css";

[[nodiscard]] const std::string &MasterStyles() {
	// Built as in twlite::Renderer::apply_theme(); no theme value sets a cursor.
	static const auto result = std::string(litehtml::master_css)
		+ kUaExtra
		+ twlite::theme::unwrap_where(twlite::theme::substitute(
			TW_UA_STYLESHEET,
			twlite::theme::variables(twidget::Json::object())));
	return result;
}

[[nodiscard]] std::shared_ptr<litehtml::element> Lookup(
		const Elements &elements,
		int64 id) {
	const auto i = elements.find(id);
	return (i != end(elements)) ? i->second : nullptr;
}

[[nodiscard]] bool MarkChain(
		std::shared_ptr<litehtml::element> element,
		litehtml::string_id pseudo,
		bool add) {
	auto result = false;
	while (element) {
		result = element->set_pseudo_class(pseudo, add) || result;
		element = element->parent();
	}
	return result;
}

[[nodiscard]] bool Retarget(
		const Elements &elements,
		int64 &applied,
		int64 wanted,
		litehtml::string_id pseudo) {
	if (applied == wanted) {
		return false;
	}
	const auto cleared = MarkChain(Lookup(elements, applied), pseudo, false);
	const auto marked = MarkChain(Lookup(elements, wanted), pseudo, true);
	applied = wanted;
	return cleared || marked;
}

[[nodiscard]] Chain ChainOf(std::shared_ptr<litehtml::element> element) {
	auto result = Chain();
	while (element) {
		result.push_back(element);
		element = element->parent();
	}
	return result;
}

[[nodiscard]] bool Reconcile(
		const Elements &elements,
		const Chain &marked,
		int64 &applied,
		litehtml::string_id pseudo) {
	if (ChainOf(Lookup(elements, applied)) == marked) {
		return false;
	}
	applied = 0;
	auto result = false;
	for (const auto &element : marked) {
		result = element->set_pseudo_class(pseudo, false) || result;
	}
	return result;
}

} // namespace

class CursorStyles::Container final : public litehtml::document_container {
public:
	explicit Container(double dpr);

	void resize(double width, int height);

	litehtml::uint_ptr create_font(
		const litehtml::font_description &descr,
		const litehtml::document *doc,
		litehtml::font_metrics *fm) override;
	void delete_font(litehtml::uint_ptr font) override;
	litehtml::pixel_t text_width(
		const char *text,
		litehtml::uint_ptr font) override;
	void draw_text(
		litehtml::uint_ptr hdc,
		const char *text,
		litehtml::uint_ptr font,
		litehtml::web_color color,
		const litehtml::position &pos) override;
	litehtml::pixel_t pt_to_px(float pt) const override;
	litehtml::pixel_t get_default_font_size() const override;
	const char *get_default_font_name() const override;
	void draw_list_marker(
		litehtml::uint_ptr hdc,
		const litehtml::list_marker &marker) override;
	void load_image(
		const char *src,
		const char *baseurl,
		bool redraw_on_ready) override;
	void get_image_size(
		const char *src,
		const char *baseurl,
		litehtml::size &sz) override;
	void draw_image(
		litehtml::uint_ptr hdc,
		const litehtml::background_layer &layer,
		const std::string &url,
		const std::string &base_url) override;
	void draw_solid_fill(
		litehtml::uint_ptr hdc,
		const litehtml::background_layer &layer,
		const litehtml::web_color &color) override;
	void draw_linear_gradient(
		litehtml::uint_ptr hdc,
		const litehtml::background_layer &layer,
		const litehtml::background_layer::linear_gradient &gradient) override;
	void draw_radial_gradient(
		litehtml::uint_ptr hdc,
		const litehtml::background_layer &layer,
		const litehtml::background_layer::radial_gradient &gradient) override;
	void draw_conic_gradient(
		litehtml::uint_ptr hdc,
		const litehtml::background_layer &layer,
		const litehtml::background_layer::conic_gradient &gradient) override;
	void draw_borders(
		litehtml::uint_ptr hdc,
		const litehtml::borders &borders,
		const litehtml::position &draw_pos,
		bool root) override;
	void set_caption(const char *caption) override;
	void set_base_url(const char *base_url) override;
	void link(
		const std::shared_ptr<litehtml::document> &doc,
		const litehtml::element::ptr &el) override;
	void on_anchor_click(
		const char *url,
		const litehtml::element::ptr &el) override;
	void on_mouse_event(
		const litehtml::element::ptr &el,
		litehtml::mouse_event event) override;
	void set_cursor(const char *cursor) override;
	void transform_text(
		std::string &text,
		litehtml::text_transform tt) override;
	void import_css(
		std::string &text,
		const std::string &url,
		std::string &baseurl) override;
	void set_clip(
		const litehtml::position &pos,
		const litehtml::border_radiuses &radiuses) override;
	void del_clip() override;
	void get_viewport(litehtml::position &viewport) const override;
	litehtml::element::ptr create_element(
		const char *tag_name,
		const litehtml::string_map &attributes,
		const std::shared_ptr<litehtml::document> &doc) override;
	void get_media_features(litehtml::media_features &media) const override;
	void get_language(
		std::string &language,
		std::string &culture) const override;

private:
	const double _dpr = 1.;
	double _width = 0.;
	int _height = 0;

};

CursorStyles::Container::Container(double dpr)
: _dpr(dpr) {
}

void CursorStyles::Container::resize(double width, int height) {
	_width = width;
	_height = height;
}

litehtml::uint_ptr CursorStyles::Container::create_font(
		const litehtml::font_description &descr,
		const litehtml::document*,
		litehtml::font_metrics *fm) {
	if (fm) {
		const auto size = descr.size.value();
		fm->font_size = descr.size;
		fm->height = fm->ascent = litehtml::pixel_t(size);
		fm->descent = litehtml::pixel_t(0.f);
		fm->x_height = fm->ch_width = litehtml::pixel_t(size / 2.f);
	}
	return kFontHandle;
}

void CursorStyles::Container::delete_font(litehtml::uint_ptr) {
}

litehtml::pixel_t CursorStyles::Container::text_width(
		const char*,
		litehtml::uint_ptr) {
	return litehtml::pixel_t(0.f);
}

void CursorStyles::Container::draw_text(
		litehtml::uint_ptr,
		const char*,
		litehtml::uint_ptr,
		litehtml::web_color,
		const litehtml::position&) {
}

litehtml::pixel_t CursorStyles::Container::pt_to_px(float pt) const {
	return litehtml::pixel_t(pt * kCssDpi / kPointsPerInch);
}

litehtml::pixel_t CursorStyles::Container::get_default_font_size() const {
	return litehtml::pixel_t(kDefaultFontSize);
}

const char *CursorStyles::Container::get_default_font_name() const {
	return kDefaultFontName;
}

void CursorStyles::Container::draw_list_marker(
		litehtml::uint_ptr,
		const litehtml::list_marker&) {
}

void CursorStyles::Container::load_image(const char*, const char*, bool) {
}

void CursorStyles::Container::get_image_size(
		const char*,
		const char*,
		litehtml::size &sz) {
	sz = litehtml::size();
}

void CursorStyles::Container::draw_image(
		litehtml::uint_ptr,
		const litehtml::background_layer&,
		const std::string&,
		const std::string&) {
}

void CursorStyles::Container::draw_solid_fill(
		litehtml::uint_ptr,
		const litehtml::background_layer&,
		const litehtml::web_color&) {
}

void CursorStyles::Container::draw_linear_gradient(
		litehtml::uint_ptr,
		const litehtml::background_layer&,
		const litehtml::background_layer::linear_gradient&) {
}

void CursorStyles::Container::draw_radial_gradient(
		litehtml::uint_ptr,
		const litehtml::background_layer&,
		const litehtml::background_layer::radial_gradient&) {
}

void CursorStyles::Container::draw_conic_gradient(
		litehtml::uint_ptr,
		const litehtml::background_layer&,
		const litehtml::background_layer::conic_gradient&) {
}

void CursorStyles::Container::draw_borders(
		litehtml::uint_ptr,
		const litehtml::borders&,
		const litehtml::position&,
		bool) {
}

void CursorStyles::Container::set_caption(const char*) {
}

void CursorStyles::Container::set_base_url(const char*) {
}

void CursorStyles::Container::link(
		const std::shared_ptr<litehtml::document>&,
		const litehtml::element::ptr&) {
}

void CursorStyles::Container::on_anchor_click(
		const char*,
		const litehtml::element::ptr&) {
}

void CursorStyles::Container::on_mouse_event(
		const litehtml::element::ptr&,
		litehtml::mouse_event) {
}

void CursorStyles::Container::set_cursor(const char*) {
}

void CursorStyles::Container::transform_text(
		std::string&,
		litehtml::text_transform) {
}

void CursorStyles::Container::import_css(
		std::string &text,
		const std::string&,
		std::string&) {
	text.clear();
}

void CursorStyles::Container::set_clip(
		const litehtml::position&,
		const litehtml::border_radiuses&) {
}

void CursorStyles::Container::del_clip() {
}

void CursorStyles::Container::get_viewport(
		litehtml::position &viewport) const {
	viewport.x = viewport.y = litehtml::pixel_t(0.f);
	viewport.width = litehtml::pixel_t(float(_width));
	viewport.height = litehtml::pixel_t(float(_height));
}

litehtml::element::ptr CursorStyles::Container::create_element(
		const char*,
		const litehtml::string_map&,
		const std::shared_ptr<litehtml::document>&) {
	return nullptr;
}

void CursorStyles::Container::get_media_features(
		litehtml::media_features &media) const {
	media.type = litehtml::media_type_screen;
	media.width = litehtml::pixel_t(float(_width));
	media.height = litehtml::pixel_t(float(_height));
	media.device_width = media.width;
	media.device_height = media.height;
	media.color = kColorBits;
	media.color_index = kColorIndex;
	media.monochrome = 0;
	media.resolution = litehtml::pixel_t(float(kCssDpi * _dpr));
}

void CursorStyles::Container::get_language(
		std::string &language,
		std::string &culture) const {
	language = kLanguage;
	culture.clear();
}

CursorStyles::CursorStyles(double dpr)
: _container(std::make_unique<Container>(dpr)) {
}

CursorStyles::~CursorStyles() {
	_elements.clear();
	_document = nullptr;
}

bool CursorStyles::pointer(
		const twlite::Renderer &renderer,
		int64 hit,
		int64 hover,
		int64 active) {
	const auto reconciled = refresh(renderer);
	const auto hoverChanged = Retarget(
		_elements,
		_hover,
		hover,
		litehtml::_hover_);
	const auto activeChanged = Retarget(
		_elements,
		_active,
		active,
		litehtml::_active_);
	if (reconciled || hoverChanged || activeChanged) {
		const auto ignore = [](const litehtml::position&) {};
		_document->root()->find_styles_changes(ignore);
	}
	const auto element = Lookup(_elements, hit);
	return element && (element->css().get_cursor() == kPointerCursor);
}

bool CursorStyles::refresh(const twlite::Renderer &renderer) {
	const auto builds = renderer.stats().builds;
	if (_document && _builds == builds) {
		return false;
	}
	_builds = builds;
	const auto &html = renderer.last_html();
	const auto width = renderer.width();
	const auto height = renderer.viewport_height();
	const auto sized = (_width == width) && (_height == height);
	if (_document && sized && (_html == html)) {
		return false;
	}
	_html = html;
	if (_document && sized) {
		const auto hover = ChainOf(Lookup(_elements, _hover));
		const auto active = ChainOf(Lookup(_elements, _active));
		auto changed = false;
		if (_document->update_from_string(_html, &changed)) {
			if (!changed || _document->last_patch_paint_only()) {
				return false;
			}
			index();
			const auto hoverCleared = Reconcile(
				_elements,
				hover,
				_hover,
				litehtml::_hover_);
			const auto activeCleared = Reconcile(
				_elements,
				active,
				_active,
				litehtml::_active_);
			return hoverCleared || activeCleared;
		}
	}
	rebuild(width, height);
	return false;
}

void CursorStyles::rebuild(double width, int height) {
	_hover = _active = 0;
	_elements.clear();
	_document = nullptr;
	_width = width;
	_height = height;
	_container->resize(width, height);
	_document = litehtml::document::createUpdatableFromString(
		_html,
		_container.get(),
		MasterStyles());
	index();
}

void CursorStyles::index() {
	_elements.clear();
	const auto root = _document->root();
	if (!root) {
		return;
	}
	for (const auto &element : root->select_all(kIdSelector)) {
		if (const auto value = element->get_attr(kIdAttribute)) {
			_elements[int64(std::strtoll(value, nullptr, 10))] = element;
		}
	}
}

} // namespace Tgw
