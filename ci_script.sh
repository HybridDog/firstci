
download_schismtracker_and_depends() {
	# Build instructions from
	# https://github.com/schismtracker/schismtracker/blob/6e9f1207015cae0fe1b829fff7bb867e02ec6dea/docs/building_on_linux.md
	# Download SchismTracker
	git clone --depth=1 https://github.com/schismtracker/schismtracker.git
	# Install dependencies with apt
	#~ sudo apt-get install build-essential automake autoconf autoconf-archive    \
                #~ libx11-dev libxext-dev libxv-dev     \
                #~ libxxf86vm-dev libsdl1.2-dev libasound2-dev mercurial \
									#~ libtool
	sudo apt-get install build-essential automake autoconf    \
                libsdl1.2-dev
}

build_schismtracker_appimage() {
	cd schismtracker

	autoreconf -i
	mkdir -p build_appimage && cd build_appimage
	export CFLAGS="-Os -pipe"
	../configure --prefix /usr
	make -j$(nproc)

	# Install Schism Tracker into ./AppDir
	mkdir -p AppDir && make install DESTDIR=AppDir

	# Modify the .desktop file so that linuxdeploy works
	sed -i '/\[Desktop Action Render WAV\]/,$ s:^:# :' ./AppDir/usr/share/applications/schism.desktop

	# Create the AppImage with linuxdeploy
	wget https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
	chmod +x linuxdeploy-x86_64.AppImage
	export NO_APPSTREAM=1
	./linuxdeploy-x86_64.AppImage --appdir AppDir --output appimage
	# Update Information:
	# https://github.com/AppImage/AppImageSpec/blob/master/draft.md#github-releases
	# https://github.com/linuxdeploy/linuxdeploy-plugin-appimage/blob/master/README.md#optional-variables

	cd ../..
}
