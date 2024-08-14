------------------------------------
How to Get VGMTrans Working on macOS
------------------------------------

TL;DR: After installing VGMTrans in your Applications folder, run the included Fix Script to bypass macOS restrictions. This step is required because we choose not to pay Apple $99 for app notarization.

----------------------------------
Why You Need to Run the Fix Script
----------------------------------

When you try to open VGMTrans, you might see the error:

"VGMTrans is damaged and can't be opened. You should move it to the Trash."

This message is misleading. Your download is fine. The issue is due to Apple's Gatekeeper service, which blocks apps that haven't been notarized. Notarization is a process Apple uses to control software distribution on macOS and requires developers to pay $99 annually, even for free and open-source software. We believe this requirement is monopolistic and harms users and developers, so we've provided a workaround.

------------------------
What the Fix Script Does
------------------------

The Fix Script will:

- Remove the quarantine attribute: This lets macOS know the app is safe to run.
- Self-sign the app: This provides a basic signature so macOS allows the app to run.
