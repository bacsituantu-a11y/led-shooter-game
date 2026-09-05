// Game ban mau WS2812 - ESP32 WROOM + MAX98357
// Thu vien can cai: FastLED

#include <FastLED.h>
#include <driver/i2s_std.h>

// ---------- CAU HINH CHAN ----------
#define LED_PIN     16
#define NUM_LEDS    70
#define BTN_GREEN   32
#define BTN_BLUE    33
#define BTN_RED     27
#define I2S_BCLK    26
#define I2S_LRC     25
#define I2S_DIN     22

#define SAMPLE_RATE 22050
#define BRIGHTNESS  70          // 0-255, giam de tiet kiem dong
#define MAX_LEVEL   10
#define CHAIN_START 10          // so led chuoi ban dau
#define BULLET_MS   15          // toc do dan (ms/led) - co dinh
#define WIN_SCORE   100

CRGB leds[NUM_LEDS];
const CRGB COLORS[3] = { CRGB::Green, CRGB::Blue, CRGB::Red };   // 0 xanh la, 1 xanh duong, 2 do

// ---------- AM THANH (chay tren core 0) ----------
enum Snd { SND_START, SND_SHOOT, SND_HIT, SND_MISS, SND_LOSE, SND_WIN, SND_TICK, SND_OK };
QueueHandle_t sndQ;

static i2s_chan_handle_t txChan = NULL;

void i2sInit() {
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan_cfg.dma_desc_num  = 8;     // = dma_buf_count cu
  chan_cfg.dma_frame_num = 256;   // = dma_buf_len cu
  chan_cfg.auto_clear    = true;  // = tx_desc_auto_clear cu

  esp_err_t e = i2s_new_channel(&chan_cfg, &txChan, NULL);
  if (e != ESP_OK) { Serial.printf("I2S_NEW_CHAN=FAIL %s\n", esp_err_to_name(e)); txChan = NULL; return; }

  i2s_std_config_t std_cfg = {
    .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = (gpio_num_t)I2S_BCLK,
      .ws   = (gpio_num_t)I2S_LRC,
      .dout = (gpio_num_t)I2S_DIN,
      .din  = I2S_GPIO_UNUSED,
      .invert_flags = { .mclk_inv = 0, .bclk_inv = 0, .ws_inv = 0 },
    },
  };
  e = i2s_channel_init_std_mode(txChan, &std_cfg);
  if (e != ESP_OK) { Serial.printf("I2S_INIT_STD=FAIL %s\n", esp_err_to_name(e)); return; }
  e = i2s_channel_enable(txChan);
  if (e != ESP_OK) { Serial.printf("I2S_ENABLE=FAIL %s\n", esp_err_to_name(e)); return; }
  Serial.println("I2S_BEGIN=OK");
}

// Phat 1 am: quet tan so f1 -> f2 trong ms, noise=true thi phat tieng "tach"
void synth(int ms, float f1, float f2, float vol, bool noise = false) {
  static int16_t buf[512];
  int total = SAMPLE_RATE * ms / 1000;
  float phase = 0;
  for (int i = 0; i < total;) {
    int n = min(256, total - i);
    for (int k = 0; k < n; k++) {
      float t = (float)(i + k) / total;
      float f = f1 + (f2 - f1) * t;
      phase += 2 * PI * f / SAMPLE_RATE;
      float env = (t < 0.05f) ? t / 0.05f : (1.0f - t);
      float s = noise ? (random(-1000, 1000) / 1000.0f) : sinf(phase);
      int16_t v = (int16_t)(s * env * vol * 32000);
      buf[2 * k] = v; buf[2 * k + 1] = v;
    }
    size_t w;
    if (!txChan) return;
    i2s_channel_write(txChan, buf, n * 4, &w, 1000);
    i += n;
  }
}
void silence(int ms) { synth(ms, 100, 100, 0); }

void soundTask(void*) {
  int s;
  for (;;) {
    if (xQueueReceive(sndQ, &s, portMAX_DELAY)) {
      switch (s) {
        case SND_START: synth(150, 523, 523, .4); synth(150, 659, 659, .4); synth(300, 784, 784, .4); break; // to te ti
        case SND_SHOOT: synth(80, 1800, 500, .35); break;                                                 // chiu
        case SND_HIT:   synth(200, 220, 50, .8); break;                                                   // bum
        case SND_MISS:  synth(40, 0, 0, .5, true); break;                                                 // tach
        case SND_LOSE:  synth(300, 300, 280, 1.0); silence(80); synth(500, 250, 200, 1.0); break;           // te te
        case SND_WIN:   synth(100, 784, 784, .4); synth(100, 988, 988, .4); synth(250, 1319, 1319, .4); break;
        case SND_TICK:  synth(30, 1000, 1000, .3); break;
        case SND_OK:    synth(80, 880, 880, .4); synth(120, 1320, 1320, .4); break;
      }
    }
  }
}
void play(Snd s) { int v = s; xQueueSend(sndQ, &v, 0); }

// ---------- NUT BAM ----------
struct Btn {
  uint8_t pin; bool last = HIGH; uint32_t lastPress = 0;
  bool pressed() {
    bool s = digitalRead(pin);
    bool p = false;
    if (s != last) {
      last = s;
      if (s == LOW && millis() - lastPress > 150) { lastPress = millis(); p = true; }
    }
    return p;
  }
};
Btn btn[3] = { {BTN_GREEN}, {BTN_BLUE}, {BTN_RED} };

// ---------- TRANG THAI GAME ----------
enum State { MENU, PLAY };
State state = MENU;
int level = 1;
int score = 0;

uint8_t chain[NUM_LEDS + 10];   // chain[0] = dau chuoi (gan goc nhat)
int chainLen = 0;
int headPos = 0;                // vi tri led cua dau chuoi
uint32_t lastStep = 0;

struct Bullet { int pos; uint8_t color; bool active; };
Bullet bullets[6];
uint32_t lastBullet = 0;

int stepMs() { return max(80, 450 - (level - 1) * 35); }   // toc do chuoi theo bai

void flashAll(CRGB c, int times, int ms) {
  for (int i = 0; i < times; i++) {
    fill_solid(leds, NUM_LEDS, c); FastLED.show(); delay(ms);
    FastLED.clear(); FastLED.show(); delay(ms);
  }
}

void showMenu() {
  FastLED.clear();
  for (int i = 0; i < level; i++) leds[NUM_LEDS - 1 - i] = CRGB::Green;   // bai N = N led cuoi
  FastLED.show();
}

void newChain() {
  chainLen = CHAIN_START;
  headPos = NUM_LEDS - CHAIN_START;
  for (int i = 0; i < chainLen; i++) chain[i] = random(3);
  for (auto& b : bullets) b.active = false;
  lastStep = millis();
}

void startGame() {
  score = 0;
  newChain();
  state = PLAY;
}

void loseGame() {
  play(SND_LOSE);
  flashAll(CRGB::Red, 3, 250);
  state = MENU;
  showMenu();
}

void nextLevel() {
  play(SND_WIN);
  flashAll(CRGB::Green, 3, 200);
  if (level >= MAX_LEVEL) {           // thang toan bo
    for (int k = 0; k < 3; k++) { fill_rainbow(leds, NUM_LEDS, k * 40, 5); FastLED.show(); delay(400); }
    level = 1; state = MENU; showMenu();
    return;
  }
  level++;
  score = 0;
  newChain();
}

void drawGame() {
  FastLED.clear();
  for (int i = 0; i < chainLen; i++) {
    int p = headPos + i;
    if (p >= 0 && p < NUM_LEDS) leds[p] = COLORS[chain[i]];
  }
  for (auto& b : bullets)
    if (b.active && b.pos >= 0 && b.pos < NUM_LEDS) leds[b.pos] = COLORS[b.color];
  // led goc bao diem: sang trang mo khi gan qua man
  leds[0] += CRGB(score / 2, score / 2, score / 2);
  FastLED.show();
}

void updateGame() {
  uint32_t now = millis();

  // nguoi choi ban
  for (int c = 0; c < 3; c++) {
    if (btn[c].pressed()) {
      for (auto& b : bullets) {
        if (!b.active) { b.active = true; b.pos = 1; b.color = c; play(SND_SHOOT); break; }
      }
    }
  }

  // chuoi di chuyen ve goc
  if (now - lastStep >= (uint32_t)stepMs()) {
    lastStep = now;
    headPos--;
    // nap them led moi o cuoi day
    while (headPos + chainLen - 1 < NUM_LEDS - 1 && chainLen < NUM_LEDS + 5) chain[chainLen++] = random(3);
    if (headPos <= 0) { loseGame(); return; }
  }

  // dan bay - toc do co dinh
  if (now - lastBullet >= BULLET_MS) {
    lastBullet = now;
    for (auto& b : bullets) {
      if (!b.active) continue;
      b.pos++;
      if (b.pos >= headPos) {                // cham dau chuoi
        b.active = false;
        if (b.color == chain[0]) {           // dung mau -> huy dau chuoi
          play(SND_HIT);
          for (int i = 1; i < chainLen; i++) chain[i - 1] = chain[i];
          chainLen--; headPos++;
          if (chainLen == 0) { chainLen = 1; headPos = NUM_LEDS - 1; chain[0] = random(3); }
          score += 10;
          if (score >= WIN_SCORE) { nextLevel(); return; }
        } else {                             // sai mau -> noi vao dau
          play(SND_MISS);
          for (int i = chainLen; i > 0; i--) chain[i] = chain[i - 1];
          chain[0] = b.color; chainLen++; headPos--;
          if (headPos <= 0) { loseGame(); return; }
        }
      }
    }
  }
  drawGame();
}

// ---------- SETUP / LOOP ----------
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("=== led_shooter_game (70 LED, I2S std driver) ===");
  pinMode(BTN_GREEN, INPUT_PULLUP);
  pinMode(BTN_BLUE, INPUT_PULLUP);
  pinMode(BTN_RED, INPUT_PULLUP);
  randomSeed(esp_random());

  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.clear(); FastLED.show();

  i2sInit();
  sndQ = xQueueCreate(8, sizeof(int));
  xTaskCreatePinnedToCore(soundTask, "snd", 4096, NULL, 1, NULL, 0);

  play(SND_START);          // to te ti
  showMenu();               // led cuoi xanh la = bai 1
}

void loop() {
  switch (state) {
    case MENU:
      // do: tang bai (khong quay vong)
      if (btn[2].pressed() && level < MAX_LEVEL) { level++; play(SND_TICK); showMenu(); }
      // xanh la: giam bai (khong quay vong)
      if (btn[0].pressed() && level > 1)         { level--; play(SND_TICK); showMenu(); }
      // xanh duong: vao game ngay
      if (btn[1].pressed()) { play(SND_OK); startGame(); }
      break;
    case PLAY:
      updateGame();
      break;
  }
  delay(2);
}
