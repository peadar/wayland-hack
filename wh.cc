#include <iostream>
#include <sys/poll.h>
#include <vector>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <wayland-client.h>
#include <wayland-client-protocol.h>
#include <wayland-egl.h>
#include <sys/mman.h>
#include <EGL/egl.h>

wl_shell *shell;
wl_compositor *compositor;
wl_shm *shm;
wl_shell_surface *shell_surface;
wl_surface *surface;
wl_buffer *buffer;
wl_callback *frame_callback;


void *mem;
size_t memSize;

const int WIDTH=480;
const int HEIGHT=400;

void redraw()
{
   memset(mem, 0, memSize);
   static int step = 0;

   step = step + 1 % WIDTH;
   for (auto i = 0; i < memSize; i += step * 4) {
      ((char *)mem)[i] = 0xff;
      ((char *)mem)[i+1] = 0xee;
      ((char *)mem)[i+1] = 0xdd;
   }
}

static void frame_listener_done(void *data, struct wl_callback *wl_callback, uint32_t cbd);

wl_callback_listener frame_listener  {
   .done = frame_listener_done
};

void update()
{
   wl_surface_attach(surface, buffer, 0, 0);
   if (mem) {
      wl_surface_damage(surface, 0, 0, WIDTH, HEIGHT);
      redraw();
      wl_surface_commit(surface);
   }
}

void frame_listener_done(void *data, struct wl_callback *wl_callback, uint32_t cbd)
{
   wl_callback_destroy(frame_callback);
   frame_callback = wl_surface_frame(surface);
   wl_callback_add_listener(frame_callback, &frame_listener, 0);
   update();
}


struct wl_shm_pool  *
getpool(int size)
{
   auto fd = open("/home/peadar/xxxx", O_RDWR|O_CREAT, 0666);
   assert(fd != -1);
   memSize = size + getpagesize() - 1;
   memSize -= memSize % getpagesize();
   int rc = ftruncate(fd, size);
   assert(rc != -1);
   mem = mmap(0, memSize, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);

   auto pool = wl_shm_create_pool(shm, fd, size);
   std::clog << "created a pool " << pool << " of size " << memSize << "\n";
   return pool;
}

wl_buffer *
getbuf(int width, int height, int pixelSize)
{
   auto size = width * height * pixelSize;
   auto pool = getpool(size);
   auto buf = wl_shm_pool_create_buffer(pool, 0, width, height, pixelSize * width, WL_SHM_FORMAT_XRGB8888);
   wl_shm_pool_destroy(pool);
   assert(buf);
   std::clog << "created a buffer " << buffer << " of size " << size << "\n";
   return buf;
}

void
shm_format(void *data, struct wl_shm *shm, uint32_t format)
{
   std::clog << "shm " << shm << ": format " << format << "\n";

   if (format == WL_SHM_FORMAT_XRGB8888) {
      buffer = getbuf(WIDTH, HEIGHT, 4);
      wl_surface_attach(surface, buffer, 0, 0);
      wl_surface_commit(surface);
   }
}

wl_shm_listener shm_listener = {
   .format = shm_format
};

void shell_surface_ping(void *data,
		     struct wl_shell_surface *surface,
		     uint32_t serial)
{
   wl_shell_surface_pong(surface, serial);
}

void shell_surface_configure(void *data,
                 struct wl_shell_surface *wl_shell_surface,
                 uint32_t edges,
                 int32_t width,
                 int32_t height)

{
   std::cout << "resize: edges: " << edges << ", size: " << width << "x" << height << "\n";
}

void shell_surface_popup_done(void *data,
                  struct wl_shell_surface *wl_shell_surface)
{
}

wl_shell_surface_listener shell_surface_listener = {
   .ping = shell_surface_ping,
   .configure = shell_surface_configure,
   .popup_done = shell_surface_popup_done

};

void announce_global(void *v, wl_registry *registry, uint32_t name, const char *iface, uint32_t version)
{
   std::clog << "announce: global object " << name << " with interface " << iface << ", version " << version << "\n";
   if (strcmp(iface , "wl_compositor") == 0)
      compositor = (wl_compositor *)wl_registry_bind(registry, name, &wl_compositor_interface, 1);
   else if (strcmp(iface , "wl_shell") == 0)
      shell = (wl_shell *)wl_registry_bind(registry, name, &wl_shell_interface, 1);
   else if (strcmp(iface , "wl_shm") == 0) {
      shm = (wl_shm *)wl_registry_bind(registry, name, &wl_shm_interface, 1);
      wl_shm_add_listener(shm, &shm_listener, shm);
   }

   if (shell && compositor && !shell_surface) {
      surface = wl_compositor_create_surface(compositor);
      shell_surface = wl_shell_get_shell_surface(shell, surface);
      wl_shell_surface_set_toplevel(shell_surface);
      wl_shell_surface_add_listener(shell_surface, &shell_surface_listener, 0);
      //frame_callback = wl_surface_frame(surface);
      //wl_callback_add_listener(frame_callback, &frame_listener, 0);
      std::clog << "created shell surface " << shell_surface << " from surface " << surface << std::endl;
   }
}

void withdraw_global(void *v, wl_registry *registry, uint32_t name)
{
   std::clog << "withdraw: global object " << name << "\n";
}

static constexpr wl_registry_listener registry_listener = {
   announce_global, withdraw_global
};

void
init_egl(EGLNativeDisplayType display)
{

   static const EGLint config_attribs[] = {
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_RED_SIZE, 8, 
      EGL_GREEN_SIZE, 8, 
      EGL_BLUE_SIZE, 8, 
      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, 
      EGL_NONE
   };

   static const EGLint context_attribs[] = {
      EGL_CONTEXT_CLIENT_VERSION, 2, 
      EGL_NONE
   };
   auto egl_display = eglGetDisplay(display);
   if (egl_display == EGL_NO_DISPLAY) {
      std::cout << "can't open EGL display" << std::endl;
      return;
   }
   if (!eglInitialize(egl_display, 0, 0)) {
      std::cout << "can't init EGL display " << egl_display << std::endl;
      return;
   }

   std::cout << "created EGL display " << egl_display << std::endl;
   EGLint maj, min, cnt, n;
   eglGetConfigs(egl_display, 0, 0, &cnt);
   std::vector<EGLConfig> configs;
   configs.resize(cnt);
   eglChooseConfig(egl_display, config_attribs, &configs[0], cnt, &cnt);

   for (auto &conf : configs) {
      EGLint bufferSize, redSize;
      eglGetConfigAttrib(egl_display, conf, EGL_BUFFER_SIZE, &bufferSize);
      eglGetConfigAttrib(egl_display, conf, EGL_RED_SIZE, &redSize);
      std::cout << "config: buffer size=" << bufferSize << ", colorsize=" << redSize << "\n";
   }

}

int
main(int argc, char *argv[])
{
   auto display = wl_display_connect(nullptr);
   auto registry = wl_display_get_registry(display);
   wl_registry_add_listener(registry, &registry_listener, nullptr);
   init_egl(display);


   struct pollfd pfds[10];

   pfds[0].fd = wl_display_get_fd(display);
   pfds[0].events = POLLIN;
   pfds[0].revents = 0;

   for (;;) {
      while (wl_display_dispatch_pending(display) > 0)
         ;
      wl_display_flush(display);
      int rc = poll(pfds, 1, 1000 );
      if (rc > 0)
         if (pfds[0].revents & POLLIN) {
            if (wl_display_prepare_read(display) == 0) {
               std::clog << "reading events {";
               wl_display_read_events(display);
               std::clog << "}\n";
            }
         }
         update();
   }
   wl_display_disconnect(display);
}
