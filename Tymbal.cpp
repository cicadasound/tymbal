#include "daisy_patch_sm.h"
#include "daisysp.h"
#include "calibrate.h"
#include "control.h"
#include "settings.h"

using namespace daisy;
using namespace daisysp;
using namespace patch_sm;

// ── Hardware ──────────────────────────────────────────────────────────────────

DaisyPatchSM patch;
PersistentStorage<Settings> storage(patch.qspi);
Calibrate calibration_1, calibration_2;
Switch button, toggle;
Control control_1, control_2, control_3, control_4;

// ── DSP objects ───────────────────────────────────────────────────────────────

Adsr env_1, env_2;
Oscillator osc_1_saw, osc_2_saw, osc_1_square, osc_2_square;
Svf filter_l, filter_r;
Chorus chorus;
ChorusEngine chorus_engine_l, chorus_engine_r;

// ── State ─────────────────────────────────────────────────────────────────────

struct Voice
{
    float attack_knob     = 0.0f;
    float decay_knob      = 0.0f;
    float cutoff_knob     = 0.0f;
    float resonance_knob  = 0.0f;

    float shape_knob   = 0.0f;
    float pwm_knob     = 0.0f;
    float chorus_knob  = 0.0f;
    float env_mod_knob = 0.0f;

    float osc_pw        = 0.5f;
    float osc_pw_target = 0.5f;

    float env_out_1 = 0.0f;
    float env_out_2 = 0.0f;
} voice;

struct Effects
{
    float chorus_lfo            = 0.0f;
    float chorus_lfo_target     = 0.0f;
    float chorus_delay_1        = 0.0f;
    float chorus_delay_target_1 = 0.0f;
    float chorus_delay_2        = 0.0f;
    float chorus_delay_target_2 = 0.0f;
} fx;

struct UI
{
    bool  calibration_mode     = false;
    bool  trigger_save         = false;
    bool  shift_params_changed = false;
    bool  audio_triggered      = false;
    float led_brightness       = 0.0f;
    float last_button_press    = 0.0f;
    float old_in_l             = 0.0f;
    int   button_press_count   = 0;
} ui;

float volume = 0.8f;

// ── Helpers ───────────────────────────────────────────────────────────────────

float Crossfade(float a, float b, float mix)
{
    mix = fminf(fmaxf(mix, 0.0f), 1.0f);
    return (1.0f - mix) * a + mix * b;
}

// ── Audio processing ──────────────────────────────────────────────────────────

struct BlockParams
{
    float osc_freq_1;
    float osc_freq_2;
    float shape_value;
    bool  gate_1;
    bool  gate_2;
    bool  osc_mode;
    bool  pass_mode;
};

BlockParams ProcessControls(Settings &settings)
{
    toggle.Debounce();
    button.Debounce();
    bool shift_released = button.FallingEdge();
    bool button_pressed = button.RisingEdge();
    bool shift_mode     = button.Pressed();
    bool osc_mode       = toggle.Pressed();

    float voct_1 = patch.GetAdcValue(CV_5);
    float voct_2 = patch.GetAdcValue(CV_6);

    /** Calibration processing */
    if (ui.calibration_mode)
    {
        bool done_1 = calibration_1.ProcessCalibration(voct_1, button_pressed);
        bool done_2 = calibration_2.ProcessCalibration(voct_2, button_pressed);
        if (done_1 && done_2)
        {
            calibration_1.cal.GetData(settings.scale_1, settings.offset_1);
            calibration_2.cal.GetData(settings.scale_2, settings.offset_2);

            // If calibration was done in wrong order (3V before 1V), scale is negative.
            // Mathematically correct: new_offset = 48 - old_offset, new_scale = -old_scale.
            if (settings.scale_1 < 0) {
                settings.scale_1 = -settings.scale_1;
                settings.offset_1 = 48.0f - settings.offset_1;
                calibration_1.cal.SetData(settings.scale_1, settings.offset_1);
            }
            if (settings.scale_2 < 0) {
                settings.scale_2 = -settings.scale_2;
                settings.offset_2 = 48.0f - settings.offset_2;
                calibration_2.cal.SetData(settings.scale_2, settings.offset_2);
            }

            ui.trigger_save     = true;
            ui.calibration_mode = false;
        }
        ui.led_brightness = calibration_1.GetBrightness();
    }

    /** Calibration entry: 5 rapid taps */
    float time_now = System::GetNow();
    if (time_now - ui.last_button_press >= 1000.0f && ui.button_press_count != 0)
        ui.button_press_count = 0;
    else if (button_pressed && !ui.calibration_mode)
    {
        ui.last_button_press = time_now;
        ui.button_press_count++;
    }

    if (ui.button_press_count >= 5 && !ui.calibration_mode)
    {
        ui.button_press_count = 0;
        ui.calibration_mode   = true;
        ui.led_brightness     = 1.0f;
        calibration_1.Start();
        calibration_2.Start();
    }

    /** Gates */
    bool gate_1 = patch.gate_in_1.State();
    bool gate_2 = patch.gate_in_2.State();
    dsy_gpio_write(&patch.gate_out_1, gate_1);
    dsy_gpio_write(&patch.gate_out_2, gate_2);

    /** Knobs — normal and shift pairs */
    float prev_shape   = voice.shape_knob;
    float prev_pwm     = voice.pwm_knob;
    float prev_chorus  = voice.chorus_knob;
    float prev_env_mod = voice.env_mod_knob;

    float cv1 = patch.GetAdcValue(CV_1);
    float cv2 = patch.GetAdcValue(CV_2);
    float cv3 = patch.GetAdcValue(CV_3);
    float cv4 = patch.GetAdcValue(CV_4);

    control_1.Process(shift_mode, cv1, voice.attack_knob,    voice.shape_knob);
    control_2.Process(shift_mode, cv2, voice.decay_knob,     voice.pwm_knob);
    control_3.Process(shift_mode, cv3, voice.cutoff_knob,    voice.chorus_knob);
    control_4.Process(shift_mode, cv4, voice.resonance_knob, voice.env_mod_knob);

    /** Shift param persistence */
    if (shift_mode && (voice.shape_knob   != prev_shape   ||
                       voice.pwm_knob     != prev_pwm     ||
                       voice.chorus_knob  != prev_chorus  ||
                       voice.env_mod_knob != prev_env_mod))
        ui.shift_params_changed = true;

    if (shift_released && ui.shift_params_changed)
    {
        settings.shape_knob   = voice.shape_knob;
        settings.pwm_knob     = voice.pwm_knob;
        settings.chorus_knob  = voice.chorus_knob;
        settings.env_mod_knob = voice.env_mod_knob;
        ui.trigger_save       = true;
        ui.shift_params_changed = false;
    }

    /** Envelope */
    float attack_time  = fmap(voice.attack_knob, 0.015f, 6.0f, Mapping::LOG);
    float release_time = fmap(voice.decay_knob,  0.015f, 6.0f, Mapping::LOG);
    env_1.SetAttackTime(attack_time);
    env_2.SetAttackTime(attack_time);
    env_1.SetReleaseTime(release_time);
    env_2.SetReleaseTime(release_time);

    /** Shape: square→saw crossfade */
    float shape_value = voice.shape_knob;

    /** PWM: pulse width, CV8-modulatable, 0 = 50% (square), 1 = very narrow */
    float pwm_value = fclamp(voice.pwm_knob + patch.GetAdcValue(CV_8), 0.0f, 1.0f);
    voice.osc_pw_target = fmap(pwm_value, 0.5f, 0.05f);

    /** Filter — cutoff with CV and envelope modulation */
    float filter_cv  = patch.GetAdcValue(CV_7);
    float env_mod    = voice.env_mod_knob * voice.env_out_1;
    float filter_freq = fmap(fclamp(voice.cutoff_knob + filter_cv + env_mod, 0.0f, 1.0f),
                             80.0f, 18000.0f, Mapping::LOG);
    filter_l.SetFreq(filter_freq);
    filter_r.SetFreq(filter_freq);
    filter_l.SetRes(fmap(voice.resonance_knob, 0.0f, 0.98f, Mapping::LINEAR));
    filter_r.SetRes(fmap(voice.resonance_knob, 0.0f, 0.98f, Mapping::LINEAR));

    /** Oscillator frequencies */
    float midi_nn_1 = fclamp(calibration_1.cal.ProcessInput(voct_1), 0.f, 127.f);
    float midi_nn_2 = fclamp(calibration_2.cal.ProcessInput(voct_2), 0.f, 127.f);

    /** Chorus */
    float chorus_rate = 1.0f - voice.chorus_knob;
    fx.chorus_lfo_target     = fmap(voice.chorus_knob, 0.0f, 0.8f);
    fx.chorus_delay_target_1 = fmap(chorus_rate, 0.1f, 0.5f);
    fx.chorus_delay_target_2 = fmap(chorus_rate, 0.3f, 0.75f);
    chorus.SetPan(0.5f - voice.chorus_knob * 0.4f, 0.5f + voice.chorus_knob * 0.4f);

    return {
        mtof(midi_nn_1),
        mtof(midi_nn_2),
        shape_value,
        gate_1,
        gate_2,
        osc_mode,
        !osc_mode
    };
}

void ProcessSample(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out,
                   size_t i, const BlockParams &p)
{
    float dry_l = IN_L[i];
    float dry_r = IN_R[i];

    if (ui.old_in_l - dry_l > 0.001f)
        ui.audio_triggered = true;
    ui.old_in_l = dry_l;

    /** Chorus smoothing */
    fonepole(fx.chorus_delay_1, fx.chorus_delay_target_1, .0001f);
    fonepole(fx.chorus_delay_2, fx.chorus_delay_target_2, .0001f);
    fonepole(fx.chorus_lfo,     fx.chorus_lfo_target,     .0001f);

    chorus.SetLfoDepth(fx.chorus_lfo);
    chorus.SetLfoFreq(fx.chorus_delay_1, fx.chorus_delay_2);
    chorus_engine_l.SetLfoDepth(fx.chorus_lfo);
    chorus_engine_l.SetLfoFreq(fx.chorus_delay_1);
    chorus_engine_r.SetLfoDepth(fx.chorus_lfo);
    chorus_engine_r.SetLfoFreq(fx.chorus_delay_2);

    /** Envelopes */
    voice.env_out_1 = env_1.Process(p.gate_1);
    voice.env_out_2 = env_2.Process(p.gate_2);

    /** Oscillators */
    fonepole(voice.osc_pw, voice.osc_pw_target, .0001f);
    osc_1_square.SetPw(voice.osc_pw);
    osc_2_square.SetPw(voice.osc_pw);

    osc_1_square.SetFreq(p.osc_freq_1);
    osc_2_square.SetFreq(p.osc_freq_2);
    osc_1_saw.SetFreq(p.osc_freq_1);
    osc_2_saw.SetFreq(p.osc_freq_2);

    float sq1  = osc_1_square.Process();
    float sq2  = osc_2_square.Process();
    float saw1 = osc_1_saw.Process();
    float saw2 = osc_2_saw.Process();

    /** Waveshape morph (square → saw) */
    float osc_1_out = Crossfade(sq1, saw1, p.shape_value);
    float osc_2_out = Crossfade(sq2, saw2, p.shape_value);

    float osc_mix = (osc_1_out * voice.env_out_1) + (osc_2_out * voice.env_out_2);

    /** Routing: osc mode uses stereo chorus, pass mode uses per-channel chorus */
    float pre_filter_l, pre_filter_r;
    if (p.osc_mode)
    {
        chorus.Process(osc_mix);
        pre_filter_l = chorus.GetLeft();
        pre_filter_r = chorus.GetRight();
    }
    else
    {
        pre_filter_l = chorus_engine_l.Process(dry_l);
        pre_filter_r = chorus_engine_r.Process(dry_r);
    }

    filter_l.Process(pre_filter_l);
    filter_r.Process(pre_filter_r);

    float wet_l = filter_l.Low();
    float wet_r = filter_r.Low();

    if (!ui.audio_triggered && p.pass_mode)
    {
        OUT_L[i] = voice.env_out_1;
        OUT_R[i] = voice.env_out_2;
    }
    else
    {
        OUT_L[i] = wet_l * volume;
        OUT_R[i] = wet_r * volume;
    }
}

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    patch.ProcessAllControls();
    Settings &settings = storage.GetSettings();
    BlockParams p = ProcessControls(settings);

    for (size_t i = 0; i < size; i++)
        ProcessSample(in, out, i, p);

    patch.WriteCvOut(CV_OUT_BOTH, ui.calibration_mode ? ui.led_brightness * 5.0f
                                                      : voice.env_out_1 * 5.0f);
}

// ── Initialisation ────────────────────────────────────────────────────────────

void InitDSP(float sample_rate)
{
    env_1.Init(sample_rate);
    env_1.SetAttackTime(0.01f);
    env_1.SetDecayTime(0.0f);
    env_1.SetSustainLevel(1.0f);
    env_1.SetReleaseTime(0.4f);

    env_2.Init(sample_rate);
    env_2.SetAttackTime(0.01f);
    env_2.SetDecayTime(0.0f);
    env_2.SetSustainLevel(1.0f);
    env_2.SetReleaseTime(0.4f);

    osc_1_saw.Init(sample_rate);
    osc_2_saw.Init(sample_rate);
    osc_1_square.Init(sample_rate);
    osc_2_square.Init(sample_rate);
    osc_1_square.SetAmp(0.15f);
    osc_2_square.SetAmp(0.15f);
    osc_1_saw.SetAmp(0.2f);
    osc_2_saw.SetAmp(0.2f);
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
}

int main(void)
{
    patch.Init();
    float sample_rate = patch.AudioSampleRate();

    storage.Init(default_settings);
    if (storage.GetState() == PersistentStorage<Settings>::State::FACTORY)
        storage.RestoreDefaults();

    Settings &settings = storage.GetSettings();

    if (settings.scale_1 < 0) {
        settings.scale_1 = -settings.scale_1;
        settings.offset_1 = 48.0f - settings.offset_1;
    }
    if (settings.scale_2 < 0) {
        settings.scale_2 = -settings.scale_2;
        settings.offset_2 = 48.0f - settings.offset_2;
    }
    calibration_1.cal.SetData(settings.scale_1, settings.offset_1);
    calibration_2.cal.SetData(settings.scale_2, settings.offset_2);

    auto load_knob = [](float v) { return (v >= 0.0f && v <= 1.0f) ? v : 0.0f; };
    voice.shape_knob   = load_knob(settings.shape_knob);
    voice.pwm_knob     = load_knob(settings.pwm_knob);
    voice.chorus_knob  = load_knob(settings.chorus_knob);
    voice.env_mod_knob = load_knob(settings.env_mod_knob);

    button.Init(patch.B7, sample_rate);
    toggle.Init(patch.B8, sample_rate);

    InitDSP(sample_rate);

    patch.StartAudio(AudioCallback);

    while (1)
    {
        patch.Delay(1);
        if (ui.trigger_save)
        {
            storage.Save();
            ui.trigger_save = false;
        }
    }
}
