#include "daisy_patch_sm.h"
#include "daisysp.h"
#include "calibrate.h"
#include "control.h"

using namespace daisy;
using namespace daisysp;
using namespace patch_sm;

#define MAX_SIZE 48000 * 2
float ZERO_RANGE = 0.002f;
float DETUNE_RANGE = 0.005f;

float last_button_press;

inline float Crossfade(float a, float b, float fade)
{
    return a + (b - a) * fade;
}

float IndexToBrightness(int index, int total)
{
    return static_cast<float>(index + 1) / static_cast<float>(total);
}

float GetTuningOffset(int index)
{
    float offset;
    switch (index)
    {
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
        offset = 12.0f;
        break;
    case 4:
        offset = 24.0f;
        break;
    case 5:
        offset = 36.0f;
        break;
    default:
        offset = 12.0f;
        break;
    }
    return offset;
}

struct Settings
{
    float volume;
    float fine_tune;
    float scale_1;
    float offset_1;
    float scale_2;
    float offset_2;

    bool operator==(const Settings &rhs)
    {
        return volume == rhs.volume &&
               fine_tune == rhs.fine_tune &&
               scale_1 == rhs.scale_1 &&
               offset_1 == rhs.offset_1 &&
               scale_2 == rhs.scale_2 &&
               offset_2 == rhs.offset_2;
    }
    bool operator!=(const Settings &rhs) { return !operator==(rhs); }
};

Settings default_settings{
    0.8f, // volume
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

Oscillator osc_1_saw;
Oscillator osc_2_saw;
Oscillator osc_1_square;
Oscillator osc_2_square;

Svf filter_l;
Svf filter_r;

Compressor compressor;
Chorus chorus;
ChorusEngine chorus_engine_l;
ChorusEngine chorus_engine_r;

Control control_1;
Control control_2;
Control control_3;
Control control_4;

float volume = 0.8f;

bool audio_triggered = false;
float old_in_l, old_in_r;

float knob_1_last_value;
float attack_knob;
float attack_knob_pickup = 0.0f;
float pitch_knob = 0.5f;
float fine_knob = 0.5f;

float knob_2_last_value;
float decay_knob;
float decay_knob_pickup = 0.0f;
float shape_knob = 0.0f;

float knob_3_last_value;
float cutoff_knob;
float cutoff_knob_pickup = 0.0f;
float chorus_knob = 0.0f;

float knob_4_last_value;
float resonance_knob;
float resonance_knob_pickup = 0.0f;
float modulate_knob = 0.0f;

float osc_out_l;
float osc_out_r;

float osc_pw_l = 0.2f;
float osc_pw_target_l = 0.2f;
float osc_pw_r = 0.3f;
float osc_pw_target_r = 0.3f;

bool shape_triggered = false;
bool shape_knob_changed = false;
float old_shape_in;
float old_shape_knob;
float shape_attenuation = 1.0f;

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

float chorus_delay_target_1 = 0.0f;
float chorus_delay_1 = 0.0f;
float chorus_delay_target_2 = 0.0f;
float chorus_delay_2 = 0.0f;
float chorus_lfo_target = 0.0f;
float chorus_lfo = 0.0f;

int button_press_count = 0;

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    patch.ProcessAllControls();

    Settings &settings_data = storage.GetSettings();

    if (!settings_initialized)
    {
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

    if (calibration_mode)
    {
        bool calibration_1_complete = calibration_1.ProcessCalibration(voct_1_input, button_pressed);
        bool calibration_2_complete = calibration_2.ProcessCalibration(voct_2_input, button_pressed);
        if (calibration_1_complete && calibration_2_complete)
        {
            calibration_1.cal.GetData(settings_data.scale_1, settings_data.offset_1);
            calibration_2.cal.GetData(settings_data.scale_2, settings_data.offset_2);
            trigger_save = true;
            calibration_mode = false;
        }
        led_brightness = calibration_1.GetBrightness();
    }

    float time_now = System::GetNow();
    float time_between_presses = time_now - last_button_press;

    if (time_between_presses >= 1000 && button_press_count != 0)
    {
        button_press_count = 0;
    }
    else if (button_pressed && !calibration_mode)
    {
        last_button_press = System::GetNow();
        button_press_count++;
    }

    if (button_press_count >= 5 && !calibration_mode)
    {
        button_press_count = 0;
        calibration_mode = true;
        led_brightness = 1.0f;
        calibration_1.Start();
        calibration_2.Start();
    }

    if (patch.gate_in_1.Trig())
    {
        gate_1_triggered = true;
        env_1.Trigger();
    }

    if (patch.gate_in_2.Trig())
    {
        gate_2_triggered = true;
        env_2.Trigger();
    }

    dsy_gpio_write(&patch.gate_out_1, patch.gate_in_1.State());
    dsy_gpio_write(&patch.gate_out_2, patch.gate_in_2.State());

    bool osc_mode = toggle.Pressed();
    bool pass_mode = !osc_mode;
    bool shift_mode = button.Pressed();

    /** Attack and octave */
    float knob_1_current_value = patch.GetAdcValue(CV_1);
    control_1.Process(shift_mode, knob_1_current_value, attack_knob, pitch_knob);

    float coarse = fmap(pitch_knob, 0, 5);
    pitch_offset = GetTuningOffset(coarse);
    float attack_time = fmap(attack_knob, 0.015f, 6.0f, Mapping::LOG);
    env_1.SetTime(ADENV_SEG_ATTACK, attack_time);
    env_2.SetTime(ADENV_SEG_ATTACK, attack_time);

    /** Decay and shape */
    float knob_2_current_value = patch.GetAdcValue(CV_2);
    control_2.Process(shift_mode, knob_2_current_value, decay_knob, shape_knob);

    float decay_time = fmap(decay_knob, 0.015f, 6.0f, Mapping::LOG);
    env_1.SetTime(ADENV_SEG_DECAY, decay_time);
    env_2.SetTime(ADENV_SEG_DECAY, decay_time);

    float shape_cv = patch.GetAdcValue(CV_8);

    if (old_shape_in - shape_cv > 0.01f)
    {
        shape_triggered = true;
    }

    if (old_shape_knob - shape_knob > 0.01f)
    {
        shape_knob_changed = true;
    }

    float shape_attenuation;

    if (shape_knob_changed)
    {
        shape_attenuation = fclamp(shape_knob, 0.1f, 1.0f);
    }
    else
    {
        shape_attenuation = 1.0f;
    }

    float shape_value;

    if (shape_triggered)
    {
        shape_value = fmap(shape_cv * shape_attenuation, 0.0f, 2.0f, Mapping::LINEAR);
    }
    else
    {
        shape_value = fmap(shape_cv + shape_knob, 0.0f, 2.0f, Mapping::LINEAR);
    }

    osc_pw_target_l = fclamp(shape_value, 0.2f, 0.8f);
    osc_pw_target_r = fclamp(shape_value, 0.3f, 0.95f);

    /** Cutoff and chorus depth */
    float knob_3_current_value = patch.GetAdcValue(CV_3);
    control_3.Process(shift_mode, knob_3_current_value, cutoff_knob, chorus_knob);

    chorus_lfo_target = fmap(chorus_knob, 0.0f, 0.8f);

    float filter_cv = patch.GetAdcValue(CV_7);
    float filter_frequency = fmap(cutoff_knob + filter_cv, 80.0f, 18000.0f, Mapping::LOG);
    filter_l.SetFreq(filter_frequency);
    filter_r.SetFreq(filter_frequency);

    float note_1 = calibration_1.cal.ProcessInput(voct_1_input);
    float midi_nn_1 = fclamp(note_1 + pitch_offset, 0.f, 127.f);
    float osc_freq_1 = mtof(midi_nn_1);

    float note_2 = calibration_2.cal.ProcessInput(voct_2_input);
    float midi_nn_2 = fclamp(note_2 + pitch_offset, 0.f, 127.f);
    float osc_freq_2 = mtof(midi_nn_2);

    /** Resonance and chorus rate */
    float knob_4_current_value = patch.GetAdcValue(CV_4);
    control_4.Process(shift_mode, knob_4_current_value, resonance_knob, modulate_knob);

    float filter_resonance = fmap(resonance_knob, 0.0f, 0.98f, Mapping::LINEAR);
    filter_l.SetRes(filter_resonance);
    filter_r.SetRes(filter_resonance);

    chorus_delay_target_1 = fmap(modulate_knob, 0.1f, 0.5f);
    chorus_delay_target_2 = fmap(modulate_knob, 0.3f, 0.75f);
    float k = modulate_knob * 0.5f;
    chorus.SetPan(0.5f - k, 0.5f + k);

    for (size_t i = 0; i < size; i++)
    {
        float dry_l = IN_L[i];
        float dry_r = IN_R[i];

        if (old_in_l - dry_l > 0.001f)
        {
            audio_triggered = true;
        }

        old_in_l = dry_l;
        old_in_r = dry_r;

        fonepole(chorus_delay_1, chorus_delay_target_1, .0001f);
        fonepole(chorus_delay_2, chorus_delay_target_2, .0001f);
        fonepole(chorus_lfo, chorus_lfo_target, .0001f);

        chorus.SetLfoDepth(chorus_lfo);
        chorus.SetLfoFreq(chorus_delay_1, chorus_delay_2);
        chorus_engine_l.SetLfoDepth(chorus_lfo);
        chorus_engine_l.SetLfoFreq(chorus_delay_1);
        chorus_engine_r.SetLfoDepth(chorus_lfo);
        chorus_engine_r.SetLfoFreq(chorus_delay_2);

        env_out_1 = env_1.Process();
        env_out_2 = env_2.Process();

        fonepole(osc_pw_l, osc_pw_target_l, .0001f);
        fonepole(osc_pw_r, osc_pw_target_r, .0001f);

        osc_1_square.SetPw(osc_pw_l);
        osc_2_square.SetPw(osc_pw_r);

        osc_1_square.SetFreq(osc_freq_1);
        osc_1_saw.SetFreq(osc_freq_1);
        osc_2_square.SetFreq(osc_freq_2);
        osc_2_saw.SetFreq(osc_freq_2);

        float osc_1_square_processed = osc_1_square.Process();
        float osc_2_square_processed = osc_2_square.Process();
        float osc_1_saw_processed = osc_1_saw.Process();
        float osc_2_saw_processed = osc_2_saw.Process();

        float osc_1_out = Crossfade(osc_1_square_processed, osc_1_saw_processed, shape_value / 2.0f);
        float osc_2_out = Crossfade(osc_2_square_processed, osc_2_saw_processed, shape_value / 2.0f);

        float osc_mix = (osc_2_out * env_out_2) + (osc_1_out * env_out_1);

        float compressor_in_l, compressor_in_r;

        if (osc_mode)
        {
            chorus.Process(osc_mix);
            compressor_in_l = chorus.GetLeft();
            compressor_in_r = chorus.GetRight();
        }
        else
        {
            compressor_in_l = chorus_engine_l.Process(dry_l);
            compressor_in_r = chorus_engine_l.Process(dry_r);
        }

        // Compress the output
        float compressed_l = compressor.Process(compressor_in_l);
        float compressed_r = compressor.Process(compressor_in_r);

        filter_l.Process(compressed_l);
        filter_r.Process(compressed_r);

        float wet_l = filter_l.Low();
        float wet_r = filter_r.Low();

        if (calibration_mode)
        {
            patch.WriteCvOut(CV_OUT_BOTH, led_brightness * 5.0f);
        }
        else
        {
            patch.WriteCvOut(CV_OUT_BOTH, (env_out_1 * 5.0f));
        }

        if (!audio_triggered && pass_mode)
        {
            OUT_L[i] = env_out_1;
            OUT_R[i] = env_out_2;
        }
        else
        {
            OUT_L[i] = wet_l * volume;
            OUT_R[i] = wet_r * volume;
        }
    }
}

int main(void)
{
    patch.Init();

    float sample_rate = patch.AudioSampleRate();

    storage.Init(default_settings);

    if (storage.GetState() == PersistentStorage<Settings>::State::FACTORY)
    {
        // Update the user values with the defaults.
        storage.RestoreDefaults();
    }

    /** Restore settings from previous power cycle */
    Settings &settings_data = storage.GetSettings();

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

    osc_1_saw.Init(sample_rate);
    osc_2_saw.Init(sample_rate);
    osc_1_square.Init(sample_rate);
    osc_2_square.Init(sample_rate);

    osc_1_square.SetAmp(0.015f);
    osc_2_square.SetAmp(0.015f);
    osc_1_saw.SetAmp(0.015f);
    osc_2_saw.SetAmp(0.015f);

    osc_1_square.SetWaveform(Oscillator::WAVE_POLYBLEP_SQUARE);
    osc_2_square.SetWaveform(Oscillator::WAVE_POLYBLEP_SQUARE);
    osc_1_saw.SetWaveform(Oscillator::WAVE_POLYBLEP_SAW);
    osc_2_saw.SetWaveform(Oscillator::WAVE_POLYBLEP_SAW);

    chorus.Init(sample_rate);
    chorus.SetLfoFreq(0.1f * 0.3f * 20.0f);
    chorus_engine_l.Init(sample_rate);
    chorus_engine_l.SetLfoFreq(0.1f * 0.3f * 20.0f);
    chorus_engine_r.Init(sample_rate);
    chorus_engine_r.SetLfoFreq(0.1f * 0.3f * 20.0f);

    filter_l.Init(sample_rate);
    filter_l.SetFreq(15000.0f);
    filter_l.SetRes(0.1f);
    filter_l.SetDrive(0.6);
    filter_r.Init(sample_rate);
    filter_r.SetFreq(15000.0f);
    filter_r.SetRes(0.1f);
    filter_r.SetDrive(0.6);

    compressor.Init(sample_rate);
    compressor.SetAttack(0.01f);
    compressor.SetMakeup(12.0f);
    compressor.SetRatio(4.0f);
    compressor.SetRelease(7.0f);
    compressor.SetThreshold(-12.0f);

    patch.StartAudio(AudioCallback);

    while (1)
    {
        patch.Delay(1);
        if (trigger_save)
        {
            storage.Save();
            trigger_save = false;
        }
    }
}
