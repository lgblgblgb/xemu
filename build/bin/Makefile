TARGET = lgb@192.168.18.231:www-lgb/dist/

all:
	@echo "Use 'make dist' to distribute." >&2
	@false

dist:
	rsync -av --ignore-existing $(TARGET) .
	make -C ../emscripten
	{ echo "var xemufiledb = {" ; for a in * ; do if [ -s $$a ]; then stat -c '"%n": [%s, %Y],' $$a ; fi ; done ; echo "};" ; } > filedb.js
	for a in *.js *.wasm ; do if [ $$a -nt $$a.gz ] ; then echo "Compressing file $$a -> $$a.gz" ; gzip -9 < $$a > $$a.gz && touch -r $$a $$a.gz ; fi ; done
	rsync -av . $(TARGET)
	rsync -av ../xemu-dist-page.html $(TARGET)/../index.html

clean:
	rm -f *.*
