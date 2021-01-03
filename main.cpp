#include <fstream>
#include <fftw3.h>
#include <math.h>
#include <SFML/Graphics.hpp>
#include <X11/Xlib.h>
#include "util.cpp"
#include "input/pulse.h"
#include "input/pulse.cpp"

float fps = 50;
int maxHeight = 256;
int windowWidth = 1280;
float bars[50];
std::string brp = "/sys/devices/platform/dell-laptop/leds/dell::kbd_backlight/brightness";
std::string brm = "/sys/devices/platform/dell-laptop/leds/dell::kbd_backlight/max_brightness";

Window TransparentWindow () {
  Display* display = XOpenDisplay(NULL);
  XVisualInfo visualinfo;
  XMatchVisualInfo(display, DefaultScreen(display), 32, TrueColor, &visualinfo);

  // Create window
  Window wnd;
  GC gc;
  XSetWindowAttributes attr;
  attr.colormap = XCreateColormap(display, DefaultRootWindow(display), visualinfo.visual, AllocNone);
  attr.event_mask = ExposureMask | KeyPressMask;
  attr.background_pixmap = None;
  attr.border_pixel = 0;
  attr.override_redirect = true;
  wnd = XCreateWindow(
    display, DefaultRootWindow(display),
    (sf::VideoMode::getDesktopMode().width / 2) - (windowWidth / 2),
    sf::VideoMode::getDesktopMode().height - (maxHeight * 2),
    windowWidth,
    maxHeight * 2,
    0,
    visualinfo.depth,
    InputOutput,
    visualinfo.visual,
    CWColormap|CWEventMask|CWBackPixmap|CWBorderPixel,
    &attr
  );
  gc = XCreateGC(display, wnd, 0, 0);
  XStoreName(display, wnd, "Visualizer");

  XSizeHints sizehints;
  sizehints.flags = PPosition | PSize;
  sizehints.x = (sf::VideoMode::getDesktopMode().width / 2) - (windowWidth / 2);
  sizehints.y = sf::VideoMode::getDesktopMode().height - (maxHeight * 2);
  sizehints.width = windowWidth;
  sizehints.height = maxHeight * 2;
  XSetWMNormalHints(display, wnd, &sizehints);

  XSetWMProtocols(display, wnd, NULL, 0);

  x11_window_set_desktop(display, wnd);
  x11_window_set_borderless(display, wnd);
  x11_window_set_below(display, wnd);
  x11_window_set_sticky(display, wnd);
  x11_window_set_skip_taskbar(display, wnd);
  x11_window_set_skip_pager(display, wnd);

  return wnd;
}
#undef None

void draw(sf::RenderWindow* window) {
  int i;

  sf::Vector2u s = window->getSize();
  window->clear(sf::Color::Transparent);

  for (i = 0; i < 50; i++) {
    float bar = bars[i];
    float width = (float)s.x / (float)50;
    float height = bar * maxHeight;
    float posY = (s.y / 2) - (height / 2);

    sf::RectangleShape rect(sf::Vector2f(width, height));
    rect.setPosition(sf::Vector2f(width * i, posY));
    rect.setFillColor(sf::Color(0, 160, 160, 255 * bar));

    window->draw(rect);
  }

  window->display();
  // std::cout << "FPS: " + std::to_string(fps) << std::endl;
}

int main () {
  Window win = TransparentWindow();
  sf::RenderWindow window(win);
  window.setFramerateLimit(fps);

  sf::Clock clock;
  pthread_t p_thread;
  int i, thr_id, silence, sleep, highest, kbmax;
  struct timespec req = { .tv_sec = 0, .tv_nsec = 0 };
  struct audio_data audio;
  double in[2050];
  fftw_complex out[1025][2];
  fftw_plan p = fftw_plan_dft_r2c_1d(2048, in, *out, FFTW_MEASURE);
  int *freq;
  std::ifstream maxBrightness;
  FILE *f;

  // Initialization
  for (i = 0; i < 50; i++) {
    bars[i] = 0;
  }

  audio.source = (char*)"auto";
  audio.format = -1;
  audio.terminate = 0;
  for (i = 0; i < 2048; i++) {
    audio.audio_out[i] = 0;
  }
  getPulseDefaultSink((void*)&audio);
  thr_id = pthread_create(&p_thread, NULL, input_pulse, (void*)&audio);
  audio.rate = 44100;

  // get maximum brightness available
  maxBrightness.open(brm);
  if (maxBrightness.is_open()) {
    std::string t;
    getline(maxBrightness, t);
    kbmax = std::stoi(t);
    maxBrightness.close();
  }

  // this is where we send the brightness
  if (kbmax > 0) {
    f = fopen(brp.c_str(), "w");
    setbuf(f, NULL);
  }

  // Main Loop
  while (window.isOpen()) {
    // Handle Events
    sf::Event event;
    while (window.pollEvent(event)) {
      if (event.type == sf::Event::Closed) window.close();
    }

    // Highest bar among all of them
    highest = 0;

    // Copy Audio Data
    silence = 1;
    for (i = 0; i < 2050; i++) {
      if (i < 2048) {
        in[i] = audio.audio_out[i];
        if (in[i]) silence = 0;
      } else {
        in[i] = 0;
      }
    }

    // Check if there was silence
    if (silence == 1) sleep++;
    else sleep = 0;

    if (sleep > fps * 5) {
      // i sleep
      req.tv_sec = 1;
      req.tv_nsec = 0;
      nanosleep(&req, NULL);
      continue;
    }

    // real shit??
    fftw_execute(p);

    for (i = 0; i < 50; i++) {
      int ab = pow(pow(*out[i * 2][0], 2) + pow(*out[i * 2][1], 2), 0.5);
      ab = ab + (ab * i / 30);
      float bar = (float)ab / (float)3500000;
      if (bar > 1) bar = 1;
      if (bar > bars[i]) {
        bars[i] = bar;
      } else {
        bars[i] -= 1 / fps;
      }
      if (bars[i] < 0) bars[i] = 0;
      int high = static_cast<int>(bars[i] * kbmax + 0.5);
      if (high > highest) highest = high;
    }

    // Render
    draw(&window);
    fps = 1 / clock.restart().asSeconds();
    if (f && kbmax > 0) {
      char const *hh = std::to_string(highest).c_str();
      fputs(hh, f);
    }
  }

  // Free resources
  audio.terminate = 1;
  pthread_join(p_thread, NULL);
  free(audio.source);

  return 0;
}
