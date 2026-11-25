// DeauthGuardian.cpp — see DeauthGuardian.h
#include "DeauthGuardian.h"

#if HALE_DEAUTH_GUARDIAN

namespace hale {

static inline bool macEq(const uint8_t* a, const uint8_t* b) {
  return a && b && std::memcmp(a, b, 6) == 0;
}

void DeauthGuardian::begin(uint16_t suspect_pps, uint16_t attack_pps,
                           uint16_t window_ms, uint16_t cooldown_ms) {
  suspect_pps_ = suspect_pps;
  attack_pps_  = (attack_pps > suspect_pps) ? attack_pps : (uint16_t)(suspect_pps + 1);
  // Clamp window to the compiled ring size.
  uint16_t max_window = (uint16_t)MAX_BUCKETS * BUCKET_MS;
  window_ms_   = (window_ms == 0 || window_ms > max_window) ? max_window : window_ms;
  cooldown_ms_ = cooldown_ms;
  reset();
}

void DeauthGuardian::reset() {
  if (suspect_pps_ == 0) suspect_pps_ = HALE_DG_SUSPECT_PPS;
  if (attack_pps_  == 0) attack_pps_  = HALE_DG_ATTACK_PPS;
  if (window_ms_   == 0) window_ms_   = HALE_DG_WINDOW_MS;
  if (cooldown_ms_ == 0) cooldown_ms_ = HALE_DG_COOLDOWN_MS;

  std::memset(buckets_, 0, sizeof(buckets_));
  bucket_head_   = 0;
  bucket_time_   = 0;
  window_total_  = 0;

  std::memset(attackers_, 0, sizeof(attackers_));
  attacker_count_ = 0;

  std::memset(channel_count_, 0, sizeof(channel_count_));

  level_       = DG_SECURE;
  calm_since_  = 0;

  total_frames_   = 0;
  total_disassoc_ = 0;

  event_pending_ = false;
  std::memset(&event_, 0, sizeof(event_));
}

// Roll the ring forward to `now`, zeroing buckets for elapsed time and keeping
// window_total_ in sync. Buckets older than the window are dropped.
void DeauthGuardian::advanceWindow(uint32_t now) {
  if (bucket_time_ == 0) {          // first use
    bucket_time_ = now;
    return;
  }
  // Number of whole buckets elapsed since the current bucket started.
  uint32_t elapsed = now - bucket_time_;
  uint32_t steps   = elapsed / BUCKET_MS;
  if (steps == 0) return;
  if (steps > MAX_BUCKETS) steps = MAX_BUCKETS;  // full flush

  for (uint32_t i = 0; i < steps; i++) {
    bucket_head_ = (uint8_t)((bucket_head_ + 1) % MAX_BUCKETS);
    window_total_ -= buckets_[bucket_head_];     // subtract the slot we overwrite
    buckets_[bucket_head_] = 0;                  // this slot becomes "now"
  }
  bucket_time_ += steps * BUCKET_MS;
}

DeauthThreat DeauthGuardian::classify(uint16_t pps) const {
  if (pps >= attack_pps_)  return DG_UNDER_ATTACK;
  if (pps >= suspect_pps_) return DG_SUSPECT;
  return DG_SECURE;
}

uint16_t DeauthGuardian::ratePerSec() const {
  if (window_ms_ == 0) return 0;
  return (uint16_t)(((uint32_t)window_total_ * 1000u) / window_ms_);
}

DeauthAttacker* DeauthGuardian::findOrInsert(const uint8_t* ta, uint32_t now) {
  for (uint8_t i = 0; i < attacker_count_; i++) {
    if (macEq(attackers_[i].mac, ta)) return &attackers_[i];
  }
  if (attacker_count_ < MAX_ATTACKERS) {
    DeauthAttacker* a = &attackers_[attacker_count_++];
    std::memset(a, 0, sizeof(*a));
    std::memcpy(a->mac, ta, 6);
    return a;
  }
  // Table full: evict the least-recently-seen entry.
  uint8_t  oldest = 0;
  uint32_t oldest_t = attackers_[0].last_seen;
  for (uint8_t i = 1; i < attacker_count_; i++) {
    if (attackers_[i].last_seen < oldest_t) { oldest_t = attackers_[i].last_seen; oldest = i; }
  }
  DeauthAttacker* a = &attackers_[oldest];
  std::memset(a, 0, sizeof(*a));
  std::memcpy(a->mac, ta, 6);
  (void)now;
  return a;
}

void DeauthGuardian::recordFrame(const uint8_t* ta, const uint8_t* ra,
                                 const uint8_t* bssid, uint8_t channel,
                                 int8_t rssi, bool is_disassoc, uint32_t now) {
  advanceWindow(now);

  buckets_[bucket_head_]++;
  window_total_++;
  total_frames_++;
  if (is_disassoc) total_disassoc_++;

  if (channel < CHANNELS) channel_count_[channel]++;

  if (ta) {
    DeauthAttacker* a = findOrInsert(ta, now);
    if (a) {
      a->count++;
      a->last_channel = channel;
      a->last_rssi    = rssi;
      a->last_seen    = now;
      if (bssid) std::memcpy(a->bssid, bssid, 6);
    }
  }
  (void)ra;  // reserved: victim-centric views can use RA later
}

uint8_t DeauthGuardian::busiestChannel() const {
  uint8_t  best = 0;
  uint16_t best_n = 0;
  for (uint8_t ch = 1; ch < CHANNELS; ch++) {
    if (channel_count_[ch] > best_n) { best_n = channel_count_[ch]; best = ch; }
  }
  return best;
}

const DeauthAttacker* DeauthGuardian::topAttacker() const {
  if (attacker_count_ == 0) return nullptr;
  const DeauthAttacker* best = &attackers_[0];
  for (uint8_t i = 1; i < attacker_count_; i++) {
    if (attackers_[i].count > best->count) best = &attackers_[i];
  }
  return best;
}

bool DeauthGuardian::evaluate(uint32_t now) {
  advanceWindow(now);                 // expire stale buckets even with no frames

  uint16_t     pps       = ratePerSec();
  DeauthThreat raw_level = classify(pps);
  DeauthThreat prev      = level_;

  if (raw_level > level_) {
    // Rising threat: react immediately.
    level_      = raw_level;
    calm_since_ = 0;
  } else if (raw_level < level_) {
    // Falling: apply cooldown hysteresis before downgrading.
    if (calm_since_ == 0) calm_since_ = now;
    if (now - calm_since_ >= cooldown_ms_) {
      level_      = raw_level;
      calm_since_ = 0;
    }
  } else {
    calm_since_ = 0;                  // steady at current level
  }

  if (level_ != prev) {
    if (level_ > prev) {              // only alert on escalation
      event_.level    = level_;
      event_.rate_pps = pps;
      event_.channel  = busiestChannel();
      event_.when     = now;
      const DeauthAttacker* t = topAttacker();
      if (t) event_.top = *t; else std::memset(&event_.top, 0, sizeof(event_.top));
      event_pending_ = true;
    }
    return true;
  }
  return false;
}

bool DeauthGuardian::popEvent(DeauthEvent& out) {
  if (!event_pending_) return false;
  out = event_;
  event_pending_ = false;
  return true;
}

}  // namespace hale

#endif  // HALE_DEAUTH_GUARDIAN
