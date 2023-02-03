CFLAGS	= -g -Wall

UTIL	= utoken-decrypt
SRCS	= main.c \
	  descriptor.c \
	  usb.c \
	  ccid.c \
	  reader.c \
	  scard.c \
	  yubikey.c \
	  bufparser.c \
	  util.c
OBJS	= $(SRCS:.c=.o)

all: $(UTIL)

$(UTIL): $(OBJS)
	$(CC) -o $@ $(OBJS)

clean:
	rm -f $(UTIL) $(OBJS)
