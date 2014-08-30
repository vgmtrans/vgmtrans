#!/bin/bash
# a little script to fix project files for use with AppCode after qmake conversion

qmake -spec macx-xcode VGMTrans.pro
sed -i .bak 's/sourceTree = "<absolute>"/sourceTree = SOURCE_ROOT/' "VGMTrans.xcodeproj/project.pbxproj"
rm VGMTrans.xcodeproj/project.pbxproj.bak

