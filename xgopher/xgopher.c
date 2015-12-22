#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xlocale.h>
#include <X11/extensions/shape.h>
#include <X11/xpm.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <memory.h>
#include <signal.h>
#include <stdint.h>
#include <time.h>
#include <jansson.h>

#include "out01.xpm"
#include "out02.xpm"
#include "out03.xpm"
#include "waiting.xpm"

#define _NET_WM_STATE_REMOVE        0    /* remove/unset property */
#define _NET_WM_STATE_ADD           1    /* add/set property */

#define MAX_PROP_WORDS 100000

typedef struct _MSG {
  char* method;
  char* content;
  char* link;
  struct _MSG* next;
} MSG;

typedef struct _ImageSprite {
  XImage *body;
  XImage *mask;
} ImageSprite;

typedef struct _PixmapSprite {
  Pixmap body;
  Pixmap mask;
  int width;
  int height;
} PixmapSprite;

static ImageSprite *
new_image_sprite(void)
{
  ImageSprite *ims = malloc(sizeof (ImageSprite));

  if (!ims)
    return NULL;

  ims->body = NULL;
  ims->mask = NULL;

  return ims;
}

static PixmapSprite *
new_pixmap_sprite(void)
{
  PixmapSprite *pms = malloc(sizeof (PixmapSprite));

  if (!pms)
    return NULL;

  pms->body = None;
  pms->mask = None;

  return pms;
}

static int
is_image_sprite_valid(const ImageSprite *ims)
{
  return ims && ims->body && ims->mask;
}

static int
is_pixmap_sprite_valid(const PixmapSprite *pms)
{
  return pms && pms->body && pms->mask;
}

static ImageSprite *
create_image_sprite_from_data(Display *dpy, char **data)
{
  ImageSprite *ims = new_image_sprite();

  if (!ims)
    return NULL;

  if (XpmCreateImageFromData(dpy, data, &ims->body, &ims->mask, NULL)) {
    free(ims);
    return NULL;
  }
  assert(ims->body->width == ims->mask->width && ims->body->height == ims->mask->height);

  return ims;
}

static void
destroy_image_sprite(ImageSprite *ims)
{
  if (ims) {
    if (ims->body)
      XDestroyImage(ims->body);
    if (ims->mask)
      XDestroyImage(ims->mask);
    free(ims);
  }
}

static void
destroy_pixmap_sprite(Display *dpy, PixmapSprite *pms)
{
  if (pms) {
    if (pms->body)
      XFreePixmap(dpy, pms->body);
    if (pms->mask)
      XFreePixmap(dpy, pms->mask);
    free(pms);
  }
}

static ImageSprite *
create_horizontal_reverse_image_sprite(const ImageSprite *src)
{
  ImageSprite *dst;
  int x, y, w, h;

  if (!is_image_sprite_valid(src))
    return NULL;

  assert(src->body->width == src->mask->width && src->body->height == src->mask->height);

  w = src->body->width;
  h = src->body->height;

  dst = new_image_sprite();
  if (!dst)
    return NULL;

  /* XXX: unnecessary copy */
  dst->body = XSubImage(src->body, 0, 0, w, h);
  dst->mask = XSubImage(src->mask, 0, 0, w, h);
  if (!is_image_sprite_valid(dst)) {
    destroy_image_sprite(dst);
    return NULL;
  }

  /* XXX: slow */
  for (y=0; y<h; y++) {
    for (x=0; x<w; x++) {
      XPutPixel(dst->body, w-x-1, y, XGetPixel(src->body, x, y));
      XPutPixel(dst->mask, w-x-1, y, XGetPixel(src->mask, x, y));
    }
  }

  return dst;
}

static PixmapSprite *
convert_image_sprite_to_pixmap_sprite(Display *dpy, const ImageSprite *src)
{
  PixmapSprite *dst;
  int w, h;
  GC gc;

  if (!is_image_sprite_valid(src))
    return NULL;

  assert(src->body->width == src->mask->width && src->body->height == src->mask->height);

  dst = new_pixmap_sprite();
  if (!dst)
    return NULL;

  w = dst->width = src->body->width;
  h = dst->height = src->body->height;

  dst->body = XCreatePixmap(dpy, DefaultRootWindow(dpy), w, h, DefaultDepth(dpy, DefaultScreen(dpy)));
  dst->mask = XCreatePixmap(dpy, DefaultRootWindow(dpy), w, h, 1);
  if (!is_pixmap_sprite_valid(dst)) {
    destroy_pixmap_sprite(dpy, dst);
    return NULL;
  }

  gc = XCreateGC(dpy, dst->mask, 0, 0);

  XPutImage(dpy, dst->body, DefaultGC(dpy, DefaultScreen(dpy)), src->body, 0, 0, 0, 0, w, h);
  XPutImage(dpy, dst->mask, gc, src->mask, 0, 0, 0, 0, w, h);

  XFreeGC(dpy, gc);

  return dst;
}

static void
put_pixmap_sprite_body_to_window(Display *dpy, Window win, const PixmapSprite *pms)
{
  XCopyArea(dpy, pms->body, win, DefaultGC(dpy, DefaultScreen(dpy)), 0, 0, pms->width, pms->height, 0, 0);
}

static void
set_pixmap_sprite_mask_to_window(Display *dpy, Window win, const PixmapSprite *pms)
{
  XShapeCombineMask(dpy, win, ShapeBounding, 0, 0, pms->mask, ShapeSet);

}

static void
x11_set_property(Display *dpy, Window win, char *atom, int state) {
  XEvent xev;
  int set = _NET_WM_STATE_ADD;
  Atom type, property;

  if (state == 0) set = _NET_WM_STATE_REMOVE;
  type = XInternAtom(dpy, "_NET_WM_STATE", 0);
  property = XInternAtom(dpy, atom, 0);
  xev.type = ClientMessage;
  xev.xclient.type = ClientMessage;
  xev.xclient.window = win;
  xev.xclient.message_type = type;
  xev.xclient.format = 32;
  xev.xclient.data.l[0] = set;
  xev.xclient.data.l[1] = property;
  xev.xclient.data.l[2] = 0;
  XSendEvent(dpy, DefaultRootWindow(dpy), 0, SubstructureNotifyMask, &xev);
}

static void
x11_set_window_type(Display *dpy, Window win, char* type) {
  Atom _NET_WM_WINDOW_TYPE = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", 0);
  Atom _NET_WM_WINDOW_TYPE_DOCK = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", 0);
  XChangeProperty(dpy, win, _NET_WM_WINDOW_TYPE, XA_ATOM, 32, PropModeReplace,
      (unsigned char *)&_NET_WM_WINDOW_TYPE_DOCK, 1);
}

static int
x11_moveresize_window(Display *dpy, Window win, int x, int y, int width, int height) {
  XEvent xevent;
  static Atom moveresize = 0;
  if (!moveresize) {
    moveresize = XInternAtom(dpy, "_NET_MOVERESIZE_WINDOW", 0);
    if (!moveresize) return -1;
  }

  xevent.type = ClientMessage;
  xevent.xclient.window = win;
  xevent.xclient.message_type = moveresize;
  xevent.xclient.format = 32;
  xevent.xclient.data.l[0] = StaticGravity | (1 << 8) | (1 << 9) | (1 << 10) | (1 << 11);
  xevent.xclient.data.l[1] = x;
  xevent.xclient.data.l[2] = y;
  xevent.xclient.data.l[3] = width;
  xevent.xclient.data.l[4] = height;
  return XSendEvent(dpy, DefaultRootWindow(dpy), 0, SubstructureRedirectMask, &xevent);
}

MSG* free_msg(MSG* top) {
  MSG* next;
  if (!top) return NULL;
  next = top->next;
  if (top->method) free(top->method);
  if (top->content) free(top->content);
  if (top->link) free(top->link);
  free(top);
  return next;
}

typedef enum {
  S_START_WALK,
  S_WALK,
  S_START_JUMP,
  S_JUMP,
  S_START_PAUSE,
  S_PAUSE
} State;

int
main() {
  Display *dpy;
  Drawable root;
  Window win;
  PixmapSprite *pmss[10];
  const PixmapSprite *curpms;
  int step = 0;
  char **xpms[] = {
    out01, out02, out03, out02, waiting
  };
  int screen, width, height;
  GC  gc;
  XFontSet fs;
  XEvent event;
  Atom gopherNotify;
  int i;
  State state = S_START_WALK;
  int x, y;
  int dx = 10, dy = 0;
  char **miss;
  char *def;
  int n_miss;
  MSG *msg = NULL;
  Bool use_shape = True;
  const struct timeval move_interval = { .tv_sec=0, .tv_usec=50000 };
  const struct timeval pause_interval = { .tv_sec=5, .tv_usec=0 };
  struct timeval now, eta, timeout;
  int fd;
  fd_set rfds;

  setlocale(LC_CTYPE,"");

  dpy = XOpenDisplay(NULL);
  if (dpy == NULL) {
    fprintf(stderr, "error: cannot connect to X server\n");
    return 1;
  }

  screen = DefaultScreen(dpy);
  root = DefaultRootWindow(dpy);

  width = DisplayWidth(dpy, screen);
  height = DisplayHeight(dpy, screen);
  win = XCreateSimpleWindow(dpy, root,
      0, 0, 200, 200, 0, BlackPixel(dpy, 0), WhitePixel(dpy, 0));
  XSetWindowBackgroundPixmap(dpy, win, None);
  gc = XCreateGC(dpy, win, 0, 0);
  //fs = XCreateFontSet(dpy, "*-iso10646-1", &miss, &n_miss, &def);
  fs = XCreateFontSet(dpy, "-*-*-*-R-Normal--14-130-75-75-*-*", &miss, &n_miss, &def);

  for (i = 0; i < 5; i++) {
    ImageSprite *ims, *rims;
    ims = create_image_sprite_from_data(dpy, xpms[i]);
    rims = create_horizontal_reverse_image_sprite(ims);
    pmss[i] = convert_image_sprite_to_pixmap_sprite(dpy, ims);
    pmss[i+5] = convert_image_sprite_to_pixmap_sprite(dpy, rims);
    destroy_image_sprite(ims);
    destroy_image_sprite(rims);
  }

  x11_set_property(dpy, win, "_NET_WM_STATE_STAYS_ON_TOP", 1);
  x11_set_property(dpy, win, "_NET_WM_STATE_ABOVE", 1);
  x11_set_property(dpy, win, "_NET_WM_STATE_SKIP_TASKBAR", 1);
  x11_set_property(dpy, win, "_NET_WM_STATE_SKIP_PAGER", 1);
  x11_set_property(dpy, win, "_NET_WM_STATE_STICKY", 1);
  x11_set_window_type(dpy, win, "_NET_WM_WINDOW_TYPE_DOCK");

  x = -200;
  y = height - 200;
  x11_moveresize_window(dpy, win, x, y, 200, 200);
  XSelectInput(dpy, win, ExposureMask | PropertyChangeMask);
  XStoreName(dpy, win, "Gopher");
  XMapWindow(dpy, win);

  srand(time(NULL));

  gopherNotify = XInternAtom(dpy, "GopherNotify", 0);

  timerclear(&timeout);
  fd = ConnectionNumber(dpy);
  while(1) {
    /* (1) timeout process */
    while (!timerisset(&timeout)) {
      switch (state) {
      case S_START_WALK:
        state = S_WALK;
        step = 0;
        dy = 0;
        y = height - 200;
        break;
      case S_WALK:
        if (msg != NULL) {
          if (strcmp(msg->method, "message") == 0) {
            /* pending dequeueing msg while pausing */
            state = S_START_PAUSE;
            break;
          } else if (strcmp(msg->method, "jump") == 0) {
            msg = free_msg(msg);
            state = S_START_JUMP;
            break;
          }
        }
        if (rand() % 40 == 0) {
          state = S_START_JUMP;
          break;
        }
        step++;
        x += dx;
        y += dy;
        curpms = pmss[(step%4)+(dx>0?0:5)];
        timeout = move_interval;
        break;
      case S_START_JUMP:
        dy = -20;
        state = S_JUMP;
        break;
      case S_JUMP:
        x += dx / 2;
        y += dy;
        dy += 2;
        if (y > height - 200) {
          state = S_START_WALK;
          break;
        }
        curpms = pmss[(step%4)+(dx>0?0:5)];
        timeout = move_interval;
        break;
      case S_START_PAUSE:
        assert(msg != NULL && strcmp(msg->method, "message") == 0);
        curpms = pmss[4+(dx>0?0:5)];
        timeout = pause_interval;
        state = S_PAUSE;
        break;
      case S_PAUSE:
        assert(msg != NULL && strcmp(msg->method, "message") == 0);
        msg = free_msg(msg);
        state = S_START_WALK;
        break;
      }
    }
    if ((dx < 0 && x < 0) || (dx > 0 && x > width - 200)) dx = -dx;
    if (use_shape)
      set_pixmap_sprite_mask_to_window(dpy, win, curpms);
    else
      XClearArea(dpy, win, 0, 0, 200, 200, True);
    x11_moveresize_window(dpy, win, x, y, 200, 200);

    /* (2) prepare to wait */
    gettimeofday(&now, NULL);
    timeradd(&now, &timeout, &eta);

    /* (3) event loop with waiting */
    while (timerisset(&timeout) || XPending(dpy) > 0) {
      XNextEvent(dpy, &event);
      switch(event.type) {
        case PropertyNotify:
          {
            unsigned char *prop = NULL;
            int result, actualFormat;
            unsigned long numItems, bytesAfter;
            Atom actualType;

            result = XGetWindowProperty(dpy, win, gopherNotify, 0L,
                (long)MAX_PROP_WORDS, 1,
                XA_STRING, &actualType,
                &actualFormat, &numItems, &bytesAfter,
                &prop);
            if (result == 0 && prop) {
              json_error_t error;
              json_t *body = json_loads((char*) prop, 0, &error);
              if (body != NULL) {
                MSG* newmsg = malloc(sizeof(MSG));;
                memset(newmsg, 0, sizeof(MSG));
                json_t *v;
                v = json_object_get(body, "method");
                if (v != NULL) newmsg->method = strdup(json_string_value(v));
                v = json_object_get(body, "content");
                if (v != NULL) newmsg->content = strdup(json_string_value(v));
                v = json_object_get(body, "link");
                if (v != NULL) newmsg->link = strdup(json_string_value(v));
                json_decref(body);
                if (msg) {
                  MSG* q = msg;
                  while (q->next) q = q->next;
                  q->next = newmsg;
                } else msg = newmsg;
                /* cancel timeout */
                gettimeofday(&eta, NULL);
              }
            }
          }
          break;
        case Expose:
          if(event.xexpose.count == 0) {
            put_pixmap_sprite_body_to_window(dpy, win, curpms);
            if (state == S_PAUSE && msg->content) {
              XSetForeground(dpy, gc, BlackPixel(dpy, 0));
              Xutf8DrawString(dpy, win, fs, gc, 20, 150, msg->content, strlen(msg->content));
            }
          }
          break;
        default:
          break;
      }
      if (XPending(dpy) == 0) {
        timerclear(&timeout);
        gettimeofday(&now, NULL);
        if (timercmp(&now, &eta, <)) {
          timersub(&eta, &now, &timeout);
          FD_ZERO(&rfds);
          FD_SET(fd, &rfds);
          i = select(fd+1, &rfds, NULL, NULL, &timeout);
          switch (i) {
          case -1:
            /* error */
            goto quit;
          case 0:
            /* timeout */
            timerclear(&timeout);
            break;
          default:
            /* incoming */
            break;
          }
        }
      }
    }
    if (t++ > 180000) t = 0;
  }
quit:
  XCloseDisplay(dpy);
  return 0;
}

// vim: set sw=2 cino=J2 et:
