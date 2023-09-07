#include "daisy_patch_sm.h"
#include "daisysp.h"
#include "calibrate.h"

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

float IndexToBrightness(int index, int total) {
    return static_cast<float>(index + 1) / static_cast<float>(total);
}

float GetTuningOffset(int index) {
    float offset;
    switch(index) {
        case 0:
            offset = -12.0f;
            break;
        case 1:
            offset = 0.0f;
            break;
        case 2:
            offset = 12.0f;
            break;
        case 3:
            offset = 24.0f;
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
    float scale_1;
    float offset_1;
    float scale_2;
    float offset_2;

    bool operator==(const Settings& rhs) {
        return volume == rhs.volume && 
            fine_tune == rhs.fine_tune &&
            scale_1 == rhs.scale_1 &&
            offset_1 == rhs.offset_1 &&
            scale_2 == rhs.scale_2 &&
            offset_2 == rhs.offset_2;
    }
    bool operator!=(const Settings& rhs) { return !operator==(rhs); }
};

Settings default_settings{
    0.3f, // volume
    0.0f, // fine_tune
};

DaisyPatchSM patch;

PersistentStorage<Settings> storage(patch.qspi);

Calibrate calibration_1;
Calibrate calibration_2;

Switch button;
Switch toggle;

AdEnv env_1;
AdEnv env_2;

Oscillator osc_1;
Oscillator osc_2;
Oscillator osc_1_saw;
Oscillator osc_2_saw;
Oscillator osc_1_square;
Oscillator osc_2_square;

MoogLadder filter_l;
MoogLadder filter_r;

Compressor compressor;

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

bool calibration_mode = false;
bool trigger_save = false;
bool settings_initialized = false;
float led_brightness = 0.0f;

float scale_1;
float offset_1;
float scale_2;
float offset_2;

float pitch_offset = 0.0f;

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size) {
    patch.ProcessAllControls();

    Settings& settings_data = storage.GetSettings();

    if (!settings_initialized) {
        scale_1 = settings_data.scale_1;
        offset_1 = settings_data.offset_1;
        scale_2 = settings_data.scale_2;
        offset_2 = settings_data.offset_2;
        settings_initialized = true;
    }

    toggle.Debounce();
    button.Debounce();

    float voct_1_input = patch.GetAdcValue(CV_5);
    float voct_2_input = patch.GetAdcValue(CV_6);

    bool button_pressed = button.RisingEdge();

    if (calibration_mode) {
        bool calibration_1_complete = calibration_1.ProcessCalibration(voct_1_input, button_pressed);
        bool calibration_2_complete = calibration_2.ProcessCalibration(voct_2_input, button_pressed);
        if (calibration_1_complete && calibration_2_complete) {
            calibration_1.cal.GetData(settings_data.scale_1, settings_data.offset_1);
            calibration_2.cal.GetData(settings_data.scale_2, settings_data.offset_2);
            trigger_save = true;
            calibration_mode = false;
        }
        led_brightness = calibration_1.GetBrightness();
    }

    if (button.TimeHeldMs() >= 5000.0f && !calibration_mode) {
        calibration_mode = true;
        led_brightness = 1.0f;
        calibration_1.Start();
        calibration_2.Start();
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
        if (shift_mode) {
            pitch_knob = knob_1_current_value;
            float coarse = fmap(pitch_knob, 0, 4);
            pitch_offset = GetTuningOffset(coarse);
        } else {
            attack_knob = knob_1_current_value;
        }

        knob_1_last_value = knob_1_current_value;
    }

    float attack_time = fmap(attack_knob, 0.005f, 5.0f, Mapping::LOG);
    env_1.SetTime(ADENV_SEG_ATTACK, attack_time);
    env_2.SetTime(ADENV_SEG_ATTACK, attack_time);

    /** Shape and decay */
    float knob_2_current_value = patch.GetAdcValue(CV_2);

    bool knob_2_changed = ValuesChangedThreshold(knob_2_last_value, knob_2_current_value);
    if (knob_2_changed) {
        if (shift_mode) {
            shape_knob = knob_2_current_value;
        } else {
            decay_knob = knob_2_current_value;
        }

        knob_2_last_value = knob_2_current_value;
    }

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

    float shape_value = fmap(shape_knob, 0.0f, 2.0f, Mapping::LINEAR);

    // float voct_1 = fmap(voct_1_input, 0.f, 60.0f);
    // float pitch_offset_value = fmap(pitch_offset, 0.f, 60.0f);
    float note_1 = calibration_1.cal.ProcessInput(voct_1_input);
    float midi_nn_1 = fclamp(note_1 + pitch_offset, 0.f, 127.f);
    float osc_freq_1 = mtof(midi_nn_1);

    // float voct_2 = fmap(voct_2_input, 0.f, 60.0f);
    float note_2 = calibration_2.cal.ProcessInput(voct_2_input);
    float midi_nn_2 = fclamp(note_2 + pitch_offset, 0.f, 127.f);
    float osc_freq_2 = mtof(midi_nn_2);

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

        osc_1.SetFreq(osc_freq_1);
        osc_1_square.SetFreq(osc_freq_1);
        osc_1_saw.SetFreq(osc_freq_1);
        osc_2.SetFreq(osc_freq_2);
        osc_2_square.SetFreq(osc_freq_2);
        osc_2_saw.SetFreq(osc_freq_2);


        float osc_1_processed = osc_1.Process();
        float osc_2_processed = osc_2.Process();
        float osc_1_square_processed = osc_1_square.Process();
        float osc_2_square_processed = osc_2_square.Process();
        float osc_1_saw_processed = osc_1_saw.Process();
        float osc_2_saw_processed = osc_2_saw.Process();

        float osc_1_out, osc_2_out;

        if (shape_value < 1.0f) {
            osc_1_out = Crossfade(osc_1_processed, osc_1_square_processed, shape_value);
            osc_2_out = Crossfade(osc_2_processed, osc_2_square_processed, shape_value);
        } else {
            osc_1_out = Crossfade(osc_1_square_processed, osc_1_saw_processed, shape_value - 1.0f);
            osc_2_out = Crossfade(osc_2_square_processed, osc_2_saw_processed, shape_value - 1.0f);
        }

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

        if (calibration_mode) {
            patch.WriteCvOut(CV_OUT_BOTH, led_brightness * 5.0f);
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
    Settings& settings_data = storage.GetSettings();

    calibration_1.cal.SetData(settings_data.scale_1, settings_data.offset_1);
    calibration_2.cal.SetData(settings_data.scale_2, settings_data.offset_2);

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
    osc_1_saw.Init(sample_rate);
    osc_2_saw.Init(sample_rate);
    osc_1_square.Init(sample_rate);
    osc_2_square.Init(sample_rate);

    osc_1.SetAmp(0.1f);
    osc_2.SetAmp(0.1f);
    osc_1_square.SetAmp(0.015f);
    osc_1_square.SetAmp(0.015f);
    osc_1_saw.SetAmp(0.015f);
    osc_1_saw.SetAmp(0.015f);

    osc_1.SetWaveform(Oscillator::WAVE_POLYBLEP_TRI);
    osc_2.SetWaveform(Oscillator::WAVE_POLYBLEP_TRI);
    osc_1_square.SetWaveform(Oscillator::WAVE_POLYBLEP_SQUARE);
    osc_2_square.SetWaveform(Oscillator::WAVE_POLYBLEP_SQUARE);
    osc_1_saw.SetWaveform(Oscillator::WAVE_POLYBLEP_SAW);
    osc_2_saw.SetWaveform(Oscillator::WAVE_POLYBLEP_SAW);

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
