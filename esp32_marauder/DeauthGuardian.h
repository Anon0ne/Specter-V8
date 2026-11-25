// DeauthGuardian.h — Blue Team deauth/disassoc flood analyzer for MarauderHale
//
// Self-contained detection core: sliding-window rate, attacker->victim
// attribution, per-channel tally, SECURE/SUSPECT/UNDER-ATTACK verdict with
// hysteresis, and edge-triggered alert events for logging.
//
// Deliberately dependency-light (only <cstdint>/<cstring>) and UI/IO-free:
// the caller feeds parsed 802.11 management frames via recordFrame() and reads
// state for display; SD/GPS logging is done by the caller from popEvent().
// This keeps the analyzer host-compilable and board-agnostic.
//
// Scope: Marauder V8 built-in WiFi only. Clean-room — behavior modeled on
// HaleHound's "WiFi Guardian" README description; no HaleHound code used.
// See THIRD_PARTY_NOTICE.md.

#pragma once

#include "hale_features.h"

#if HALE_DEAUTH_GUARDIAN

#include <cstdint>
#include <cstring>

namespace hale {

enum DeauthThreat : uint8_t {
  DG_SECURE       = 0,
  DG_SUSPECT      = 1,
  DG_UNDER_ATTACK = 2,
};

// One tracked transmitter of deauth/disassoc frames.
struct DeauthAttacker {
  uint8_t  mac[6];      // transmitter address (TA)
  uint8_t  bssid[6];    // last associated BSSID seen
  uint16_t count;       // total frames attributed since reset
  uint8_t  last_channel;
  int8_t   last_rssi;
  uint32_t last_seen;   // ms
};

// Edge-triggered event emitted when the threat level rises.
struct DeauthEvent {
  DeauthThreat   level;
  DeauthAttacker top;        // top attacker at the moment of the event
  uint16_t       rate_pps;   // frames/sec over the window
  uint8_t        channel;    // channel with the most activity
  uint32_t       when;       // ms
};

class DeauthGuardian {
 public:
  static constexpr uint8_t  MAX_ATTACKERS = 16;
  static constexpr uint8_t  BUCKET_MS     = 250;                         // window resolution
  static constexpr uint8_t  MAX_BUCKETS   = HALE_DG_WINDOW_MS / BUCKET_MS; // ring size
  static constexpr uint8_t  CHANNELS      = 15;                          // index 1..14

  DeauthGuardian() { reset(); }

  // Configure thresholds (frames/sec) and timing. Safe to call before use.
  void begin(uint16_t suspect_pps = HALE_DG_SUSPECT_PPS,
             uint16_t attack_pps  = HALE_DG_ATTACK_PPS,
             uint16_t window_ms   = HALE_DG_WINDOW_MS,
             uint16_t cooldown_ms = HALE_DG_COOLDOWN_MS);

  void reset();

  // Feed one parsed deauth (0xC0) or disassoc (0xA0) management frame.
  // ta/ra/bssid are 6-byte MACs (ra/bssid may be nullptr if unknown).
  void recordFrame(const uint8_t* ta, const uint8_t* ra, const uint8_t* bssid,
                   uint8_t channel, int8_t rssi, bool is_disassoc, uint32_t now);

  // Advance the window and recompute the verdict. Returns true when the
  // threat level changed this call (rising edge also queues an event).
  bool evaluate(uint32_t now);

  // ---- state for the UI ----
  DeauthThreat threatLevel()   const { return level_; }
  uint16_t     framesInWindow()const { return window_total_; }
  uint16_t     ratePerSec()    const;
  uint16_t     totalFrames()   const { return total_frames_; }
  uint16_t     disassocFrames()const { return total_disassoc_; }
  uint8_t      busiestChannel()const;
  uint16_t     channelCount(uint8_t ch) const {
    return (ch < CHANNELS) ? channel_count_[ch] : 0;
  }
  uint8_t              attackerCount() const { return attacker_count_; }
  const DeauthAttacker* attacker(uint8_t i) const {
    return (i < attacker_count_) ? &attackers_[i] : nullptr;
  }
  const DeauthAttacker* topAttacker() const;

  // ---- edge-triggered events for SD/GPS logging ----
  bool hasPendingEvent() const { return event_pending_; }
  bool popEvent(DeauthEvent& out);

 private:
  void advanceWindow(uint32_t now);
  DeauthThreat classify(uint16_t pps) const;
  DeauthAttacker* findOrInsert(const uint8_t* ta, uint32_t now);

  // thresholds / timing
  uint16_t suspect_pps_;
  uint16_t attack_pps_;
  uint16_t window_ms_;
  uint16_t cooldown_ms_;

  // sliding window ring of frame counts
  uint16_t buckets_[MAX_BUCKETS];
  uint8_t  bucket_head_;       // index of the current (newest) bucket
  uint32_t bucket_time_;       // start time of the current bucket
  uint16_t window_total_;      // cached sum of buckets_

  // attribution
  DeauthAttacker attackers_[MAX_ATTACKERS];
  uint8_t        attacker_count_;

  // per-channel tally (lifetime since reset)
  uint16_t channel_count_[CHANNELS];

  // verdict + hysteresis
  DeauthThreat level_;
  uint32_t     calm_since_;    // ms the rate first dropped below suspect

  // counters
  uint16_t total_frames_;
  uint16_t total_disassoc_;

  // pending alert event
  bool        event_pending_;
  DeauthEvent event_;
};

}  // namespace hale

#endif  // HALE_DEAUTH_GUARDIAN
