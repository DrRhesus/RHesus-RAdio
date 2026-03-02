// -------------------------------------------------------------------
// Rhesus WebRadio v2 for cardputer ESP32S3
// -------------------------------------------------------------------
#include <M5Cardputer.h>
#include <M5Unified.h>
#include <Preferences.h>
#include "CardWifiSetup.h"
#include <AudioOutput.h>
#include <AudioFileSourceICYStream.h>
#include <AudioFileSource.h>
#include <AudioFileSourceBuffer.h>
#include <AudioGeneratorMP3.h>
#include <AudioGeneratorAAC.h>  
#include <HTTPClient.h>
#include <AudioOutputI2S.h>
#include <SPI.h>
#include <SD.h>

#define PIN_LED    21
#define NUM_LEDS   1

// Pines SD M5Cardputer
#define SD_SPI_SCK_PIN  40
#define SD_SPI_MISO_PIN 39
#define SD_SPI_MOSI_PIN 14
#define SD_SPI_CS_PIN   12

// Modos de visualización
enum VisualMode {
  MODE_BARS,
  MODE_SPECTRUM,
  MODE_BOTH,
  MODE_COUNT
};

int PreviousStation = 0;
int previousVolume = 0;
VisualMode currentVisualMode = MODE_SPECTRUM;

/// set M5Speaker virtual channel
static constexpr uint8_t m5spk_virtual_channel = 0;

// -------------------------------------------------------------------
// Estaciones dinámicas 
// -------------------------------------------------------------------
#define MAX_STATIONS 50
static char sd_station_names[MAX_STATIONS][64];
static char sd_station_urls[MAX_STATIONS][256];
static const char* station_list[MAX_STATIONS][2];
static size_t stations = 0;

static constexpr const char* default_station_list[][2] = {
  {"JAZZ GROOVE"              , "http://audio-edge-5bkfj.fra.h.radiomast.io/8a384ff3-6fd1-4e5d-b47d-0cbefeffe8d7"},
  {"Shaq Fu Radio"            , "http://ais-edge148-pit01.cdnstream.com/2092_128.mp3"},
  {"Japan Only Hits"          , "http://ais-edge91-dal03.cdnstream.com/2629_128.mp3"},
  {"Crucial Velocity Radio"   , "http://ais-edge94-nyc04.cdnstream.com/1369_128"},
  {"Opera Katolikus Radio"    , "http://www.katolikusradio.hu:9000/opera"},
  {"Make 80s great AGAIN"     , "http://ais-edge147-dal03.cdnstream.com/2281_128.mp3"},
  {"Radio Tango"              , "http://ais-edge148-pit01.cdnstream.com/2202_128.mp3"},
  {"Twente Gold - Fallout"    , "http://c18.radioboss.fm:8403/autodj"},
  {"Radio Galaxy - PipRaDI0"  , "http://c15.radioboss.fm:8078/autodj"},
  {"Klasszik Rádió 92.1"      , "http://s04.diazol.hu:9600/live.mp3"},
  {"RTHK Radio"               , "http://stm.rthk.hk:80/radio1"},
  {"Los 40 - Argentina "      , "http://edge01.radiohdvivo.com:9006/live"},
  {"Le Son Parisien "         , "http://stream.lesonparisien.com/hi"},
  {"Pure FM - Berlin"         , "http://s4.radionetz.de/purefm-bln.mp3"},
  {"Radio Soyuz Книга вслух"  , "http://65.109.84.248:8100/soyuzfm-192.mp3"},
  {"Radio DEFCON Hacker station" , "http://ice6.somafm.com/defcon-128-mp3"},
  {"KAH Radio Gansta"         , "http://ec5.yesstreaming.net:2170/stream"},
  {"Radio Patmos"             , "http://s3.yesstreaming.net:7011/radio"},
  {"Polskie Radio OLSZTYN"    , "http://rostr1.radioolsztyn.pl:8000/RO.mp3"},
  {"COOL radio | Serbia"      , "http://live.coolradio.rs/cool192"},
  {"Radio DIL india"          , "http://us3.streamingpulse.com:7026/radiodil2"},
  {"punkrockers-radio"        , "http://stream.punkrockers-radio.de:8000/prr.mp3"},
  {"K-ROCK Radio"             , "http://ice10.fluidstream.net:8080/ric.mp3"},
  {"Radio sakura Japan"       , "http://icecast.movemedia.be:8000/meteor"},
  {"Radio boysacc"            , "https://alphaboys-live.streamguys1.com/alphaboys.aac"},
};
static constexpr size_t default_stations = sizeof(default_station_list) / sizeof(default_station_list[0]);

void loadStationsFromSD() {
  SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
  if (SD.begin(SD_SPI_CS_PIN, SPI, 25000000)) {
    File f = SD.open("/station_list.txt", FILE_READ);
    if (f) {
      stations = 0;
      while (f.available() && stations < MAX_STATIONS) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0 || line.startsWith("#")) continue;
        int comma = line.indexOf(',');
        if (comma < 0) continue;
        String name = line.substring(0, comma);
        String url  = line.substring(comma + 1);
        name.trim(); url.trim();
        strncpy(sd_station_names[stations], name.c_str(), 63);
        sd_station_names[stations][63] = '\0';
        strncpy(sd_station_urls[stations], url.c_str(), 255);
        sd_station_urls[stations][255] = '\0';
        station_list[stations][0] = sd_station_names[stations];
        station_list[stations][1] = sd_station_urls[stations];
        stations++;
      }
      f.close();
      SD.end();
      if (stations > 0) return;
    }
    SD.end();
  }
  // Fallback
  stations = default_stations;
  for (size_t i = 0; i < stations; i++) {
    station_list[i][0] = default_station_list[i][0];
    station_list[i][1] = default_station_list[i][1];
  }
}

// -------------------------------------------------------------------
// Clase AudioOutputM5Speaker
// -------------------------------------------------------------------
class AudioOutputM5Speaker : public AudioOutput {
public:
  AudioOutputM5Speaker(m5::Speaker_Class* m5sound, uint8_t virtual_sound_channel = 0) {
    _m5sound = m5sound;
    _virtual_ch = virtual_sound_channel;
  }
  virtual ~AudioOutputM5Speaker(void) {};
  virtual bool begin(void) override { return true; }
  virtual bool ConsumeSample(int16_t sample[2]) override {
    if (_tri_buffer_index < tri_buf_size) {
      _tri_buffer[_tri_index][_tri_buffer_index  ] = sample[0];
      _tri_buffer[_tri_index][_tri_buffer_index+1] = sample[1];
      _tri_buffer_index += 2;
      return true;
    }
    flush();
    return false;
  }
  virtual void flush(void) override {
    if (_tri_buffer_index) {
      _m5sound->playRaw(_tri_buffer[_tri_index], _tri_buffer_index, hertz, true, 1, _virtual_ch);
      _tri_index = _tri_index < 2 ? _tri_index + 1 : 0;
      _tri_buffer_index = 0;
      ++_update_count;
    }
  }
  virtual bool stop(void) override {
    flush();
    _m5sound->stop(_virtual_ch);
    for (size_t i = 0; i < 3; ++i) {
      memset(_tri_buffer[i], 0, tri_buf_size * sizeof(int16_t));
    }
    ++_update_count;
    return true;
  }
  const int16_t* getBuffer(void) const { return _tri_buffer[(_tri_index + 2) % 3]; }
  const uint32_t getUpdateCount(void) const { return _update_count; }

protected:
  m5::Speaker_Class* _m5sound;
  uint8_t _virtual_ch;
  static constexpr size_t tri_buf_size = 1024;
  int16_t _tri_buffer[3][tri_buf_size];
  size_t _tri_buffer_index = 0;
  size_t _tri_index = 0;
  size_t _update_count = 0;
};

// -------------------------------------------------------------------
// Clase fft_t (completa) 
// -------------------------------------------------------------------
#define FFT_SIZE 256
class fft_t {
  float _wr[FFT_SIZE + 1];
  float _wi[FFT_SIZE + 1];
  float _fr[FFT_SIZE + 1];
  float _fi[FFT_SIZE + 1];
  uint16_t _br[FFT_SIZE + 1];
  size_t _ie;

public:
  fft_t(void) {
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
    _ie = logf( (float)FFT_SIZE ) / log(2.0) + 0.5;
    static constexpr float omega = 2.0f * M_PI / FFT_SIZE;
    static constexpr int s4 = FFT_SIZE / 4;
    static constexpr int s2 = FFT_SIZE / 2;
    for ( int i = 1 ; i < s4 ; ++i) {
      float f = cosf(omega * i);
      _wi[s4 + i] = f;
      _wi[s4 - i] = f;
      _wr[     i] = f;
      _wr[s2 - i] = -f;
    }
    _wi[s4] = _wr[0] = 1;

    size_t je = 1;
    _br[0] = 0;
    _br[1] = FFT_SIZE / 2;
    for ( size_t i = 0 ; i < _ie - 1 ; ++i ) {
      _br[ je << 1 ] = _br[ je ] >> 1;
      je = je << 1;
      for ( size_t j = 1 ; j < je ; ++j ) {
        _br[je + j] = _br[je] + _br[j];
      }
    }
  }

  void exec(const int16_t* in) {
    memset(_fi, 0, sizeof(_fi));
    for ( size_t j = 0 ; j < FFT_SIZE / 2 ; ++j ) {
      float basej = 0.25 * (1.0-_wr[j]);
      size_t r = FFT_SIZE - j - 1;
      _fr[_br[j]] = basej * (in[j * 2] + in[j * 2 + 1]);
      _fr[_br[r]] = basej * (in[r * 2] + in[r * 2 + 1]);
    }

    size_t s = 1;
    size_t i = 0;
    do {
      size_t ke = s;
      s <<= 1;
      size_t je = FFT_SIZE / s;
      size_t j = 0;
      do {
        size_t k = 0;
        do {
          size_t l = s * j + k;
          size_t m = ke * (2 * j + 1) + k;
          size_t p = je * k;
          float Wxmr = _fr[m] * _wr[p] + _fi[m] * _wi[p];
          float Wxmi = _fi[m] * _wr[p] - _fr[m] * _wi[p];
          _fr[m] = _fr[l] - Wxmr;
          _fi[m] = _fi[l] - Wxmi;
          _fr[l] += Wxmr;
          _fi[l] += Wxmi;
        } while ( ++k < ke) ;
      } while ( ++j < je );
    } while ( ++i < _ie );
  }

  uint32_t get(size_t index) {
    return (index < FFT_SIZE / 2) ? (uint32_t)sqrtf(_fr[ index ] * _fr[ index ] + _fi[ index ] * _fi[ index ]) : 0u;
  }
};

// -------------------------------------------------------------------
// Variables globales 
// -------------------------------------------------------------------
static constexpr const int preallocateBufferSize = 512 * 1024;
static constexpr const int preallocateCodecSize = 85332;
static void* preallocateBuffer = nullptr;
static void* preallocateCodec = nullptr;
static constexpr size_t WAVE_SIZE = 320;
static AudioOutputM5Speaker out(&M5Cardputer.Speaker, m5spk_virtual_channel);
static AudioGenerator *decoder = nullptr;
static AudioFileSourceICYStream *file = nullptr;
static AudioFileSourceBuffer *buff = nullptr;
static fft_t fft;
static bool fft_enabled = false;
static bool wave_enabled = false;
static uint16_t prev_y[(FFT_SIZE / 2)+1];
static uint16_t peak_y[(FFT_SIZE / 2)+1];
static int16_t wave_y[WAVE_SIZE];
static int16_t wave_h[WAVE_SIZE];
static int16_t raw_data[WAVE_SIZE * 3];
static int header_height = 0;
static size_t station_index = 0;
static char stream_title[128] = { 0 };
static const char* meta_text[2] = { nullptr, stream_title };
static const size_t meta_text_num = sizeof(meta_text) / sizeof(meta_text[0]);
static uint8_t meta_mod_bits = 0;
static volatile size_t playindex = ~0u;

// Colores — SIN CAMBIOS
#define COLOR_BAR_LOW  0x008000u
#define COLOR_BAR_HIGH 0x0CCE0Cu
#define COLOR_VU_LOW   0x008000u
#define COLOR_VU_HIGH  0x3AC13Au
#define COLOR_WAVE     0x06CC06u
#define COLOR_PEAK     TFT_WHITE
#define COLOR_TEXT     0x0CCE0Cu

// -------------------------------------------------------------------
// Callbacks y funciones auxiliares
// -------------------------------------------------------------------
static void MDCallback(void *cbData, const char *type, bool isUnicode, const char *string) {
  (void)cbData;
  if ((strcmp(type, "StreamTitle") == 0) && (strcmp(stream_title, string) != 0)) {
    strncpy(stream_title, string, sizeof(stream_title));
    meta_mod_bits |= 2;
  }
}

static void stop(void) {
  if (decoder) { decoder->stop(); delete decoder; decoder = nullptr; }
  if (buff) { buff->close(); delete buff; buff = nullptr; }
  if (file) { file->close(); delete file; file = nullptr; }
  out.stop();
}

static void play(size_t index) {
  playindex = index;
}

static void decodeTask(void*) {
  for (;;) {
    delay(1);
    if (playindex != ~0u) {
      auto index = playindex;
      playindex = ~0u;
      stop();
      meta_text[0] = station_list[index][0];
      stream_title[0] = 0;
      meta_mod_bits = 3;
      file = new AudioFileSourceICYStream(station_list[index][1]);
      file->RegisterMetadataCB(MDCallback, (void*)"ICY");
      buff = new AudioFileSourceBuffer(file, preallocateBuffer, preallocateBufferSize);
      const char* url = station_list[index][1];
      if (strstr(url, ".aac") || strstr(url, ".m4a") || strstr(url, "alphaboys")) {
        decoder = new AudioGeneratorAAC(preallocateCodec, preallocateCodecSize);
      } else {
        decoder = new AudioGeneratorMP3(preallocateCodec, preallocateCodecSize);
      }
      decoder->begin(buff, &out);
    }
    if (decoder && decoder->isRunning()) {
      if (!decoder->loop()) {
        decoder->stop();
        delay(1000);
        play(station_index);
      }
    }
  }
}

static uint32_t bgcolor(LGFX_Device* gfx, int y) {
  auto h = gfx->height();
  auto dh = h - header_height;
  int v = ((h - y)<<5) / dh;
  if (dh > 44) {
    int v2 = ((h - y - 1)<<5) / dh;
    if ((v >> 2) != (v2 >> 2)) {
      return 0x000000u;
    }
  }
  return gfx->color888(0, 0, 0);
}

// -------------------------------------------------------------------
// Configuración gráfica 
// -------------------------------------------------------------------
static void gfxSetup(LGFX_Device* gfx) {
  if (gfx == nullptr) return;
  gfx->setFont(&fonts::lgfxJapanGothic_12);
  gfx->setEpdMode(epd_mode_t::epd_fastest);
  gfx->setTextWrap(false);
  gfx->setCursor(0, 8);
  gfx->println("RHesus RAdio");
  gfx->fillRect(0, 6, gfx->width(), 2, TFT_BLACK);
  header_height = (gfx->height() > 100) ? 33 : 21;
  updateVisualMode(gfx, currentVisualMode);
  for (int x = 0; x < (FFT_SIZE/2)+1; ++x) { prev_y[x] = INT16_MAX; peak_y[x] = INT16_MAX; }
  for (int x = 0; x < WAVE_SIZE; ++x) { wave_y[x] = gfx->height(); wave_h[x] = 0; }
}

void updateVisualMode(LGFX_Device* gfx, VisualMode mode) {
  fft_enabled = false;
  wave_enabled = false;
  
  switch (mode) {
    case MODE_BARS: fft_enabled = true; break;
    case MODE_SPECTRUM: wave_enabled = true; break;
    case MODE_BOTH: fft_enabled = true; wave_enabled = true; break;
    default: fft_enabled = true; break;
  }
  
  if (gfx) {
    gfx->fillRect(0, header_height, gfx->width(), gfx->height() - header_height, TFT_BLACK);
    for (int x = 0; x < (FFT_SIZE/2)+1; ++x) { prev_y[x] = INT16_MAX; peak_y[x] = INT16_MAX; }
    for (int x = 0; x < WAVE_SIZE; ++x) { wave_y[x] = gfx->height(); wave_h[x] = 0; }
  }
}

// -------------------------------------------------------------------
// Bucle gráfico principal 
// -------------------------------------------------------------------
void gfxLoop(LGFX_Device* gfx) {
  if (gfx == nullptr) return;
  
  if (header_height > 32) {
    if (meta_mod_bits) {
      gfx->startWrite();
      for (int id = 0; id < meta_text_num; ++id) {
        if (0 == (meta_mod_bits & (1<<id))) continue;
        meta_mod_bits &= ~(1<<id);
        size_t y = id * 12;
        if (y+12 >= header_height) continue;
        gfx->setCursor(4, 8 + y);
        gfx->fillRect(0, 8 + y, gfx->width(), 12, gfx->getBaseColor());
        gfx->print(meta_text[id]);
        gfx->print(" ");
      }
      gfx->display();
      gfx->endWrite();
    }
  } else {
    static int title_x, title_id, wait = INT16_MAX;
    if (meta_mod_bits) {
      if (meta_mod_bits & 1) { title_x = 4; title_id = 0; gfx->fillRect(0, 8, gfx->width(), 12, gfx->getBaseColor()); }
      meta_mod_bits = 0;
      wait = 0;
    }
    if (--wait < 0) {
      int tx = title_x;
      int tid = title_id;
      wait = 3;
      gfx->startWrite();
      uint_fast8_t no_data_bits = 0;
      do {
        if (tx == 4) wait = 255;
        gfx->setCursor(tx, 8);
        const char* meta = meta_text[tid];
        if (meta[0] != 0) {
          gfx->print(meta);
          gfx->print("  /  ");
          tx = gfx->getCursorX();
          if (++tid == meta_text_num) tid = 0;
          if (tx <= 4) { title_x = tx; title_id = tid; }
        } else {
          if ((no_data_bits |= 1 << tid) == ((1 << meta_text_num) - 1)) break;
          if (++tid == meta_text_num) tid = 0;
        }
      } while (tx < gfx->width());
      --title_x;
      gfx->display();
      gfx->endWrite();
    }
  }

  if (fft_enabled || wave_enabled) {
    static int prev_x[2];
    static int peak_x[2];

    auto buf = out.getBuffer();
    if (buf) {
      memcpy(raw_data, buf, WAVE_SIZE * 2 * sizeof(int16_t));
      gfx->startWrite();

      // Dibujar barras VU superiores 
      for (size_t i = 0; i < 2; ++i) {
        int32_t level = 0;
        for (size_t j = i; j < 640; j += 32) {
          uint32_t lv = abs(raw_data[j]);
          if (level < lv) level = lv;
        }
        int32_t x = (level * gfx->width()) / INT16_MAX;
        int32_t px = prev_x[i];
        if (px != x) {
          gfx->fillRect(x, i * 3, px - x, 2, px < x ? COLOR_VU_HIGH : COLOR_VU_LOW);
          prev_x[i] = x;
        }
        px = peak_x[i];
        if (px > x) {
          gfx->writeFastVLine(px, i * 3, 2, TFT_BLACK);
          px--;
        } else px = x;
        if (peak_x[i] != px) {
          peak_x[i] = px;
          gfx->writeFastVLine(px, i * 3, 2, COLOR_PEAK);
        }
      }
      gfx->display();

      if (fft_enabled) fft.exec(raw_data);
      
      size_t bw = gfx->width() / 120;
      if (bw < 3) bw = 3;
      int32_t dsp_height = gfx->height();
      int32_t fft_height = dsp_height - header_height - 1;
      size_t xe = gfx->width() / bw;
      if (xe > (FFT_SIZE/1)) xe = (FFT_SIZE/1);
      int32_t wave_next = ((header_height + dsp_height) >> 1) + ( (int32_t)(256 - (raw_data[0] + raw_data[1])) * fft_height >> 17 );

      uint32_t bar_color[2] = { COLOR_BAR_LOW, COLOR_BAR_HIGH };

      for (size_t bx = 0; bx <= xe; ++bx) {
        size_t x = bx * bw;
        if ((x & 7) == 0) { gfx->display(); taskYIELD(); }
        
        if (fft_enabled) {
          int32_t f = fft.get(bx);
          int32_t y = (f * fft_height) >> 18;
          if (y > fft_height) y = fft_height;
          y = dsp_height - y;
          int32_t py = prev_y[bx];
          if (y != py) {
            gfx->fillRect(x, y, bw - 1, py - y, bar_color[(y < py)]);
            prev_y[bx] = y;
          }
          int32_t py_peak = peak_y[bx] + 1;
          if (py_peak < y) {
            gfx->writeFastHLine(x, py_peak - 1, bw - 1, bgcolor(gfx, py_peak - 1));
          } else py_peak = y - 1;
          if (peak_y[bx] != py_peak) {
            peak_y[bx] = py_peak;
            gfx->writeFastHLine(x, py_peak, bw - 1, COLOR_PEAK);
          }
        }

        if (wave_enabled) {
          for (size_t bi = 0; bi < bw; ++bi) {
            size_t i = x + bi;
            if (i >= gfx->width() || i >= WAVE_SIZE) break;
            int32_t y = wave_y[i];
            int32_t h = wave_h[i];
            bool use_bg = (bi+1 == bw);
            if (h>0) {
              gfx->setAddrWindow(i, y, 1, h);
              h += y;
              do {
                uint32_t bg = (use_bg || y < peak_y[bx]) ? bgcolor(gfx, y)
                            : (y == peak_y[bx]) ? COLOR_WAVE
                            : bar_color[(y >= prev_y[bx])];
                gfx->writeColor(bg, 1);
              } while (++y < h);
            }
            size_t i2 = i << 1;
            int32_t y1 = wave_next;
            wave_next = ((header_height + dsp_height) >> 1) + ( (int32_t)(512 - (raw_data[i2] + raw_data[i2 + 1])) * fft_height >> 17 );
            int32_t y2 = wave_next;
            if (y1 > y2) { int32_t tmp = y1; y1 = y2; y2 = tmp; }
            y = y1;
            h = y2 + 1 - y;
            wave_y[i] = y;
            wave_h[i] = h;
            if (h>0) {
              gfx->setAddrWindow(i, y, 1, h);
              h += y;
              do { gfx->writeColor(COLOR_WAVE, 1); } while (++y < h);
            }
          }
        }
      }

      gfx->display();
      gfx->endWrite();
    }
  }

  // Barra de volumen 
  if (!gfx->displayBusy()) {
    static int px;
    uint8_t v = M5Cardputer.Speaker.getVolume();
    int x = v * (gfx->width()) >> 8;
    if (px != x) {
      gfx->fillRect(x, 6, px - x, 2, px < x ? COLOR_VU_HIGH : 0u);
      gfx->display();
      px = x;
    }
  }
}

// -------------------------------------------------------------------
// Setup 
// -------------------------------------------------------------------
void setup(void) {
  Serial.begin(115200);

  auto cfg = M5.config();
  cfg.external_speaker.hat_spk = true;
  M5Cardputer.begin(cfg);

  loadStationsFromSD();  // <- único añadido

  preallocateBuffer = malloc(preallocateBufferSize);
  preallocateCodec = malloc(preallocateCodecSize);

  {
    auto spk_cfg = M5Cardputer.Speaker.config();
    spk_cfg.sample_rate = 48000;
    spk_cfg.task_pinned_core = APP_CPU_NUM;
    M5Cardputer.Speaker.config(spk_cfg);
  }

  M5Cardputer.Speaker.begin();
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.setTextColor(COLOR_TEXT, TFT_BLACK);
  M5Cardputer.Display.setFont(&fonts::lgfxJapanGothic_20);    
  
  connectToWiFi();

  M5Cardputer.Lcd.setTextSize(1);
  M5Cardputer.Display.clear();
  
  currentVisualMode = MODE_SPECTRUM;
  gfxSetup(&M5Cardputer.Display);

  preferences.begin("M5_settings", false);
  previousVolume = preferences.getInt("PreviousVolume", 50);
  station_index = preferences.getInt("PreviousStation", 1);
  preferences.end();

  if (station_index >= stations) station_index = 0;  // <- único guard añadido
  
  play(station_index);
  xTaskCreatePinnedToCore(decodeTask, "decodeTask", 8096, nullptr, 1, nullptr, PRO_CPU_NUM);
}

// -------------------------------------------------------------------
// Loop 
// -------------------------------------------------------------------
void loop(void) {
  gfxLoop(&M5Cardputer.Display);

  {
    static int prev_frame;
    int frame;
    do { delay(1); } while (prev_frame == (frame = millis() >> 3));
    prev_frame = frame;
  }

  M5Cardputer.update();
  size_t v = M5Cardputer.Speaker.getVolume();
  if (M5Cardputer.Keyboard.isChange()) {
    M5Cardputer.Speaker.tone(550, 50);
    
    if (M5Cardputer.Keyboard.isKeyPressed('v')) {
      delay(200);
      M5Cardputer.Speaker.tone(600, 50);
      currentVisualMode = static_cast<VisualMode>((currentVisualMode + 1) % MODE_COUNT);
      updateVisualMode(&M5Cardputer.Display, currentVisualMode);
      
      M5Cardputer.Display.fillRect(0, 40, M5Cardputer.Display.width(), 20, TFT_BLACK);
      M5Cardputer.Display.setCursor(10, 45);
      M5Cardputer.Display.setTextColor(COLOR_TEXT);
      switch (currentVisualMode) {
        case MODE_BARS: M5Cardputer.Display.print("Barras"); break;
        case MODE_SPECTRUM: M5Cardputer.Display.print("Spectrum"); break;
        case MODE_BOTH: M5Cardputer.Display.print("Ambos"); break;
      }
      M5Cardputer.Display.display();
      delay(500);
      M5Cardputer.Display.fillRect(0, 40, M5Cardputer.Display.width(), 20, TFT_BLACK);
      M5Cardputer.Display.display();
    }
    
    if (M5Cardputer.Keyboard.isKeyPressed('/')) {
      delay(200); M5Cardputer.Speaker.tone(1000, 100);
      if (++station_index >= stations) station_index = 0;
      play(station_index);
    }
    if (M5Cardputer.Keyboard.isKeyPressed(',')) {
      delay(200); M5Cardputer.Speaker.tone(800, 100);
      if (station_index <= 0) station_index = stations - 1; else station_index--;
      play(station_index);
    }
    if (M5Cardputer.Keyboard.isKeyPressed(';')) {
      if (v <= 255) { v += 10; M5Cardputer.Speaker.setVolume(v); previousVolume = M5Cardputer.Speaker.getVolume(); }
    }
    if (M5Cardputer.Keyboard.isKeyPressed('.')) {
      if (v >= 0) { v -= 10; M5Cardputer.Speaker.setVolume(v); previousVolume = M5Cardputer.Speaker.getVolume(); }
    }
    if (M5Cardputer.Keyboard.isKeyPressed('m')) {
      if (M5Cardputer.Speaker.getVolume() > 0) { previousVolume = M5Cardputer.Speaker.getVolume(); M5Cardputer.Speaker.setVolume(0); }
      else { M5Cardputer.Speaker.setVolume(previousVolume); }
    }
    preferences.begin("M5_settings", false);
    preferences.putInt("PreviousVolume", previousVolume);
    preferences.putInt("PreviousStation", station_index);
    preferences.end();
  }
  
  if (M5Cardputer.BtnA.wasPressed()) M5Cardputer.Speaker.tone(440, 50);
  if (M5Cardputer.BtnA.wasDecideClickCount()) {
    switch (M5Cardputer.BtnA.getClickCount()) {
      case 1: M5Cardputer.Speaker.tone(1000, 80); if (++station_index >= stations) station_index = 0; play(station_index); break;
      case 2: M5Cardputer.Speaker.tone(800, 100); if (station_index == 0) station_index = stations; play(--station_index); break;
    }
  }
  if (M5Cardputer.BtnA.isHolding() || M5.BtnB.isPressed() || M5.BtnC.isPressed()) {
    int add = (M5.BtnB.isPressed()) ? -1 : 1;
    if (M5.BtnA.isHolding()) add = M5.BtnA.getClickCount() ? -1 : 1;
    v += add;
    if (v <= 255) M5Cardputer.Speaker.setVolume(v);
  }
}
