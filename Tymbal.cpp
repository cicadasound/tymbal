#include "daisy_patch_sm.h"
#include "daisysp.h"

using namespace daisy;
using namespace daisysp;
using namespace patch_sm;

#define MAX_SIZE 48000 * 2
float CHANGE_THRESHOLD = 0.002f;

bool ValuesChangedThreshold(float last_value, float new_value) {
    bool is_changed = fabsf(new_value - last_value) > CHANGE_THRESHOLD;
    return is_changed;
}

uint8_t GetWaveform(int index) {
    int waveform;
    switch(index) {
        case 0:
            waveform = Oscillator::WAVE_SQUARE;
            break;
        case 1:
            waveform = Oscillator::WAVE_TRI;
            break;
        case 2:
            waveform = Oscillator::WAVE_SAW;
            break;
        case 3:
            waveform = Oscillator::WAVE_SIN;
            break;
        case 4:
            waveform = Oscillator::WAVE_RAMP;
            break;
        default:
            waveform = Oscillator::WAVE_SQUARE;
            break;
    }
    return waveform;
}

float intervals[8] = {
    -12.0f,
    -7.0f,
    -5.0f,
    0.0f,
    0.0f,
    5.0f,
    7.0f,
    12.0f,
};

DaisyPatchSM patch;

Switch button;
Switch toggle;

Adsr env_1;

Oscillator osc_1;
Oscillator osc_2;

MoogLadder filter_l;
MoogLadder filter_r;

float pitch_knob;
float pitch_knob_last_value;
float attack_knob = 0;

float shape_knob;
float shape_knob_last_value;
float decay_knob = 0;

float cutoff_knob;
float cutoff_knob_last_value;
float sustain_knob = 0;

float resonance_knob;
float resonance_knob_last_value;
float release_knob = 0;

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size) {
    patch.ProcessAllControls();

    toggle.Debounce();
    button.Debounce();

    bool gate_on = patch.gate_in_1.State();

    bool pass_through_mode = toggle.Pressed();
    bool shift_mode = button.Pressed();

    /** Pitch and attack */
    float pitch_knob_current_value = patch.GetAdcValue(CV_1);

    bool pitch_changed = ValuesChangedThreshold(pitch_knob_last_value, pitch_knob_current_value);
    if (pitch_changed) {
        if (shift_mode) {
            attack_knob = pitch_knob_current_value;
        } else {
            pitch_knob = pitch_knob_current_value;
        }

        pitch_knob_last_value = pitch_knob_current_value;
    }   
    float coarse = fmap(pitch_knob, 36.f, 96.f);

    float voct_cv = patch.GetAdcValue(CV_5);
    float voct = fmap(voct_cv, 0.f, 60.f);

    float midi_nn = fclamp(coarse + voct, 0.f, 127.f);
    float osc_freq = mtof(midi_nn);

    osc_1.SetFreq(osc_freq);
    osc_2.SetFreq(osc_freq);

    float attack_time = fmap(attack_knob, 0.005f, 10.0f);
    env_1.SetAttackTime(attack_time);

    /** Shape and decay */
    float shape_knob_current_value = patch.GetAdcValue(CV_2);

    bool shape_changed = ValuesChangedThreshold(shape_knob_last_value, shape_knob);
    if (shape_changed) {
        if (shift_mode) {
            decay_knob = shape_knob_current_value;
        } else {
            shape_knob = shape_knob_current_value;
        }

        shape_knob_last_value = shape_knob_current_value;
    }

    float wave_index = fmap(attack_knob, 0.0f, 4.0f);
    uint8_t waveform = GetWaveform(wave_index);
    osc_1.SetWaveform(waveform);
    osc_2.SetWaveform(waveform);

    float decay_time = fmap(decay_knob, 0.005f, 10.0f);
    env_1.SetDecayTime(decay_time);

    /** Cutoff and sustain */
    float cutoff_knob_current_value = patch.GetAdcValue(CV_3);
    bool cutoff_changed = ValuesChangedThreshold(cutoff_knob_last_value, cutoff_knob);
    if (cutoff_changed) {
        if (shift_mode) {
            sustain_knob = cutoff_knob_current_value;
        } else {
            cutoff_knob = cutoff_knob_current_value;
        }

        cutoff_knob_last_value = cutoff_knob_current_value;
    }

    float filter_frequency = fmap(cutoff_knob, 20.0f, 20000.0f);
    filter_l.SetFreq(filter_frequency);
    filter_r.SetFreq(filter_frequency);

    float sustain_level = fmap(sustain_knob, 0.0f, 1.0f);
    env_1.SetSustainLevel(sustain_level);

    /** Resonance and release */
    float resonance_knob_current_value = patch.GetAdcValue(CV_4);
    bool resonance_changed = ValuesChangedThreshold(resonance_knob_last_value, resonance_knob);
    if (resonance_changed) {
        if (shift_mode) {
            sustain_knob = resonance_knob_current_value;
        } else {
            resonance_knob = resonance_knob_current_value;
        }

        resonance_knob_last_value = resonance_knob_current_value;
    }

    float filter_resonance = fmap(cutoff_knob, 0.0f, 1.0f);
    filter_l.SetRes(filter_resonance);
    filter_r.SetRes(filter_resonance);

    float release_time = fmap(release_knob, 0.005f, 10.0f);
    env_1.SetReleaseTime(release_time);

    for(size_t i = 0; i < size; i++) {
        float dry_l = IN_L[i];
        float dry_r = IN_R[i];

        float osc_out_l = osc_1.Process();
        float osc_out_r = osc_2.Process();

        float filter_in_l, filter_in_r;
        if (pass_through_mode) {
            filter_in_l = dry_l;
            filter_in_r = dry_r;
        } else {
            filter_in_l = osc_out_l;
            filter_in_r = osc_out_r;
        }

        float wet_l = filter_l.Process(filter_in_l);
        float wet_r = filter_r.Process(filter_in_r);

        float env_value = env_1.Process(gate_on);
        patch.WriteCvOut(CV_OUT_BOTH, (env_value * 5.0f));

        OUT_L[i] = wet_l * env_value;
        OUT_R[i] = wet_r * env_value;
    }
}

int main(void)
{
    patch.Init();

    float sample_rate = patch.AudioSampleRate();

    button.Init(patch.B7, sample_rate);
    toggle.Init(patch.B8, sample_rate);

    env_1.Init(sample_rate);

    osc_1.Init(sample_rate);
    osc_2.Init(sample_rate);

    filter_l.Init(sample_rate);
    filter_r.Init(sample_rate);

    patch.StartAudio(AudioCallback);
    
    while(1) {}
}
