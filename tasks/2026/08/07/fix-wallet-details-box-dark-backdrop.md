# Wallet details box backdrop in the dark theme

No client code changes: this task measured the wallet transaction details
box backdrop and found it already correct, so nothing was repaired.

The box paints its own backdrop from `st::walletDetailsBox { bg:
windowBgOver; }` (`Telegram/SourceFiles/wallet/wallet.style`), applied by
`box->setStyle(st::walletDetailsBox)` in `WalletTransactionBox`
(`Telegram/SourceFiles/wallet/wallet_content.cpp`). It was reported to stay
at the day value `#f1f1f1` in the dark theme. An in-binary probe on a plain
GRAM transfer's box measured `Ui::BoxLayerWidget`'s rendered pixels in three
states and found the shipped behaviour correct in all of them:

- light: `#f1f1f1`, matching `st::windowBgOver`;
- dark with the box opened fresh: `#232e3c`;
- dark after a live night-mode toggle with the box left open: `#232e3c`,
  with a `QEvent::Paint` confirmed to reach the layer after the palette
  changed.

The report came from a capture artifact. `WalletTransactionBox` calls
`setNoContentMargin(true)`, which clears `Qt::WA_OpaquePaintEvent` on the
`Ui::BoxContent`, so `BoxContent::paintEvent` paints nothing and the
backdrop belongs to the parent `Ui::BoxLayerWidget`. Grabbing the
`Ui::BoxContent` therefore yields Qt's default `QPalette::Window`
(`#f0f0f0` on Windows, which `Ui::SeparatePanel` never overrides), in both
themes alike. A box that uses `setNoContentMargin(true)` must be captured
at the `Ui::BoxLayerWidget`.

One real defect was found while measuring and is routed separately:
`AddDetailsAmountHeader` bakes the amount colour as a `QColor` through
`setTextColorOverride`, so a details box left open across a live night-mode
toggle keeps the light amount colour on the dark backdrop.

Measurements, captures and the probe overlay live in the AI task's `work/`
and `evidence/` directories.
