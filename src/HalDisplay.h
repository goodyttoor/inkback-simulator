#pragma once
#include <Arduino.h>
#include <EInkDisplay.h>

class HalDisplay {
public:
  // Constructor with pin configuration
  HalDisplay();

  // Destructor
  ~HalDisplay();

  // Refresh modes
  enum RefreshMode {
    FULL_REFRESH, // Full refresh with complete waveform
    HALF_REFRESH, // Half refresh (1720ms) - balanced quality and speed
    FAST_REFRESH  // Fast refresh using custom LUT
  };

  // Initialize the display hardware and driver
  void begin();
  void begin(bool seamless);

  // Display dimensions
  static constexpr uint16_t DISPLAY_WIDTH = EInkDisplay::DISPLAY_WIDTH;
  static constexpr uint16_t DISPLAY_HEIGHT = EInkDisplay::DISPLAY_HEIGHT;
  static constexpr uint16_t DISPLAY_WIDTH_BYTES = DISPLAY_WIDTH / 8;
  static constexpr uint32_t BUFFER_SIZE = DISPLAY_WIDTH_BYTES * DISPLAY_HEIGHT;

  // Frame buffer operations
  void clearScreen(uint8_t color = 0xFF) const;
  void drawImage(const uint8_t *imageData, uint16_t x, uint16_t y, uint16_t w,
                 uint16_t h, bool fromProgmem = false) const;
  void drawImageTransparent(const uint8_t *imageData, uint16_t x, uint16_t y,
                            uint16_t w, uint16_t h,
                            bool fromProgmem = false) const;

  // Persistent black/white polarity used by the X4 Pro frontlight panel.
  void setInverted(bool inverted);
  bool toggleInverted();
  bool isInverted() const;

  void displayBuffer(RefreshMode mode = RefreshMode::FAST_REFRESH,
                     bool turnOffScreen = false);
  void displayBufferAsync(RefreshMode mode = RefreshMode::FAST_REFRESH);
  void waitRefreshComplete();
  bool supportsAsyncRefresh() const;
  void displayWindow(int x, int y, int w, int h);
  // Whether the SIMULATED panel honours a windowed refresh. On real hardware
  // this is a property of the controller — SSD1677 implements one, UC8179 and
  // UC8279 repaint the whole panel — so it is a device trait worth modelling
  // rather than a fact about the simulator. Set CROSSPOINT_SIM_WINDOW_REFRESH=0
  // to model a panel without one.
  // Whether the panel folds the grayscale base into the gray activation
  // (SSD1683). The simulator composites in software and has no such staging, so
  // the reader takes its ordinary AA path — false is the accurate answer here,
  // not a stub.
  bool combinesGrayscaleBase() const { return false; }
  bool supportsWindowedRefresh() const;
  void refreshDisplay(RefreshMode mode = RefreshMode::FAST_REFRESH,
                      bool turnOffScreen = false);

  // Power management
  void deepSleep();

  // Access to frame buffer
  uint8_t *getFrameBuffer() const;
  uint8_t *lendFrameBufferStorage(uint32_t *sizeOut);
  void returnFrameBufferStorage();

  // Runtime geometry passthrough
  uint16_t getDisplayWidth() const;
  uint16_t getDisplayHeight() const;
  uint16_t getDisplayWidthBytes() const;
  uint32_t getBufferSize() const;

  void displayGrayscaleBase(RefreshMode fallback = HALF_REFRESH,
                            bool turnOffScreen = false);
  void preconditionGrayscale();
  void preconditionGrayscale(uint16_t x, uint16_t y, uint16_t w, uint16_t h);

  void copyGrayscaleBuffers(const uint8_t *lsbBuffer, const uint8_t *msbBuffer);
  void copyGrayscaleLsbBuffers(const uint8_t *lsbBuffer);
  void copyGrayscaleMsbBuffers(const uint8_t *msbBuffer);
  void cleanupGrayscaleBuffers(const uint8_t *bwBuffer);

  void displayGrayBuffer(bool turnOffScreen = false,
                         const unsigned char *lut = nullptr,
                         bool factoryMode = false);

  // The simulator intentionally advertises strip grayscale support so host
  // builds exercise the same low-memory path as the device firmware, and so
  // streamed plane data can feed the same grayscale preview compositor as the
  // legacy full-frame API.
  void writeGrayscalePlaneStrip(bool lsbPlane, const uint8_t *rows,
                                uint16_t yStart, uint16_t numRows);
  bool supportsStripGrayscale() const;

  // Simulator only: call from main thread to push rendered pixels to SDL.
  void presentIfNeeded();
  // Simulator only: returns true once a hard shutdown has been requested.
  bool shouldQuit() const;

private:
  EInkDisplay einkDisplay;
  bool inverted = false;

  // E-ink BUSY-wait slice hook, added for X4 Pro Beta 20.
  //
  // On device the driver calls this repeatedly while polling the panel's BUSY
  // pin, letting the power manager light-sleep through a 0.3-2 s wait. ACCEPTED
  // AND DISCARDED here: the simulator has no BUSY pin and its "refresh" returns
  // immediately, so a hook would never be called. Stored nowhere on purpose —
  // keeping the pointer would imply it might fire.
  void setBusyWaitSliceHook(bool (*)(int8_t busyPin, uint8_t busyLevel)) {}

};

extern HalDisplay display;
