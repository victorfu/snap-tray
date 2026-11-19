# Screenshot clipboard compatibility

Tested on Ubuntu 22.04.5 X11 with Qt 6.12.0 on November 19, 2026, for [issue #360](https://github.com/victorfu/snap-tray/issues/360). The test used a private Xvfb display, a separate settings directory, and a synthetic 320 × 240 screenshot.

| Receiver | Paste check | Observed result |
| --- | --- | --- |
| Thunar 4.16.10 | Paste into an empty folder | One PNG, byte-for-byte identical to the PNG on the clipboard |
| Google Chrome 157.0.8103.38 | Paste into an editable webpage and decode the supplied file | One PNG image, 320 × 240 pixels |
| LibreOffice Draw 7.3.7.2 | Paste into an empty drawing and inspect the inserted objects | One graphic object, 320 × 240 pixels |

The captures came from `SnapTray region -r 0,0,320,240 -c` with **Image and PNG file** selected. The CLI exited before paste. The file and image remained available after starting and quitting the tray app. Repeating the receiver tests with Xfce Clipman 1.6.2 running gave the same results. An active file aged eight days survived startup. After a subsequent copy replaced the reference, the next cleanup removed the expired file.

On the base revision `7434e173`, the same copy exposed image targets alone. With the feature enabled, the private clipboard advertised both `image/png` and `text/uri-list`. An injected disk-full error during the PNG write made the CLI report failure and preserved the previous clipboard URL.

The Linux file and lifecycle tests can be run on a private desktop with the following command after building the tests.

```sh
XDG_SESSION_TYPE=x11 SNAPTRAY_TEST_X11_CLIPBOARD=1 xvfb-run -a \
    ctest --test-dir build -R '^(Platform_ClipboardImageFile|Platform_LinuxClipboardOwner|PinWindow_StyleSync)$' --output-on-failure
```

Finder and Explorer paste, including paste after quitting and restarting SnapTray, still require native desktop verification.
