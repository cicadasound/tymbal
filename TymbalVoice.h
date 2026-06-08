#pragma once
#include "daisysp.h"

using namespace daisysp;

// Shared DSP voice used by both the Daisy firmware and the VCV Rack plugin.
// Edit this file to change sound — both targets pick it up automatically.

struct TymbalVoice
{
    // ── DSP objects ───────────────────────────────────────────────────────────

    Adsr         env_1, env_2;
    Svf          filter_l, filter_r;
    ChorusEngine chorus_l, chorus_r;

    // ── Phase accumulators ────────────────────────────────────────────────────

    float phase_1a = 0.f;
    float phase_2a = 0.f;
    float sr_      = 48000.f;

    // ── Internal state ────────────────────────────────────────────────────────

    float env_out_1 = 0.f;
    float env_out_2 = 0.f;
    float ch_depth  = 0.f;
    float ch_rate_l = 0.2f;
    float ch_rate_r = 0.18f;
    float peak_     = 0.f;

    // ── Parameter struct (all values 0–1 normalised) ──────────────────────────

    struct Params
    {
        float attack           = 0.f;
        float release          = 0.25f;
        float cutoff           = 0.8f;
        float resonance        = 0.f;

        // Shape: phase-offset PWM technique.
        //   shape=0 → pure saw, shape=1 → square wave.
        //   CV modulating shape gives the "Hoover" pulsation effect.
        float shape            = 0.f;

        // Chorus: wet/dry blend of stereo chorus (0=dry, 1=full).
        float chorus           = 0.f;

        // filter_cv_amount: attenuates the external filter CV input (0=no CV, 1=full).
        float filter_cv_amount = 0.f;

        // env_mod: how much the envelope modulates filter cutoff.
        float env_mod          = 0.f;

        bool  osc_mode = true;
    };

    struct Output { float left, right, env; };

    // ── Init ──────────────────────────────────────────────────────────────────

    void init(float sr)
    {
        sr_ = sr;

        env_1.Init(sr); env_1.SetAttackTime(0.01f); env_1.SetDecayTime(0.f);
        env_1.SetSustainLevel(1.f); env_1.SetReleaseTime(0.4f);
        env_2.Init(sr); env_2.SetAttackTime(0.01f); env_2.SetDecayTime(0.f);
        env_2.SetSustainLevel(1.f); env_2.SetReleaseTime(0.4f);

        chorus_l.Init(sr); chorus_l.SetLfoFreq(0.2f);  chorus_l.SetLfoDepth(0.f);
        chorus_r.Init(sr); chorus_r.SetLfoFreq(0.18f); chorus_r.SetLfoDepth(0.f);

        filter_l.Init(sr); filter_l.SetFreq(15000.f); filter_l.SetRes(0.1f); filter_l.SetDrive(0.3f);
        filter_r.Init(sr); filter_r.SetFreq(15000.f); filter_r.SetRes(0.1f); filter_r.SetDrive(0.3f);
    }

    // ── Main oscillator path ──────────────────────────────────────────────────

    Output process(const Params& p, float freq_1, float freq_2,
                   bool gate_1, bool gate_2,
                   float filter_cv = 0.f)
    {
        updateEnvTimes(p);
        updateFilter(p, filter_cv);

        // ── Chorus parameters ─────────────────────────────────────────────────
        fonepole(ch_depth,  fmap(p.chorus, 0.f, 0.75f),   0.0002f);
        fonepole(ch_rate_l, fmap(p.chorus, 0.1f, 0.6f),   0.0002f);
        fonepole(ch_rate_r, fmap(p.chorus, 0.09f, 0.52f), 0.0002f);
        chorus_l.SetLfoDepth(ch_depth); chorus_l.SetLfoFreq(ch_rate_l);
        chorus_r.SetLfoDepth(ch_depth); chorus_r.SetLfoFreq(ch_rate_r);

        // ── Envelopes ─────────────────────────────────────────────────────────
        env_out_1 = env_1.Process(gate_1);
        env_out_2 = env_2.Process(gate_2);

        // ── Phase-offset PWM oscillators ──────────────────────────────────────
        float offset   = p.shape * 0.5f;
        float blend    = p.shape;

        phase_1a += freq_1 / sr_;  if (phase_1a >= 1.f) phase_1a -= 1.f;
        phase_2a += freq_2 / sr_;  if (phase_2a >= 1.f) phase_2a -= 1.f;

        float v1a = saw(phase_1a),           v2a = saw(phase_2a);
        float v1b = -saw(phase_1a + offset), v2b = -saw(phase_2a + offset);

        float amp_norm = 1.f + blend - blend * blend;
        float out1 = (v1a + v1b * blend) / amp_norm;
        float out2 = (v2a + v2b * blend) / amp_norm;

        float mix = (out1 * env_out_1 + out2 * env_out_2) * 0.5f;

        // ── Chorus: wet/dry blend ─────────────────────────────────────────────
        float wet_l = chorus_l.Process(mix);
        float wet_r = chorus_r.Process(mix);
        float pre_l = mix + (wet_l - mix) * p.chorus;
        float pre_r = mix + (wet_r - mix) * p.chorus;

        filter_l.Process(pre_l); filter_r.Process(pre_r);
        return limit(filter_l.Low(), filter_r.Low(), env_out_1);
    }

    // ── Audio pass-through path (hardware only) ───────────────────────────────

    Output processPassthrough(const Params& p, float in_l, float in_r,
                              bool gate_1, bool gate_2, float filter_cv = 0.f)
    {
        updateEnvTimes(p);
        updateFilter(p, filter_cv);

        env_out_1 = env_1.Process(gate_1);
        env_out_2 = env_2.Process(gate_2);

        float wet_l = chorus_l.Process(in_l);
        float wet_r = chorus_r.Process(in_r);
        float mix_l = in_l + (wet_l - in_l) * p.chorus;
        float mix_r = in_r + (wet_r - in_r) * p.chorus;

        filter_l.Process(mix_l); filter_r.Process(mix_r);
        return limit(filter_l.Low(), filter_r.Low(), env_out_1);
    }

  private:
    // Peak limiter: instant attack, ~100ms release. Transparent below 0.95.
    Output limit(float l, float r, float env)
    {
        float level = fmaxf(fabsf(l), fabsf(r));
        peak_ = (level > peak_) ? level : peak_ * 0.9998f;
        float gain = (peak_ > 0.95f) ? 0.95f / peak_ : 1.f;
        return { l * gain, r * gain, env };
    }

    static inline float saw(float phase)
    {
        phase -= floorf(phase);
        return 2.f * phase - 1.f;
    }

    void updateEnvTimes(const Params& p)
    {
        float atk = fmap(p.attack,  0.015f, 6.f, Mapping::LOG);
        float rel = fmap(p.release, 0.015f, 6.f, Mapping::LOG);
        env_1.SetAttackTime(atk); env_1.SetReleaseTime(rel);
        env_2.SetAttackTime(atk); env_2.SetReleaseTime(rel);
    }

    void updateFilter(const Params& p, float filter_cv)
    {
        float effective_cv = filter_cv * p.filter_cv_amount;
        float env_mod      = p.env_mod * env_out_1;
        float ff = fmap(fclamp(p.cutoff + effective_cv + env_mod, 0.f, 1.f),
                        30.f, 12000.f, Mapping::LOG);
        filter_l.SetFreq(ff); filter_r.SetFreq(ff);
        filter_l.SetRes(fmap(p.resonance, 0.f, 0.98f, Mapping::LINEAR));
        filter_r.SetRes(fmap(p.resonance, 0.f, 0.98f, Mapping::LINEAR));
    }
};
