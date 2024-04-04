export CFLAGS+="-I$(CURDIR)/include"
export KCFLAGS=$(CFLAGS)

.phony: all
all:
	$(MAKE) -C driver all
	$(MAKE) -C user userapp

.phony: clean
clean:
	$(MAKE) -C driver clean
	$(MAKE) -C user clean

run: all
	./load_driver.sh
	./run_userapp.sh