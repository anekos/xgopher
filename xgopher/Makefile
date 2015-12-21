SRCS = \
	xgopher.c

OBJS = $(SRCS:.c=.o)

X11INCDIR = /usr/include
X11LIBDIR = /usr/lib
JANSSONINCDIR = /usr/include
JANSSONLIBDIR = /usr/lib

CFLAGS = -I$(X11INCDIR) -I$(JANSSONINCDIR) -Wall
LDFLAGS = -L$(X11LIBDIR) -L$(JANSSONLIBDIR) -R$(X11LIBDIR) -R$(JANSSONLIBDIR)
LIBS = -lX11 -lXext -lXpm -ljansson
TARGET = xgopher

all : $(TARGET)

$(TARGET) : $(OBJS)
	gcc -g -o $@ $(LDFLAGS) $(OBJS) $(LIBS)

.c.o :
	gcc -g -c $(CFLAGS) -I. $< -o $@

clean :
	rm -f *.o $(TARGET)
