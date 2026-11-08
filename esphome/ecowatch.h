// Display logic for the EcoWatch Julia ESPHome firmware.
// Both rings are 12-hour dials: LED 0 is the first LED clockwise from 12 o'clock and the chain runs
// clockwise. Outer ring: 48 LEDs (15 min each), inner ring: 24 LEDs (30 min each).
// Like the stock firmware, each LED shows the *next* occurrence of its dial position, so the ring
// shows the current slot plus the following 12 hours.
#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "esphome/components/light/addressable_light.h"
#include "esphome/components/light/light_state.h"
#include "esphome/core/color.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/core/time.h"

namespace ecowatch {

using esphome::Color;
using esphome::ESPTime;
using esphome::light::AddressableLight;

static const int DIAL_MINUTES = 12 * 60;

enum Zone { ZONE_NONE = 0, ZONE_LOW = 1, ZONE_MID = 2, ZONE_HIGH = 3 };

// Colour mode select options, in this order.
enum ColorMode { MODE_AVERAGE = 0, MODE_LIMITS = 1, MODE_GREEN_HOURS = 2 };

struct DayStats {
  int key;
  float avg, min, max;
  std::vector<float> sorted;  // ascending
};

// Prices pushed by Home Assistant: one value per slot, starting at `start` (unix time).
class PriceStore {
 public:
  void set(int64_t start, int resolution_min, const std::vector<float> &prices) {
    this->start_ = start;
    this->res_s_ = std::max(1, resolution_min) * 60;
    this->prices_ = prices;
    this->days_.clear();
    std::vector<int> keys;
    keys.reserve(prices.size());
    for (size_t i = 0; i < prices.size(); i++)
      keys.push_back(day_key(start + (int64_t) i * this->res_s_));
    for (size_t i = 0; i < prices.size(); i++) {
      DayStats *d = this->find_(keys[i]);
      if (d == nullptr) {
        this->days_.push_back(DayStats{keys[i], 0, 0, 0, {}});
        d = &this->days_.back();
      }
      d->sorted.push_back(prices[i]);
    }
    for (auto &d : this->days_) {
      std::sort(d.sorted.begin(), d.sorted.end());
      float sum = 0;
      for (float p : d.sorted) sum += p;
      d.avg = sum / d.sorted.size();
      d.min = d.sorted.front();
      d.max = d.sorted.back();
    }
  }

  bool empty() const { return this->prices_.empty(); }
  int resolution_min() const { return this->res_s_ / 60; }
  size_t size() const { return this->prices_.size(); }
  int64_t end() const { return this->start_ + (int64_t) this->prices_.size() * this->res_s_; }

  float at(int64_t t) const {
    if (this->prices_.empty() || t < this->start_) return NAN;
    int64_t idx = (t - this->start_) / this->res_s_;
    if (idx >= (int64_t) this->prices_.size()) return NAN;
    return this->prices_[idx];
  }

  const DayStats *day(int64_t t) const {
    for (auto &d : this->days_)
      if (d.key == day_key(t)) return &d;
    return nullptr;
  }

  static int day_key(int64_t t) {
    ESPTime lt = ESPTime::from_epoch_local((time_t) t);
    return lt.year * 1000 + lt.day_of_year;
  }

 protected:
  DayStats *find_(int key) {
    for (auto &d : this->days_)
      if (d.key == key) return &d;
    return nullptr;
  }

  int64_t start_{0};
  int res_s_{3600};
  std::vector<float> prices_;
  std::vector<DayStats> days_;
};

inline PriceStore &prices() {
  static PriceStore store;
  return store;
}

struct PriceSettings {
  int mode;            // ColorMode
  float low_limit;     // MODE_LIMITS: <= low is green
  float high_limit;    // MODE_LIMITS: <= high is yellow, above is red
  int green_hours;     // MODE_GREEN_HOURS: this many cheapest hours of the day are green
  float avg_offset;    // added to the daily average ("flexible average")
  bool below_zero_low; // negative prices are always green
};

inline int zone_for(int64_t t, const PriceSettings &s) {
  auto &store = prices();
  float p = store.at(t);
  if (std::isnan(p)) return ZONE_NONE;
  if (s.below_zero_low && p < 0) return ZONE_LOW;
  if (s.mode == MODE_LIMITS) {
    if (p <= s.low_limit) return ZONE_LOW;
    if (p <= s.high_limit) return ZONE_MID;
    return ZONE_HIGH;
  }
  const DayStats *d = store.day(t);
  if (d == nullptr) return ZONE_NONE;
  if (s.mode == MODE_GREEN_HOURS && s.green_hours > 0) {
    size_t n = std::min(d->sorted.size(), (size_t) (s.green_hours * 60 / std::max(1, store.resolution_min())));
    if (n > 0 && p <= d->sorted[n - 1]) return ZONE_LOW;
  }
  // Daily average: at or below average is green; the range from average to the day's maximum is
  // split in two equal halves, yellow and red.
  float avg = d->avg + s.avg_offset;
  if (p <= avg) return ZONE_LOW;
  if (p <= avg + (d->max - avg) / 2) return ZONE_MID;
  return ZONE_HIGH;
}

// Start time of the slot shown by LED `i` on a ring with `n` LEDs over the 12-hour dial.
// `is_current` is set for the LED that contains "now".
inline int64_t slot_time(const ESPTime &now, int i, int n, bool *is_current) {
  int slot = DIAL_MINUTES / n;
  int now_dial = (now.hour % 12) * 60 + now.minute;
  int cur_start = now_dial / slot * slot;
  int delta = ((i * slot - cur_start) % DIAL_MINUTES + DIAL_MINUTES) % DIAL_MINUTES;
  if (is_current != nullptr) *is_current = delta == 0;
  return (int64_t) now.timestamp - ((now_dial - cur_start) * 60 + now.second) + (int64_t) delta * 60;
}

struct Colors {
  Color low{0, 255, 0};
  Color mid{255, 220, 0};  // gamma 2.8 crushes green: 140 looked salmon on the ring, 220 reads yellow
  Color high{255, 0, 0};
  Color none{120, 0, 255};  // no price yet (stock firmware uses purple too)
  Color tariff{255, 80, 0};
};

// "Flash current" select options, in this order.
enum FlashMode { FLASH_OFF = 0, FLASH_HOUR = 1, FLASH_SLOT = 2 };

// On/off phase for flashing, toggling every `period_ms`.
inline bool flash_on(uint32_t period_ms) { return (esphome::millis() / std::max<uint32_t>(50, period_ms)) % 2 == 0; }

// Whether LED `i` of an `n`-LED ring should flash: its dial hour is the current hour (FLASH_HOUR),
// or it is the slot containing now (FLASH_SLOT).
inline bool is_flash_led(const ESPTime &now, int i, int n, int mode) {
  if (mode == FLASH_OFF) return false;
  int slot = DIAL_MINUTES / n;
  int now_dial = (now.hour % 12) * 60 + now.minute;
  if (mode == FLASH_HOUR) return (i * slot) / 60 == now_dial / 60;
  return i == now_dial / slot;
}

// Shown while there is no valid time yet: one blue LED walking round the ring.
inline void render_waiting(AddressableLight &it) {
  int pos = (esphome::millis() / 150) % it.size();
  for (int i = 0; i < it.size(); i++) it[i] = i == pos ? Color(0, 0, 255) : Color::BLACK;
}

inline void render_price(AddressableLight &it, const ESPTime &now, const PriceSettings &s, int flash_mode,
                         uint32_t flash_period_ms, bool hide_missing, const Colors &c = Colors()) {
  if (!now.is_valid()) return render_waiting(it);
  bool dark_phase = !flash_on(flash_period_ms);
  for (int i = 0; i < it.size(); i++) {
    int64_t t = slot_time(now, i, it.size(), nullptr);
    Color col;
    switch (zone_for(t, s)) {
      case ZONE_LOW: col = c.low; break;
      case ZONE_MID: col = c.mid; break;
      case ZONE_HIGH: col = c.high; break;
      default: col = hide_missing ? Color::BLACK : c.none; break;
    }
    if (dark_phase && is_flash_led(now, i, it.size(), flash_mode)) col = Color::BLACK;
    it[i] = col;
  }
}

// Tariff (peak-load) hours: LEDs whose slot falls in [start_hour, stop_hour) light up.
// stop < start wraps over midnight. weekdays_only: Monday-Friday only.
inline void render_tariff(AddressableLight &it, const ESPTime &now, int start_hour, int stop_hour,
                          bool weekdays_only, const Colors &c = Colors()) {
  if (!now.is_valid()) return render_waiting(it);
  for (int i = 0; i < it.size(); i++) {
    ESPTime lt = ESPTime::from_epoch_local((time_t) slot_time(now, i, it.size(), nullptr));
    bool in_hours = start_hour <= stop_hour ? (lt.hour >= start_hour && lt.hour < stop_hour)
                                            : (lt.hour >= start_hour || lt.hour < stop_hour);
    bool weekday = lt.day_of_week >= 2 && lt.day_of_week <= 6;  // 1 = Sunday
    it[i] = in_hours && (weekday || !weekdays_only) ? c.tariff : Color::BLACK;
  }
}

// Power bar: lights power / max_power of the ring clockwise from 12, green -> yellow -> red.
inline void render_power(AddressableLight &it, float power, float max_power) {
  if (std::isnan(power) || max_power <= 0) {
    for (int i = 0; i < it.size(); i++) it[i] = Color::BLACK;
    return;
  }
  float frac = std::max(0.0f, std::min(1.0f, power / max_power));
  int lit = (int) std::lround(frac * it.size());
  for (int i = 0; i < it.size(); i++) {
    if (i >= lit) {
      it[i] = Color::BLACK;
      continue;
    }
    float f = (float) i / std::max(1, (int) it.size() - 1);
    uint8_t r = f < 0.5f ? (uint8_t) (f * 2 * 255) : 255;
    uint8_t g = f < 0.5f ? 255 : (uint8_t) ((1 - f) * 2 * 255);
    it[i] = Color(r, g, 0);
  }
}

// --- Custom mode: per-LED colours pushed from Home Assistant ----------------------------------

// Must match the lights' gamma_correct (ESPHome default 2.8).
static const float LIGHT_GAMMA = 2.8f;

// Pixel buffers for the "Custom" ring mode: index 0 = outer (48), 1 = inner (24).
// Values are linear: what HA sends, already multiplied by the optional per-pixel brightness.
inline std::vector<Color> &custom_pixels(int ring) {
  static std::vector<Color> rings[2] = {std::vector<Color>(48, Color::BLACK), std::vector<Color>(24, Color::BLACK)};
  return rings[ring];
}

// "outer" / "inner" -> 0 / 1, anything else -> -1.
inline int ring_index(const std::string &ring) {
  if (ring == "outer") return 0;
  if (ring == "inner") return 1;
  return -1;
}

// colors: 0xRRGGBB per LED starting at `offset` (clockwise from 12). brightness: optional percent per
// LED (0-100); missing entries mean 100 %. Writes past the end of the ring are dropped.
inline bool set_custom_pixels(const std::string &ring, int offset, const std::vector<int32_t> &colors,
                              const std::vector<int32_t> &brightness = {}) {
  int r = ring_index(ring);
  if (r < 0) return false;
  auto &px = custom_pixels(r);
  for (size_t k = 0; k < colors.size(); k++) {
    int i = offset + (int) k;
    if (i < 0 || i >= (int) px.size()) continue;
    uint32_t c = (uint32_t) colors[k];
    float b = k < brightness.size() ? std::max(0, std::min(100, (int) brightness[k])) / 100.0f : 1.0f;
    px[i] = Color((uint8_t) std::lround(((c >> 16) & 0xFF) * b), (uint8_t) std::lround(((c >> 8) & 0xFF) * b),
                  (uint8_t) std::lround((c & 0xFF) * b));
  }
  return true;
}

// Writes the buffer pre-compensated for the light's gamma, so Custom values come out linear
// ("gamma off"): 50 % of 255 really is half the LED current.
inline uint8_t inverse_gamma(uint8_t v) {
  if (v == 0) return 0;
  return (uint8_t) std::lround(255.0f * std::pow(v / 255.0f, 1.0f / LIGHT_GAMMA));
}

inline void render_custom(AddressableLight &it, int ring) {
  auto &px = custom_pixels(ring);
  for (int i = 0; i < it.size(); i++) {
    Color c = i < (int) px.size() ? px[i] : Color::BLACK;
    it[i] = Color(inverse_gamma(c.r), inverse_gamma(c.g), inverse_gamma(c.b));
  }
}

// --- LED current limiter ------------------------------------------------------------------------

// WS2812B: roughly 20 mA per colour channel at full duty.
static const float MA_PER_CHANNEL = 20.0f;

inline float &limiter_scale() {
  static float scale = 1.0f;
  return scale;
}

// Estimated LED current (mA) of one ring at limiter scale 1.0. Pixel values read back are
// uncorrected; the strip outputs gamma(value * light brightness * scale).
inline float ring_current_ma(esphome::light::LightState *light) {
  auto *out = static_cast<AddressableLight *>(light->get_output());
  float local = light->current_values.is_on() ? light->current_values.get_brightness() : 0.0f;
  if (local <= 0) return 0;
  float sum = 0;
  for (int i = 0; i < out->size(); i++) {
    Color c = (*out)[i].get();
    sum += std::pow(c.r * local / 255.0f, LIGHT_GAMMA) + std::pow(c.g * local / 255.0f, LIGHT_GAMMA) +
           std::pow(c.b * local / 255.0f, LIGHT_GAMMA);
  }
  return sum * MA_PER_CHANNEL;
}

inline void set_ring_scale(esphome::light::LightState *light, float scale) {
  auto *out = static_cast<AddressableLight *>(light->get_output());
  // Read the logical colours with the old correction, then write them back under the new one.
  std::vector<Color> colors(out->size());
  for (int i = 0; i < out->size(); i++) colors[i] = (*out)[i].get();
  out->set_correction(scale, scale, scale);
  for (int i = 0; i < out->size(); i++) (*out)[i] = colors[i];
  out->schedule_show();
}

// Scales both rings down together so their estimated draw stays under `limit_ma`.
// Output current goes with scale^gamma, so the needed scale is (limit / full)^(1/gamma).
inline void limit_power(esphome::light::LightState *outer, esphome::light::LightState *inner, float limit_ma) {
  float full = ring_current_ma(outer) + ring_current_ma(inner);
  float target = full > limit_ma && full > 0 ? std::pow(limit_ma / full, 1.0f / LIGHT_GAMMA) : 1.0f;
  float &scale = limiter_scale();
  if (std::fabs(target - scale) < 0.02f) return;
  scale = target;
  set_ring_scale(outer, scale);
  set_ring_scale(inner, scale);
  ESP_LOGD("ecowatch", "LED limiter: estimated %.0f mA at full scale, scale %.2f", full, scale);
}

inline float estimated_current_ma(esphome::light::LightState *outer, esphome::light::LightState *inner) {
  return (ring_current_ma(outer) + ring_current_ma(inner)) * std::pow(limiter_scale(), LIGHT_GAMMA);
}

// Set from on_boot once the lights exist. The mode selects fire on_value while restoring their
// value during setup, before the lights are set up; starting an effect then crashes.
inline bool &lights_ready() {
  static bool ready = false;
  return ready;
}

// Switches a ring between a built-in display effect and plain manual control.
inline void apply_mode(esphome::light::LightState *light, const std::string &option) {
  if (!lights_ready()) return;
  if (option == "Manual") {
    auto call = light->make_call();
    call.set_effect("None");
    call.perform();
  } else {
    auto call = light->turn_on();
    call.set_effect(option);
    call.perform();
  }
}

}  // namespace ecowatch
