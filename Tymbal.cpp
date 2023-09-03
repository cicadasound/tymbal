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
            waveform = Oscillator::WAVE_POLYBLEP_SQUARE;
            break;
        case 4:
            waveform = Oscillator::WAVE_POLYBLEP_TRI;
            break;
        default:
            waveform = Oscillator::WAVE_POLYBLEP_SQUARE;
            break;
    }
    return waveform;
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

DaisyPatchSM patch;

Switch button;
Switch toggle;

AdEnv env_1;
AdEnv env_2;

Oscillator osc_1;
Oscillator osc_2;

MoogLadder filter_l;
MoogLadder filter_r;

ChorusEngine chorus_l;
ChorusEngine chorus_r;

float pitch_knob = 0.5f;
float knob_1_last_value;
float attack_knob;

float fine_knob = 0.5f;
float knob_2_last_value;
float decay_knob;

float cutoff_knob;
float knob_3_last_value;
float chorus_knob = 0.0f;

float resonance_knob;
float knob_4_last_value;
float chorus_rate_knob = 0.0f;

float osc_out_1;
float osc_out_2;
float env_out_1;
float env_out_2;

bool gate_1_triggered = false;
bool gate_2_triggered = false;

float delay_target_l, delay_l;
float delay_target_r, delay_r;
float lfo_target, lfo;

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size) {
    patch.ProcessAllControls();

    toggle.Debounce();
    button.Debounce();

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
        if (shift_mode) {
            fine_knob = knob_2_current_value;
        } else {
            decay_knob = knob_2_current_value;
        }

        knob_2_last_value = knob_2_current_value;
    }

    float fine_offset = fmap(fine_knob, -0.1f, 1.0f);
    
    float voct_cv_1 = patch.GetAdcValue(CV_5);
    float voct_1 = fmap(voct_cv_1, 0.f, 60.f);
    float midi_nn_1 = fclamp(pitch_offset + voct_1 + fine_offset, 0.f, 127.f);
    float osc_freq_1 = mtof(midi_nn_1);

    float voct_cv_2 = patch.GetAdcValue(CV_6);
    float voct_2 = fmap(voct_cv_2, 0.f, 60.f);
    float midi_nn_2 = fclamp(pitch_offset + voct_2 + fine_offset, 0.f, 127.f);
    float osc_freq_2 = mtof(midi_nn_2);

    osc_1.SetFreq(osc_freq_1);
    osc_2.SetFreq(osc_freq_2);

    float decay_time = fmap(decay_knob, 0.005f, 5.0f, Mapping::LOG);
    env_1.SetTime(ADENV_SEG_DECAY, decay_time);
    env_2.SetTime(ADENV_SEG_DECAY, decay_time);

    /** Cutoff and sustain */
    float knob_3_current_value = patch.GetAdcValue(CV_3);
    bool cutoff_changed = ValuesChangedThreshold(knob_3_last_value, knob_3_current_value);
    if (cutoff_changed) {
        if (shift_mode) {
            chorus_knob = knob_3_current_value;
        } else {
            cutoff_knob = knob_3_current_value;
        }

        knob_3_last_value = knob_3_current_value;
    }

    float filter_cv = patch.GetAdcValue(CV_7);
    float filter_frequency = fmap(cutoff_knob + filter_cv, 20.0f, 18000.0f, Mapping::LOG);
    filter_l.SetFreq(filter_frequency);
    filter_r.SetFreq(filter_frequency);

    float osc_amp = fmap(filter_frequency, 0.25, 0.1, Mapping::LOG);
    osc_1.SetAmp(osc_amp);
    osc_2.SetAmp(osc_amp);

    float chorus_depth = fmap(chorus_knob, 0.0f, 1.0f, Mapping::LOG);
    lfo_target = chorus_depth;

    /** Resonance and release */
    float knob_4_current_value = patch.GetAdcValue(CV_4);
    bool resonance_changed = ValuesChangedThreshold(knob_4_last_value, knob_4_current_value);
    if (resonance_changed) {
        if (shift_mode) {
            chorus_rate_knob = knob_4_current_value;
        } else {
            resonance_knob = knob_4_current_value;
        }

        knob_4_last_value = knob_4_current_value;
    }

    float filter_resonance = fmap(resonance_knob, 0.0f, 0.8f, Mapping::LINEAR);
    filter_l.SetRes(filter_resonance);
    filter_r.SetRes(filter_resonance);

    float chorus_cv = patch.GetAdcValue(CV_8);
    float chorus_target = fmap(chorus_rate_knob + chorus_cv, 0.0f, 1.0f, Mapping::LOG);
    delay_target_l = chorus_target;
    delay_target_r = chorus_target * 0.5f;

    for(size_t i = 0; i < size; i++) {
        float dry_l = IN_L[i];
        float dry_r = IN_R[i];

        fonepole(delay_l, delay_target_l, .0001f);
        fonepole(delay_r, delay_target_r, .0001f);
        fonepole(lfo, lfo_target, .0001f);

        chorus_l.SetLfoDepth(lfo);
        chorus_r.SetLfoDepth(lfo);
        chorus_l.SetDelay(delay_l);
        chorus_l.SetDelay(delay_r);

        env_out_1 = env_1.Process();
        env_out_2 = env_2.Process();

        osc_out_1 = osc_1.Process();
        osc_out_2 = osc_2.Process();

        float osc_mix = osc_out_1 + osc_out_2;

        float filter_in_l, filter_in_r;
        if (normal_mode) {
            filter_in_l = chorus_l.Process(osc_mix);
            filter_in_r = chorus_r.Process(osc_mix);
        } else {
           filter_in_l = chorus_l.Process(dry_l);
           filter_in_r = chorus_l.Process(dry_r);
        }

        float wet_l = filter_l.Process(filter_in_l);
        float wet_r = filter_r.Process(filter_in_r);

        patch.WriteCvOut(CV_OUT_BOTH, (env_out_1 * 5.0f));

        OUT_L[i] = wet_l * env_out_1;
        OUT_R[i] = wet_r * env_out_2;
    }
}

int main(void)
{
    patch.Init();

    float sample_rate = patch.AudioSampleRate();

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

    osc_1.SetAmp(0.25f);
    osc_2.SetAmp(0.25f);

    osc_1.SetWaveform(Oscillator::WAVE_TRI);
    osc_2.SetWaveform(Oscillator::WAVE_POLYBLEP_TRI);

    filter_l.Init(sample_rate);
    filter_r.Init(sample_rate);

    filter_l.SetFreq(15000.0f);
    filter_r.SetFreq(15000.0f);
    filter_l.SetRes(0.1f);
    filter_r.SetRes(0.1f);

    chorus_l.Init(sample_rate);
    chorus_r.Init(sample_rate);
    chorus_l.SetLfoFreq(20.0f);
    chorus_r.SetLfoFreq(20.0f);

    delay_target_l = delay_l = 0.0f;
    delay_target_r = delay_r = 0.0f;
    lfo_target = lfo = 0.0f;

    patch.StartAudio(AudioCallback);
    
    while(1) {}
}
