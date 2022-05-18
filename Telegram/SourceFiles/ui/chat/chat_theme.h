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

namespace style {
class palette;
struct colorizer;
} // namespace style

namespace Ui {

class ChatStyle;
struct ChatPaintContext;
struct BubblePattern;

struct ChatThemeBackground {
	QImage prepared;
	QImage preparedForTiled;
	QImage gradientForFill;
	std::optional<QColor> colorForFill;
	std::vector<QColor> colors;
	float64 patternOpacity = 1.;
	int gradientRotation = 0;
	bool isPattern = false;
	bool tile = false;

	[[nodiscard]] bool waitingForNegativePattern() const {
		return isPattern && prepared.isNull() && (patternOpacity < 0.);
	}
};

bool operator==(const ChatThemeBackground &a, const ChatThemeBackground &b);
bool operator!=(const ChatThemeBackground &a, const ChatThemeBackground &b);

struct ChatThemeBackgroundData {
	QString path;
	QByteArray bytes;
	bool gzipSvg = false;
	std::vector<QColor> colors;
	bool isPattern = false;
	float64 patternOpacity = 0.;
	bool isBlurred = false;
	bool generateGradient = false;
	int gradientRotation = 0;
};

struct ChatThemeBubblesData {
	std::vector<QColor> colors;
	std::optional<QColor> accent;
};

struct CacheBackgroundRequest {
	ChatThemeBackground background;
	QSize area;
	int gradientRotationAdd = 0;
	float64 gradientProgress = 1.;

	explicit operator bool() const {
		return !background.prepared.isNull()
			|| !background.gradientForFill.isNull();
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
	bool waitingForNegativePattern = false;
};

[[nodiscard]] CacheBackgroundResult CacheBackground(
	const CacheBackgroundRequest &request);

struct CachedBackground {
	CachedBackground() = default;
	CachedBackground(CacheBackgroundResult &&result);

	QPixmap pixmap;
	QSize area;
	int x = 0;
	int y = 0;
	bool waitingForNegativePattern = false;
};

struct BackgroundState {
	CachedBackground was;
	CachedBackground now;
	float64 shown = 1.;
};

struct ChatThemeKey {
	uint64 id = 0;
	bool dark = false;

	explicit operator bool() const {
		return (id != 0);
	}
};

inline bool operator<(ChatThemeKey a, ChatThemeKey b) {
	return (a.id < b.id) || ((a.id == b.id) && (a.dark < b.dark));
}
inline bool operator>(ChatThemeKey a, ChatThemeKey b) {
	return (b < a);
}
inline bool operator<=(ChatThemeKey a, ChatThemeKey b) {
	return !(b < a);
}
inline bool operator>=(ChatThemeKey a, ChatThemeKey b) {
	return !(a < b);
}
inline bool operator==(ChatThemeKey a, ChatThemeKey b) {
	return (a.id == b.id) && (a.dark == b.dark);
}
inline bool operator!=(ChatThemeKey a, ChatThemeKey b) {
	return !(a == b);
}

struct ChatThemeDescriptor {
	ChatThemeKey key;
	Fn<void(style::palette&)> preparePalette;
	ChatThemeBackgroundData backgroundData;
	ChatThemeBubblesData bubblesData;
	bool basedOnDark = false;
};

class ChatTheme final : public base::has_weak_ptr {
public:
	ChatTheme();

	// Expected to be invoked on a background thread. Invokes callbacks there.
	ChatTheme(ChatThemeDescriptor &&descriptor);

	~ChatTheme();

	[[nodiscard]] ChatThemeKey key() const;
	[[nodiscard]] const style::palette *palette() const {
		return _palette.get();
	}

	void setBackground(ChatThemeBackground &&background);
	void updateBackgroundImageFrom(ChatThemeBackground &&background);
	[[nodiscard]] const ChatThemeBackground &background() const {
		return _mutableBackground;
	}

	void setBubblesBackground(QImage image);
	[[nodiscard]] const BubblePattern *bubblesBackgroundPattern() const {
		return _bubblesBackgroundPattern.get();
	}
	void finishCreateOnMain(); // Called on_main after setBubblesBackground.

	[[nodiscard]] ChatPaintContext preparePaintContext(
		not_null<const ChatStyle*> st,
		QRect viewport,
		QRect clip);
	[[nodiscard]] const BackgroundState &backgroundState(QSize area);
	void clearBackgroundState();
	[[nodiscard]] rpl::producer<> repaintBackgroundRequests() const;
	void rotateComplexGradientBackground();

	[[nodiscard]] CacheBackgroundRequest cacheBackgroundRequest(
		QSize area,
		int addRotation = 0) const;

private:
	void cacheBackground();
	void cacheBackgroundNow();
	void cacheBackgroundAsync(
		const CacheBackgroundRequest &request,
		Fn<void(CacheBackgroundResult&&)> done = nullptr);
	void setCachedBackground(CacheBackgroundResult &&cached);
	[[nodiscard]] bool readyForBackgroundRotation() const;
	void generateNextBackgroundRotation();

	void cacheBubbles();
	void cacheBubblesNow();
	void cacheBubblesAsync(
		const CacheBackgroundRequest &request);
	[[nodiscard]] CacheBackgroundRequest cacheBubblesRequest(
		QSize area) const;

	[[nodiscard]] style::colorizer bubblesAccentColorizer(
		const QColor &accent) const;
	void adjustPalette(const ChatThemeDescriptor &descriptor);
	void set(const style::color &my, const QColor &color);
	void adjust(const style::color &my, const QColor &by);
	void adjust(const style::color &my, const style::colorizer &by);

	ChatThemeKey _key;
	std::unique_ptr<style::palette> _palette;
	ChatThemeBackground _mutableBackground;
	BackgroundState _backgroundState;
	Animations::Simple _backgroundFade;
	CacheBackgroundRequest _backgroundCachingRequest;
	CacheBackgroundResult _backgroundNext;
	QSize _cacheBackgroundArea;
	crl::time _lastBackgroundAreaChangeTime = 0;
	std::optional<base::Timer> _cacheBackgroundTimer;

	CachedBackground _bubblesBackground;
	QImage _bubblesBackgroundPrepared;
	CacheBackgroundRequest _bubblesCachingRequest;
	QSize _cacheBubblesArea;
	crl::time _lastBubblesAreaChangeTime = 0;
	std::optional<base::Timer> _cacheBubblesTimer;
	std::unique_ptr<BubblePattern> _bubblesBackgroundPattern;

	rpl::event_stream<> _repaintBackgroundRequests;

	rpl::lifetime _lifetime;

};

struct ChatBackgroundRects {
	QRect from;
	QRect to;
};
[[nodiscard]] ChatBackgroundRects ComputeChatBackgroundRects(
	QSize fillSize,
	QSize imageSize);

[[nodiscard]] QColor CountAverageColor(const QImage &image);
[[nodiscard]] QColor CountAverageColor(const std::vector<QColor> &colors);
[[nodiscard]] bool IsPatternInverted(
	const std::vector<QColor> &background,
	float64 patternOpacity);
[[nodiscard]] QColor ThemeAdjustedColor(QColor original, QColor background);
[[nodiscard]] QImage PreprocessBackgroundImage(QImage image);
[[nodiscard]] std::optional<QColor> CalculateImageMonoColor(
	const QImage &image);
[[nodiscard]] QImage PrepareImageForTiled(const QImage &prepared);

[[nodiscard]] QImage ReadBackgroundImage(
	const QString &path,
	const QByteArray &content,
	bool gzipSvg);
[[nodiscard]] QImage GenerateBackgroundImage(
	QSize size,
	const std::vector<QColor> &bg,
	int gradientRotation,
	float64 patternOpacity = 1.,
	Fn<void(QPainter&,bool)> drawPattern = nullptr);
[[nodiscard]] QImage InvertPatternImage(QImage pattern);
[[nodiscard]] QImage PreparePatternImage(
	QImage pattern,
	const std::vector<QColor> &bg,
	int gradientRotation,
	float64 patternOpacity);
[[nodiscard]] QImage PrepareBlurredBackground(QImage image);
[[nodiscard]] QImage GenerateDitheredGradient(
	const std::vector<QColor> &colors,
	int rotation);
[[nodiscard]] ChatThemeBackground PrepareBackgroundImage(
	const ChatThemeBackgroundData &data);

} // namespace Ui
