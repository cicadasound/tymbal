#include "daisy_patch_sm.h"
#include "daisysp.h"

using namespace daisy;
using namespace daisysp;
using namespace patch_sm;

#define MAX_SIZE 48000 * 2
float CHANGE_THRESHOLD = 0.002f;
float ZERO_RANGE = 0.002f;
float DETUNE_RANGE = 0.005f;

bool ValuesChangedThreshold(float last_value, float new_value) {
    bool is_changed = fabsf(new_value - last_value) > CHANGE_THRESHOLD;
    return is_changed;
}

inline float Crossfade(float a, float b, float fade) {
    return a + (b - a) * fade;
}

float GetTuningOffset(int index) {
    float offset;
    switch(index) {
        case 0:
            offset = 0.0f;
            break;
        case 1:
            offset = 12.0f;
            break;
        case 2:
            offset = 24.0f;
            break;
        case 3:
            offset = 36.0f;
            break;
        default:
            offset = 0.0f;
            break;
    }
    return offset;
}

struct Settings {
    float volume;
    float fine_tune;

    bool operator==(const Settings& rhs) {
        return volume == rhs.volume && fine_tune == rhs.fine_tune;
    }
    bool operator!=(const Settings& rhs) { return !operator==(rhs); }
};

Settings default_settings{
    0.3f, // volume
    0.0f, // fine_tune
};

DaisyPatchSM patch;

PersistentStorage<Settings> storage(patch.qspi);

Switch button;
Switch toggle;

AdEnv env_1;
AdEnv env_2;

Oscillator osc_1;
Oscillator osc_2;
Oscillator osc_1_detune;
Oscillator osc_2_detune;
Oscillator osc_1_square;
Oscillator osc_2_square;

MoogLadder filter_l;
MoogLadder filter_r;

Compressor compressor;

Oscillator led_lfo;

float main_volume = 0.5f;

float knob_1_last_value;
float attack_knob;
float pitch_knob = 0.5f;
float fine_knob = 0.5f;

float knob_2_last_value;
float decay_knob;
float shape_knob = 0.0f;
float volume_knob = 0.5f;

float knob_3_last_value;
float cutoff_knob;
float filter_env_knob = 0.0f;

float knob_4_last_value;
float resonance_knob;
float modulate_knob = 0.0f;

float osc_out_l;
float osc_out_r;

float env_out_1;
float env_out_2;

bool gate_1_triggered = false;
bool gate_2_triggered = false;

bool settings_mode = false;
bool trigger_save = false;
bool settings_initialized = false;

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size) {
    patch.ProcessAllControls();

    Settings& settings_data = storage.GetSettings();

    if (!settings_initialized) {
        volume_knob = settings_data.volume;
        fine_knob = settings_data.fine_tune;
        settings_initialized = true;
    }

    toggle.Debounce();
    button.Debounce();

    float voct_1_input = patch.GetAdcValue(CV_5);
    float voct_2_input = patch.GetAdcValue(CV_6);

    if (button.RisingEdge() && settings_mode) {
        settings_mode = false;
        settings_data.fine_tune = fine_knob;
        settings_data.volume = volume_knob;
        trigger_save = true;
    }

    if (button.TimeHeldMs() >= 3000.0f && !settings_mode) {
        settings_mode = true;
    }

    if (patch.gate_in_1.Trig()) {
        gate_1_triggered = true;
        env_1.Trigger();
    }

    if (patch.gate_in_2.Trig()) {
        gate_2_triggered = true;
        env_2.Trigger();
    }

    bool normal_mode = toggle.Pressed();
    bool shift_mode = button.Pressed();

    /** Pitch and attack */
    float knob_1_current_value = patch.GetAdcValue(CV_1);

    bool knob_1_changed = ValuesChangedThreshold(knob_1_last_value, knob_1_current_value);
    if (knob_1_changed) {
        if (settings_mode) {
            fine_knob = knob_1_current_value;
        } else if (shift_mode) {
            pitch_knob = knob_1_current_value;
        } else {
            attack_knob = knob_1_current_value;
        }

        knob_1_last_value = knob_1_current_value;
    }

    float coarse = fmap(pitch_knob, 0, 4);
    float pitch_offset = GetTuningOffset(coarse);

    float attack_time = fmap(attack_knob, 0.005f, 5.0f, Mapping::LOG);
    env_1.SetTime(ADENV_SEG_ATTACK, attack_time);
    env_2.SetTime(ADENV_SEG_ATTACK, attack_time);

    /** Shape and decay */
    float knob_2_current_value = patch.GetAdcValue(CV_2);

    bool knob_2_changed = ValuesChangedThreshold(knob_2_last_value, knob_2_current_value);
    if (knob_2_changed) {
        if (settings_mode) {
            volume_knob = knob_2_current_value;
        } else if (shift_mode) {
            shape_knob = knob_2_current_value;
        } else {
            decay_knob = knob_2_current_value;
        }

        knob_2_last_value = knob_2_current_value;
    }

    float fine_offset = fmap(fine_knob, -0.1f, 1.0f);

    float decay_time = fmap(decay_knob, 0.005f, 5.0f, Mapping::LOG);
    env_1.SetTime(ADENV_SEG_DECAY, decay_time);
    env_2.SetTime(ADENV_SEG_DECAY, decay_time);

    /** Cutoff and sustain */
    float knob_3_current_value = patch.GetAdcValue(CV_3);
    bool cutoff_changed = ValuesChangedThreshold(knob_3_last_value, knob_3_current_value);
    if (cutoff_changed) {
        if (shift_mode) {
            filter_env_knob = knob_3_current_value;
        } else {
            cutoff_knob = knob_3_current_value;
        }

        knob_3_last_value = knob_3_current_value;
    }

    float filter_cv = patch.GetAdcValue(CV_7);
    float filter_frequency = fmap(cutoff_knob + filter_cv, 20.0f, 18000.0f, Mapping::LOG);
    filter_l.SetFreq(filter_frequency);
    filter_r.SetFreq(filter_frequency);

    float shape_value = fmap(shape_knob, 0.0f, 1.0f, Mapping::LINEAR);

    float voct_cv_1 = voct_1_input;
    float voct_1 = fmap(voct_cv_1, 0.f, 60.f);
    float midi_nn_1 = fclamp(pitch_offset + voct_1 + fine_offset, 0.f, 127.f);
    float osc_freq_1 = mtof(midi_nn_1);

    float voct_cv_2 = voct_2_input;
    float voct_2 = fmap(voct_cv_2, 0.f, 60.f);
    float midi_nn_2 = fclamp(pitch_offset + voct_2 + fine_offset, 0.f, 127.f);
    float osc_freq_2 = mtof(midi_nn_2);

    osc_1.SetFreq(osc_freq_1);
    osc_1_square.SetFreq(osc_freq_1);
    osc_2.SetFreq(osc_freq_2);
    osc_2_square.SetFreq(osc_freq_2);

    /** Resonance and release */
    float knob_4_current_value = patch.GetAdcValue(CV_4);
    bool resonance_changed = ValuesChangedThreshold(knob_4_last_value, knob_4_current_value);
    if (resonance_changed) {
        if (shift_mode) {
            modulate_knob = knob_4_current_value;
        } else {
            resonance_knob = knob_4_current_value;
        }

        knob_4_last_value = knob_4_current_value;
    }

    float filter_resonance = fmap(resonance_knob, 0.0f, 0.8f, Mapping::LINEAR);
    filter_l.SetRes(filter_resonance);
    filter_r.SetRes(filter_resonance);

    float volume = fmap(volume_knob, 0.0f, 1.0f, Mapping::LINEAR);

    for(size_t i = 0; i < size; i++) {
        float dry_l = IN_L[i];
        float dry_r = IN_R[i];

        env_out_1 = env_1.Process();
        env_out_2 = env_2.Process();

        float osc_1_out = Crossfade(osc_1.Process(), osc_1_square.Process(), shape_value);
        float osc_2_out = Crossfade(osc_2.Process(), osc_2_square.Process(), shape_value);
        float osc_mix = (osc_1_out * env_out_1) + (osc_2_out * env_out_2);

        osc_out_l = osc_mix;
        osc_out_r = osc_mix;

        float compressor_in_l, compressor_in_r;
        if (normal_mode) {
            compressor_in_l = osc_out_l;
            compressor_in_r = osc_out_r;
        } else {
           compressor_in_l = dry_l;
           compressor_in_r = dry_r;
        }

        /** Run the reverb trail through the compressor */
        float compressed_l = compressor.Process(compressor_in_l);
        float compressed_r = compressor.Process(compressor_in_r);

        float wet_l = filter_l.Process(compressed_l);
        float wet_r = filter_r.Process(compressed_r);

        if (settings_mode) {
            float led_lfo_value = led_lfo.Process();
            patch.WriteCvOut(CV_OUT_BOTH, (led_lfo_value * 5.0f));
        } else {
            patch.WriteCvOut(CV_OUT_BOTH, (env_out_1 * 5.0f));
        }

        OUT_L[i] = wet_l * volume;
        OUT_R[i] = wet_r * volume;
    }
}

int main(void)
{
    patch.Init();

    float sample_rate = patch.AudioSampleRate();

    storage.Init(default_settings);
    if(storage.GetState() == PersistentStorage<Settings>::State::FACTORY) {
        // Update the user values with the defaults.
        storage.RestoreDefaults();
    }

    /** Restore settings from previous power cycle */
    // Settings& settings_data = storage.GetSettings();

    button.Init(patch.B7, sample_rate);
    toggle.Init(patch.B8, sample_rate);

    env_1.Init(sample_rate);
    env_1.SetTime(ADENV_SEG_ATTACK, 0.01f);
    env_1.SetTime(ADENV_SEG_DECAY, 0.4f);
    env_1.SetMin(0.0f);
    env_1.SetMax(10.0f);
    env_1.SetCurve(0);

    env_2.Init(sample_rate);
    env_2.SetTime(ADENV_SEG_ATTACK, 0.01f);
    env_2.SetTime(ADENV_SEG_DECAY, 0.4f);
    env_2.SetMin(0.0f);
    env_2.SetMax(10.0f);
    env_2.SetCurve(0);

    osc_1.Init(sample_rate);
    osc_2.Init(sample_rate);
    osc_1_detune.Init(sample_rate);
    osc_2_detune.Init(sample_rate);
    osc_1_square.Init(sample_rate);
    osc_2_square.Init(sample_rate);

    osc_1.SetAmp(0.1f);
    osc_2.SetAmp(0.1f);
    osc_1_detune.SetAmp(0.1f);
    osc_2_detune.SetAmp(0.1f);
    osc_1_square.SetAmp(0.015f);
    osc_1_square.SetAmp(0.015f);

    osc_1.SetWaveform(Oscillator::WAVE_POLYBLEP_TRI);
    osc_2.SetWaveform(Oscillator::WAVE_POLYBLEP_TRI);
    osc_1_detune.SetWaveform(Oscillator::WAVE_POLYBLEP_TRI);
    osc_2_detune.SetWaveform(Oscillator::WAVE_POLYBLEP_TRI);
    osc_1_square.SetWaveform(Oscillator::WAVE_POLYBLEP_SQUARE);
    osc_2_square.SetWaveform(Oscillator::WAVE_POLYBLEP_SQUARE);
    
    // osc_1_square.SetPw(0.25f);
    // osc_2_square.SetPw(0.75f);

    led_lfo.Init(sample_rate);
    led_lfo.SetWaveform(Oscillator::WAVE_SQUARE);
    led_lfo.SetAmp(1.0f);
    led_lfo.SetFreq(5.0f);

    filter_l.Init(sample_rate);
    filter_r.Init(sample_rate);

    filter_l.SetFreq(15000.0f);
    filter_r.SetFreq(15000.0f);
    filter_l.SetRes(0.1f);
    filter_r.SetRes(0.1f);

    compressor.Init(sample_rate);
    compressor.SetAttack(0.01f);
    compressor.SetMakeup(12.0f);
    compressor.SetRatio(4.0f);
    compressor.SetRelease(7.0f);
    compressor.SetThreshold(-12.0f);

    patch.StartAudio(AudioCallback);
    
    while(1) {
        patch.Delay(1);
        if (trigger_save) {
            storage.Save();
            trigger_save = false;
        }
    }
}
