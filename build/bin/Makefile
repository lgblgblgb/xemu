#TARGET = lgb@xemu-dist.lgb.hu:www-lgb/dist/
TARGET = lgb@xemu-dist.lgb.hu:/var/www/virtual/xemu-dist.lgb.hu/dist/
SSH_COMMAND = "ssh"

all:
	@echo "Use 'make dist' to distribute." >&2
	@false

dist:
	rsync -av -e $(SSH_COMMAND) --ignore-existing --progress $(TARGET) .
	make -C ../emscripten
	{ echo "var xemufiledb = {" ; for a in * ; do if [ -s $$a ]; then stat -c '"%n": [%s, %Y],' $$a ; fi ; done ; echo "};" ; } > filedb.js
	for a in *.js *.wasm ; do if [ $$a -nt $$a.gz ] ; then echo "Compressing file $$a -> $$a.gz" ; gzip -9 < $$a > $$a.gz && touch -r $$a $$a.gz ; fi ; done
	rsync -av -e $(SSH_COMMAND) . $(TARGET)
	rsync -av -e $(SSH_COMMAND) ../xemu-dist-page.html $(TARGET)/../index.html

clean:
	rm -f *.*
