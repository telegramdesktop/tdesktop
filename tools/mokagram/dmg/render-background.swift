// Render with: swift tools/mokagram/dmg/render-background.swift
import AppKit

let directory = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
let width = 800
let height = 500
func color(_ hex: UInt32) -> NSColor {
    NSColor(srgbRed: CGFloat((hex >> 16) & 255) / 255,
            green: CGFloat((hex >> 8) & 255) / 255,
            blue: CGFloat(hex & 255) / 255, alpha: 1)
}
for scale in [1, 2] {
    let bitmap = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: width * scale,
        pixelsHigh: height * scale, bitsPerSample: 8, samplesPerPixel: 4,
        hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB,
        bytesPerRow: 0, bitsPerPixel: 0)!
    bitmap.size = NSSize(width: width, height: height)
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: bitmap)
    // bitmap.size already maps logical points to Retina pixels.
    color(0x1c1c1c).setFill()
    NSRect(x: 0, y: 0, width: width, height: height).fill()
    func text(_ value: String, top: CGFloat, size: CGFloat, weight: NSFont.Weight,
              ink: UInt32) {
        let style = NSMutableParagraphStyle()
        style.alignment = .center
        (value as NSString).draw(in: NSRect(x: 24, y: CGFloat(height) - top - size * 1.5,
            width: CGFloat(width) - 48, height: size * 1.5), withAttributes: [
                .font: NSFont.systemFont(ofSize: size, weight: weight),
                .foregroundColor: color(ink), .paragraphStyle: style])
    }
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
    NSGraphicsContext.restoreGraphicsState()
    let name = scale == 1 ? "background.png" : "background@2x.png"
    try bitmap.representation(using: .png, properties: [:])!.write(to: directory.appendingPathComponent(name))
}
