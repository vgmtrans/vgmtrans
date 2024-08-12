tldr: Run the Fix Script to get VGMTrans working on your Mac after installing it in the Applications folder. The script is necessary because the developers don't want to pay Apple $99 to distribute their free and open source app.

----------------------------------
Why You Need to Run the Fix Script
----------------------------------

When you open VGMTrans, you may encounter the error:

"VGMTrans" is damaged and can't be opened. You should move it to the Trash."

This message is misleading. Your download is not damaged. This issue arises because of Apple's Gatekeeper system, which restricts the use of software that hasn't been officially notarized by Apple. While Apple claims that this system is designed to protect users from malicious software, it also serves to enforce Apple's control over what can and cannot be run on your Mac, and it forces developers to pay Apple to distribute their software in any capacity.

To distribute apps on macOS without encountering these issues, developers are required to pay $99 annually for an Apple Developer account and submit their apps for notarization. This requirement applies even to free and open-source software, which can be a significant financial burden for independent developers and small projects. By making notarization a paid service, Apple is extending its monopolistic practices from iOS to macOS, limiting user choice, developer freedom, and lining their pockets in the process.

------------------------
What the Fix Script Does
------------------------

To help you bypass these restrictions, we've provided a script that you can run after installing the app. The script will:

1. Temporarily disable Gatekeeper: This step is necessary to allow the app to be modified.
2. Remove the quarantine attribute: This tells macOS that the app is safe to run, even though
   Apple hasn't notarized it.
3. Self-sign the app: This gives the app a basic signature so that macOS will allow it to run.
4. Re-enable Gatekeeper (if it was enabled): Your system's security will be restored to its
   original state after the process finishes.
