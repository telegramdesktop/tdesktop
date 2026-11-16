// Render with: swift tools/mokagram/dmg/render-background.swift [--light | --minimal]
import AppKit

let directory = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
let light = CommandLine.arguments.contains("--light")
let minimal = CommandLine.arguments.contains("--minimal")
let width = 800
let height = 500
// Finder does not stretch picture backgrounds when its window is enlarged.
// Keep the layout in the initial 800 x 500 area and extend its solid backdrop
// to 4096 x 2304 points, including Retina representations.
let canvasWidth = 4096
let canvasHeight = 2304
func color(_ hex: UInt32) -> NSColor {
    NSColor(srgbRed: CGFloat((hex >> 16) & 255) / 255,
            green: CGFloat((hex >> 8) & 255) / 255,
            blue: CGFloat(hex & 255) / 255, alpha: 1)
}
for scale in [1, 2] {
    let bitmap = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: canvasWidth * scale,
        pixelsHigh: canvasHeight * scale, bitsPerSample: 8, samplesPerPixel: 4,
        hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB,
        bytesPerRow: 0, bitsPerPixel: 0)!
    bitmap.size = NSSize(width: canvasWidth, height: canvasHeight)
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: bitmap)
    // bitmap.size already maps logical points to Retina pixels.
    color(light ? 0xf1f1f1 : 0x1c1c1c).setFill()
    NSRect(x: 0, y: 0, width: canvasWidth, height: canvasHeight).fill()
    NSGraphicsContext.current!.cgContext.translateBy(x: 0, y: CGFloat(canvasHeight - height))
    func text(_ value: String, top: CGFloat, size: CGFloat, weight: NSFont.Weight,
              ink: UInt32) {
        let style = NSMutableParagraphStyle()
        style.alignment = .center
        (value as NSString).draw(in: NSRect(x: 24, y: CGFloat(height) - top - size * 1.5,
            width: CGFloat(width) - 48, height: size * 1.5), withAttributes: [
                .font: NSFont.systemFont(ofSize: size, weight: weight),
                .foregroundColor: color(ink), .paragraphStyle: style])
    }
    if minimal {
        text("mokaGram", top: 56, size: 36, weight: .semibold, ink: 0xffffff)
        text("Установка для macOS", top: 112, size: 14,
             weight: .regular, ink: 0x8e8e93)
        // Contrast only beneath the filename labels, with the icons on the main background.
        color(0xe2e2e2).setFill()
        for x in [144, 504] {
            NSBezierPath(roundedRect: NSRect(x: x, y: 148, width: 152, height: 36),
                         xRadius: 18, yRadius: 18).fill()
        }
        text("→", top: 210, size: 64, weight: .regular, ink: 0x0a84ff)
        text("Перетащите mokaGram в Applications", top: 378, size: 18,
             weight: .medium, ink: 0xffffff)
        text("Drag mokaGram into Applications to install", top: 411, size: 14,
             weight: .regular, ink: 0x8e8e93)
    } else if light {
        text("mokaGram", top: 56, size: 36, weight: .semibold, ink: 0x171717)
        text("Установка для macOS", top: 112, size: 14,
             weight: .regular, ink: 0x6e6e73)
        text("→", top: 210, size: 64, weight: .regular, ink: 0x0a84ff)
        text("Перетащите mokaGram в Applications", top: 378, size: 18,
             weight: .medium, ink: 0x171717)
        text("Drag mokaGram into Applications to install", top: 411, size: 14,
             weight: .regular, ink: 0x6e6e73)
    } else {
        text("mokaGram", top: 40, size: 36, weight: .semibold, ink: 0xffffff)
        text("Перетащите mokaGram в Applications", top: 99, size: 18,
             weight: .medium, ink: 0xffffff)
        // Finder supplies the actual icons and black filename labels on these light surfaces.
        color(0xf1f1f1).setFill()
        for x in [132, 492] {
            NSBezierPath(roundedRect: NSRect(x: x, y: 150, width: 176, height: 176),
                         xRadius: 28, yRadius: 28).fill()
        }
        text("→", top: 228, size: 48, weight: .regular, ink: 0x0a84ff)
        text("Drag mokaGram into Applications to install", top: 413, size: 15,
             weight: .regular, ink: 0x8e8e93)
    }
    NSGraphicsContext.restoreGraphicsState()
    let base = minimal ? "background-minimal" : (light ? "background-light" : "background")
    let name = scale == 1 ? "\(base).png" : "\(base)@2x.png"
    try bitmap.representation(using: .png, properties: [:])!.write(to: directory.appendingPathComponent(name))
}
