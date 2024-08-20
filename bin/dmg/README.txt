------------------------------------
How to Get VGMTrans Working on macOS
------------------------------------

1. Drag VGMTrans into the Applications folder.
2. Ctrl-click > Open the included "Unlock & Sign" script to bypass macOS restrictions.

-----------------------------------------------
Why You Need to Run the "Unblock & Sign" Script
-----------------------------------------------

In short, because we don't want to pay Apple $99 to distribute a free app.

When opening VGMTrans, you may encounter this message:

"VGMTrans is damaged and can't be opened. You should move it to the Trash."

This message is misleading - your download is fine. The issue stems from Apple's Gatekeeper service, which blocks apps that haven't been notarized. Notarization is Apple's process for controlling software distribution on macOS, requiring developers to pay $99 annually - even for free and open-source software. We believe this requirement is monopolistic and harmful to both users and developers. Rather than pay the fee, we've provided a workaround.

-------------------------------------
What the "Unblock & Sign" Script Does
-------------------------------------

The script will:

- Remove the quarantine attribute: This lets macOS know the app is safe to run.
- Self-sign the app: This provides a basic signature so macOS allows the app to run.
