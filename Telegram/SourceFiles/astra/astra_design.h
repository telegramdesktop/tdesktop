// Astra UI — Sound Glass design system.
/*
Astra UI — design tokens ("Sound Glass").
Part of the Astra UI redesign for this Telegram Desktop fork.
*/
#pragma once

#include <QColor>
#include <QGradient>
#include <QLinearGradient>

namespace Astra {
namespace Design {

// ============ radii ============
inline constexpr auto kRadiusWindow = 22;
inline constexpr auto kRadiusPanel = 26;
inline constexpr auto kRadiusCard = 18;
inline constexpr auto kRadiusControl = 12;
inline constexpr auto kRadiusAvatar = 38; // squircle-ish

// ============ palette — "Sound Glass" (dark) ============
namespace Dark {

inline QColor Background() { return QColor(0x0A, 0x0B, 0x0F); }
inline QColor Surface() { return QColor(0x12, 0x13, 0x1A); }
inline QColor Glass() { return QColor(255, 255, 255, 14); }
inline QColor GlassHover() { return QColor(255, 255, 255, 22); }
inline QColor Stroke() { return QColor(255, 255, 255, 23); }
inline QColor StrokeStrong() { return QColor(255, 255, 255, 41); }
inline QColor Text() { return QColor(0xF4, 0xF5, 0xF8); }
inline QColor TextSecondary() { return QColor(0x9B, 0xA1, 0xAE); }
inline QColor TextTertiary() { return QColor(0x66, 0x6C, 0x7A); }

inline QColor Accent() { return QColor(0x7C, 0x5C, 0xFF); }
inline QColor AccentAlt() { return QColor(0x4E, 0xA8, 0xFF); }
inline QColor Live() { return QColor(0x3E, 0xD5, 0x98); }
inline QColor LiveText() { return QColor(0x7C, 0xE8, 0xBE); }

// waveform played / unplayed bar colors
inline QColor WavePlayed() { return QColor(0x8E, 0x9A, 0xFF); }
inline QColor WaveUnplayed() { return QColor(255, 255, 255, 70); }
inline QColor WaveTick() { return QColor(0x4E, 0xA8, 0xFF); }

} // namespace Dark

// ============ palette — "Sound Paper" (light) ============
namespace Light {

inline QColor Background() { return QColor(0xF5, 0xF3, 0xEF); }
inline QColor Surface() { return QColor(0xFF, 0xFF, 0xFF); }
inline QColor Glass() { return QColor(255, 255, 255, 180); }
inline QColor GlassHover() { return QColor(255, 255, 255, 225); }
inline QColor Stroke() { return QColor(0, 0, 0, 20); }
inline QColor StrokeStrong() { return QColor(0, 0, 0, 38); }
inline QColor Text() { return QColor(0x22, 0x26, 0x2C); }
inline QColor TextSecondary() { return QColor(0x6E, 0x6A, 0x61); }
inline QColor TextTertiary() { return QColor(0x9A, 0x94, 0x88); }

inline QColor Accent() { return QColor(0x6A, 0x4B, 0xE8); }
inline QColor AccentAlt() { return QColor(0x2E, 0x7C, 0xE8); }
inline QColor Live() { return QColor(0x14, 0xA8, 0x76); }
inline QColor LiveText() { return QColor(0x0E, 0x7C, 0x56); }

inline QColor WavePlayed() { return QColor(0x6A, 0x4B, 0xE8); }
inline QColor WaveUnplayed() { return QColor(0, 0, 0, 55); }
inline QColor WaveTick() { return QColor(0x2E, 0x7C, 0xE8); }

} // namespace Light

// ============ active theme selector ============
// v1: dark-first (mockup theme). Flip to Light::* for paper theme.
namespace Active = Dark;

// ============ materials ============

// Signature accent gradient (buttons, active dock tile).
inline QLinearGradient AccentGradient(const QRect &r) {
	auto g = QLinearGradient(r.topLeft(), r.bottomRight());
	g.setColorAt(0., Dark::Accent());
	g.setColorAt(1., Dark::AccentAlt());
	return g;
}

// Vinyl groove colors for the record-shelf discs.
inline QColor VinylBase() { return QColor(0x10, 0x11, 0x16); }
inline QColor VinylGroove() { return QColor(0x17, 0x18, 0x1F); }
inline QColor VinylLabel(QColor accent) { return accent; }

} // namespace Design
} // namespace Astra
