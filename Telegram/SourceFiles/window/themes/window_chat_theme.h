/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"
#include "base/timer.h"
#include "base/weak_ptr.h"

namespace Data {
struct CloudTheme;
} // namespace Data

namespace HistoryView {
struct PaintContext;
} // namespace HistoryView

namespace Ui {
struct BubblePattern;
} // namespace Ui

namespace Window::Theme {

struct CacheBackgroundRequest {
	QImage prepared;
	QImage preparedForTiled;
	QSize area;
	int gradientRotation = 0;
	bool tile = false;
	bool isPattern = false;
	bool recreateGradient = false;
	QImage gradient;
	std::vector<QColor> gradientColors;
	float64 gradientProgress = 1.;
	float64 patternOpacity = 1.;

	explicit operator bool() const {
		return !prepared.isNull() || !gradient.isNull();
	}
};

bool operator==(
	const CacheBackgroundRequest &a,
	const CacheBackgroundRequest &b);
bool operator!=(
	const CacheBackgroundRequest &a,
	const CacheBackgroundRequest &b);

struct CacheBackgroundResult {
	QImage image;
	QImage gradient;
	QSize area;
	int x = 0;
	int y = 0;
};

struct CachedBackground {
	CachedBackground() = default;
	CachedBackground(CacheBackgroundResult &&result);

	QPixmap pixmap;
	QSize area;
	int x = 0;
	int y = 0;
};

struct BackgroundState {
	CachedBackground was;
	CachedBackground now;
	float64 shown = 1.;
};

struct ChatThemeBackground {
	QImage prepared;
	QImage gradientForFill;
	std::optional<QColor> colorForFill;
};

struct ChatThemeDescriptor {
	Fn<void(style::palette&)> preparePalette;
	Fn<ChatThemeBackground()> prepareBackground;
	std::vector<QColor> backgroundColors;
};

class ChatTheme final : public base::has_weak_ptr {
public:
	ChatTheme();

	// Runs from background thread.
	ChatTheme(const Data::CloudTheme &theme);

	[[nodiscard]] uint64 key() const;
	[[nodiscard]] not_null<const style::palette*> palette() const {
		return _palette.get();
	}

	void setBackground(ChatThemeBackground);

	void setBubblesBackground(QImage image);
	const Ui::BubblePattern *bubblesBackgroundPattern() const {
		return _bubblesBackgroundPattern.get();
	}

	[[nodiscard]] HistoryView::PaintContext preparePaintContext(
		QRect viewport,
		QRect clip);
	[[nodiscard]] const BackgroundState &backgroundState(QSize area);
	[[nodiscard]] rpl::producer<> repaintBackgroundRequests() const;
	void rotateComplexGradientBackground();

private:
	void cacheBackground();
	void cacheBackgroundNow();
	void cacheBackgroundAsync(
		const CacheBackgroundRequest &request,
		Fn<void(CacheBackgroundResult&&)> done = nullptr);
	void clearCachedBackground();
	void setCachedBackground(CacheBackgroundResult &&cached);
	[[nodiscard]] CacheBackgroundRequest currentCacheRequest(
		QSize area,
		int addRotation = 0) const;
	[[nodiscard]] bool readyForBackgroundRotation() const;
	void generateNextBackgroundRotation();

	uint64 _id = 0;
	std::unique_ptr<style::palette> _palette;
	BackgroundState _backgroundState;
	Ui::Animations::Simple _backgroundFade;
	CacheBackgroundRequest _backgroundCachingRequest;
	CacheBackgroundResult _backgroundNext;
	int _backgroundAddRotation = 0;
	QSize _willCacheForArea;
	crl::time _lastAreaChangeTime = 0;
	std::optional<base::Timer> _cacheBackgroundTimer;
	CachedBackground _bubblesBackground;
	QImage _bubblesBackgroundPrepared;
	std::unique_ptr<Ui::BubblePattern> _bubblesBackgroundPattern;

	rpl::event_stream<> _repaintBackgroundRequests;

	rpl::lifetime _lifetime;

};

} // namespace Window::Theme
