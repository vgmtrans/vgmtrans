# qt

QT_VERSION = 5.8.0
QT_URL := http://download.qt-project.org/official_releases/qt/5.8/$(QT_VERSION)/submodules/qtbase-opensource-src-$(QT_VERSION).tar.xz

PKGS += qt

ifeq ($(call need_pkg,"QtCore QtGui"),)
ifeq ($(call need_pkg,"Qt5Core Qt5Gui Qt5Widgets"),)
PKGS_FOUND += qt
endif
endif

$(TARBALLS)/qt-$(QT_VERSION).tar.xz:
	$(call download,$(QT_URL))

.sum-qt: qt-$(QT_VERSION).tar.xz

qt: qt-$(QT_VERSION).tar.xz .sum-qt
	$(UNPACK)
	mv qtbase-opensource-src-$(QT_VERSION) qt-$(QT_VERSION)
	$(MOVE)

ifdef HAVE_MACOSX
QT_PLATFORM := -platform macx-clang
EXTRA_CONFIG_OPTIONS := -sdk macosx10.12
endif
ifdef HAVE_WIN32
#QT_PLATFORM := -xplatform win32-g++ -device-option CROSS_COMPILE=$(HOST)-
QT_PLATFORM := -platform win32-g++
endif

QT_CONFIG := -static -opensource -confirm-license -no-pkg-config \
	-no-sql-sqlite -no-gif -qt-libjpeg -no-openssl -no-opengl -no-dbus \
	-no-qml-debug -no-sql-odbc -no-pch \
	-no-compile-examples -nomake examples

.qt: qt
#	cd $< && ./configure $(QT_PLATFORM) $(EXTRA_CONFIG_OPTIONS) -static -release -no-sql-sqlite -no-gif -qt-libjpeg -no-openssl -no-opengl --no-harfbuzz -opensource -confirm-license
	cd $< && ./configure $(QT_PLATFORM) $(EXTRA_CONFIG_OPTIONS) $(QT_CONFIG) -prefix $(PREFIX)
#	cd $< && $(MAKE) sub-src
    # Make && Install libraries
	cd $< && $(MAKE)
	cd $</src && $(MAKE) sub-corelib-install_subtargets sub-gui-install_subtargets sub-widgets-install_subtargets sub-platformsupport-install_subtargets
	# Install tools
	cd $</src && $(MAKE) sub-moc-install_subtargets sub-rcc-install_subtargets sub-uic-install_subtargets

    # Install plugins
	cd $</src/plugins && $(MAKE) sub-platforms-install_subtargets

	# INSTALLING LIBRARIES
	for lib in Widgets Gui Core; \
		do install $</lib/libQt5$${lib}.a "$(PREFIX)/lib/libQt5$${lib}.a"; \
	done
	# INSTALLING PLUGINS
	cp $</plugins/platforms/*.a "$(PREFIX)/lib"

	# Clean Qt mess
	rm -rf $(PREFIX)/lib/libQt5Bootstrap* $(PREFIX)/lib/*.prl $(PREFIX)/mkspecs

	# Fix .pc files to remove debug version (d)
	cd $(PREFIX)/lib/pkgconfig; for i in Qt5Core.pc Qt5Gui.pc Qt5Widgets.pc; do sed -i -e 's/d\.a/.a/g' -e 's/d $$/ /' $$i; done

ifdef HAVE_MACOSX
	# Fix Qt5Gui.pc file to include qcocoa and Qt5Platform Support
	cd $(PREFIX)/lib/pkgconfig; sed -i -e 's/ -lQt5Gui/ -lqcocoa -lQt5PlatformSupport -lQt5Gui/g' Qt5Gui.pc


#	cp $</lib/libQt5PlatformSupport.a "$(PREFIX)/lib"
	cp $</lib/libQt5PrintSupport.a "$(PREFIX)/lib"
endif
ifdef HAVE_WIN32
	# Fix Qt5Gui.pc file to include qwindows (QWindowsIntegrationPlugin) and Qt5Platform Support
	cd $(PREFIX)/lib/pkgconfig; sed -i -e 's/ -lQt5Gui/ -lqwindows -lQt5PlatformSupport -lQt5Gui/g' Qt5Gui.pc

	cp $</lib/libqtmain.a "$(PREFIX)/lib"
endif
	#install $</plugins/platforms/libqwindows.a "$(PREFIX)/lib/libqwindows.a"
	#install -d $</plugins/accessible/libqtaccessiblewidgets.a "$(PREFIX)/lib/libqtaccessiblewidgets.a"
	# INSTALLING HEADERS
	for h in corelib gui widgets; do \
		install -d "$(PREFIX)/include/src/$${h}" ; \
		(cd $</src/$${h} && find . -type f -name '*.h' -exec sh -c 'install -d "$(PREFIX)/include/src/$${0}/$$(dirname {})"; cp "{}" "$(PREFIX)/include/src/$${0}/{}"' $${h} \;) ; \
	done
	# (cd $</src/$${h} && find . -type f -name '*.h' -exec install -D "{}" "$(PREFIX)/include/src/$${h}/{}" \;) ; \

	for h in Core Gui Widgets; do \
		install -d "$(PREFIX)/include/qt5/Qt$${h}" ; \
		(cd $</include/Qt$${h} && find . -maxdepth 1 -type f \( -name '*.h' -o -name 'Q*' \) -exec install "{}" "$(PREFIX)/include/qt5/Qt$${h}/{}" \;) ; \
	done
	mkdir -p "$(PREFIX)/include/qt5/qpa"
	echo "#include \"../src/gui/kernel/qplatformnativeinterface.h\"" > "$(PREFIX)/include/qt5/qpa/qplatformnativeinterface.h"
	# INSTALLING PKGCONFIG FILES
	install -d "$(PREFIX)/lib/pkgconfig"
	for i in Core Gui Widgets; \
		do cat $(SRC)/qt/Qt5$${i}.pc.in | sed -e s/@@VERSION@@/$(QT_VERSION)/ | sed -e 's|@@PREFIX@@|$(PREFIX)|' > "$(PREFIX)/lib/pkgconfig/Qt5$${i}.pc"; \
	done

	install -d "$(PREFIX)/lib/cmake"
	for cmake in Qt5 Qt5Core Qt5Gui Qt5Widgets; do \
		install -d "$(PREFIX)/lib/cmake/$${cmake}" ; \
		(cd $</lib/cmake/$${cmake} && find . -type f -name '*.cmake' -exec sh -c 'sed -e s/plugins\\/\$${PLUGIN_LOCATION}/lib/ "{}" \
		 | sed -e s/_qt5_corelib_extra_includes.*mkspecs/_qt5_corelib_extra_includes\ \"\$${_qt5Core_install_prefix}\\/lib\\/mkspecs/ \
		 | sed -e s/install_prefix}\\/include/install_prefix}\\/include\\/qt5/g > "$(PREFIX)/lib/cmake/$${0}/{}"' $${cmake} \;) ; \
	done

	# INSTALLING MKSPECS FILES
	cp -r $</mkspecs "$(PREFIX)/lib/mkspecs"

	# BUILDING QT BUILD TOOLS
ifdef HAVE_CROSS_COMPILE
	cd $</include/QtCore; ln -sf $(QT_VERSION)/QtCore/private
	cd $</src/tools; $(MAKE) clean; \
		for i in bootstrap uic rcc moc; \
			do (cd $$i; echo $i && ../../../bin/qmake -spec win32-g++ ; $(MAKE) clean; $(MAKE)); \
		done
endif
	# INSTALLING QT BUILD TOOLS
	install -d "$(PREFIX)/bin/"
	for i in rcc moc uic qmake; \
		do cp $</bin/$$i* "$(PREFIX)/bin"; \
	done
	touch $@
