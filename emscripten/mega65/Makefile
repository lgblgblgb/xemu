XEMUSRC = ../../../xemu-next
BUILTFILES = xmega65.html xmega65.data xmega65.js xmega65.wasm
ASSETS = favicon.ico index.html shell.js shell.gif shell.css

all: $(ASSETS) $(BUILTFILES)
	git status .

favicon.ico: $(XEMUSRC)/build/xemu-48x48.xpm
	convert $< $@

index.html: $(XEMUSRC)/targets/mega65/assets/shell.html
	cp $< $@

shell.%: $(XEMUSRC)/targets/mega65/assets/shell.%
	cp $< $@

$(BUILTFILES): %: $(XEMUSRC)/build/bin/%
	cp $< $@
