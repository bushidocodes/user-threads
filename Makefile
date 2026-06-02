CC     = gcc
CFLAGS = -Wall -Wextra -g

TARGETS = gwthd_lvl0 gwthd_lvl1 gwthd_lvl2 gwthd_lvl3 \
          gwthd_lvl4 gwthd_lvl5 gwthd_lvl6 gwthd_lvl7

.PHONY: all clean run

all: $(TARGETS)

$(TARGETS): %: %.c gwthd.h
	$(CC) $(CFLAGS) -o $@ $<

run: all
	@for bin in $(TARGETS); do \
		echo "=== $$bin ==="; \
		./$$bin; \
		echo; \
	done

clean:
	rm -f $(TARGETS) $(addsuffix .exe, $(TARGETS))
