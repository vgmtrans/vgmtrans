# qt

QT_VERSION = 5.5.1
QT_URL := http://download.qt-project.org/official_releases/qt/5.5/$(QT_VERSION)/submodules/qtbase-opensource-src-$(QT_VERSION).tar.xz

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
EXTRA_CONFIG_OPTIONS := -sdk macosx10.11
endif
ifdef HAVE_WIN32
#QT_PLATFORM := -xplatform win32-g++ -device-option CROSS_COMPILE=$(HOST)-
QT_PLATFORM := -platform win32-g++
endif

.qt: qt
	cd $< && ./configure $(QT_PLATFORM) $(EXTRA_CONFIG_OPTIONS) -static -release -no-sql-sqlite -no-gif -qt-libjpeg -no-openssl -no-opengl --no-harfbuzz -opensource -confirm-license
	cd $< && $(MAKE) sub-src
	# INSTALLING LIBRARIES
	for lib in Widgets Gui Core; \
		do install $</lib/libQt5$${lib}.a "$(PREFIX)/lib/libQt5$${lib}.a"; \
	done
	# INSTALLING PLUGINS
	cp $</plugins/platforms/*.a "$(PREFIX)/lib"
ifdef HAVE_MACOSX
	cp $</lib/libQt5PlatformSupport.a "$(PREFIX)/lib"
	cp $</lib/libQt5PrintSupport.a "$(PREFIX)/lib"
endif
ifdef HAVE_WIN32
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
