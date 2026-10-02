// Generates an A4 test page PDF: text, fine lines, gray ramp, margin marks.
// Usage: swift make-testpage.swift out.pdf
// SPDX-License-Identifier: GPL-2.0-or-later
import CoreGraphics
import CoreText
import Foundation

let out = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "testpage.pdf"
var box = CGRect(x: 0, y: 0, width: 595, height: 842)
guard let ctx = CGContext(URL(fileURLWithPath: out) as CFURL, mediaBox: &box, nil) else {
  fatalError("cannot create \(out)")
}

func text(_ s: String, _ x: CGFloat, _ y: CGFloat, size: CGFloat = 12, bold: Bool = false) {
  let font = CTFontCreateWithName((bold ? "Helvetica-Bold" : "Helvetica") as CFString, size, nil)
  let attrs: [NSAttributedString.Key: Any] = [
    NSAttributedString.Key(kCTFontAttributeName as String): font,
    NSAttributedString.Key(kCTForegroundColorAttributeName as String): CGColor(gray: 0, alpha: 1),
  ]
  let line = CTLineCreateWithAttributedString(NSAttributedString(string: s, attributes: attrs))
  ctx.textPosition = CGPoint(x: x, y: y)
  CTLineDraw(line, ctx)
}

ctx.beginPDFPage(nil)

// Corner marks 12pt in from each edge (the PPD's imageable area).
ctx.setStrokeColor(gray: 0, alpha: 1)
ctx.setLineWidth(1)
for (x, y, dx, dy) in [(12.0, 12.0, 1.0, 1.0), (583.0, 12.0, -1.0, 1.0),
                       (12.0, 830.0, 1.0, -1.0), (583.0, 830.0, -1.0, -1.0)] {
  ctx.move(to: CGPoint(x: x + 30 * dx, y: y)); ctx.addLine(to: CGPoint(x: x, y: y))
  ctx.addLine(to: CGPoint(x: x, y: y + 30 * dy))
}
ctx.strokePath()

text("Brother HL-1210W native driver", 50, 770, size: 24, bold: true)
text("Test page - if you can read this, the arm64 filter works.", 50, 745, size: 13)
text("The quick brown fox jumps over the lazy dog. 0123456789", 50, 720, size: 11)
text("Small 6pt text: The quick brown fox jumps over the lazy dog.", 50, 705, size: 6)
text("\(Date())", 50, 690, size: 9)
text("Each corner mark should be about 4 mm from both paper edges.", 50, 675, size: 9)

// Gray ramp, 11 steps from white to black.
for i in 0...10 {
  ctx.setFillColor(gray: 1 - CGFloat(i) / 10, alpha: 1)
  ctx.fill(CGRect(x: 50 + CGFloat(i) * 45, y: 600, width: 45, height: 60))
}
ctx.setStrokeColor(gray: 0, alpha: 1)
ctx.stroke(CGRect(x: 50, y: 600, width: 495, height: 60))
text("Gray ramp 0% -> 100%", 50, 585, size: 9)

// Hairlines of increasing width.
for i in 0..<8 {
  let w = 0.25 * CGFloat(i + 1)
  ctx.setLineWidth(w)
  ctx.move(to: CGPoint(x: 50, y: 550 - CGFloat(i) * 12))
  ctx.addLine(to: CGPoint(x: 400, y: 550 - CGFloat(i) * 12))
  ctx.strokePath()
  text(String(format: "%.2f pt", w), 410, 547 - CGFloat(i) * 12, size: 8)
}

// Smooth radial gradient disc.
let space = CGColorSpaceCreateDeviceGray()
let grad = CGGradient(colorsSpace: space,
                      colors: [CGColor(gray: 0, alpha: 1), CGColor(gray: 1, alpha: 1)] as CFArray,
                      locations: [0, 1])!
ctx.saveGState()
ctx.addEllipse(in: CGRect(x: 50, y: 250, width: 200, height: 200))
ctx.clip()
ctx.drawRadialGradient(grad, startCenter: CGPoint(x: 150, y: 350), startRadius: 0,
                       endCenter: CGPoint(x: 150, y: 350), endRadius: 100, options: [])
ctx.restoreGState()

// Checkerboard and solid block (exercise the repeat/substitute encoder).
for r in 0..<10 { for c in 0..<10 where (r + c) % 2 == 0 {
  ctx.setFillColor(gray: 0, alpha: 1)
  ctx.fill(CGRect(x: 300 + CGFloat(c) * 20, y: 250 + CGFloat(r) * 20, width: 20, height: 20))
} }
ctx.fill(CGRect(x: 50, y: 100, width: 495, height: 100))
text("Solid block above; checkerboard and gradient disc further up.", 50, 85, size: 9)

ctx.endPDFPage()
ctx.closePDF()
print("wrote \(out)")
