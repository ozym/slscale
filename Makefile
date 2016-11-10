
# Build environment can be configured the following
# environment variables:
#   CC : Specify the C compiler to use
#   CFLAGS : Specify compiler options to use
#

CFLAGS += -I. -DPACKAGE_VERSION=\"1.0.1\"

LDFLAGS =
LDLIBS = -ldali -lslink -lmseed -lm

all: slscale msscale

slscale: slscale.o
	$(CC) $(CFLAGS) -o $@ slscale.o $(LDLIBS)

msscale: msscale.o
	$(CC) $(CFLAGS) -o $@ msscale.o $(LDLIBS)

clean:
	rm -f slscale.o slscale msscale.o msscale

# Implicit rule for building object files
%.o: %.c
	$(CC) $(CFLAGS) -c $<

install:
	@echo
	@echo "No install target, copy the executable(s) yourself"
	@echo
