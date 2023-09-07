# Tymbal

A stereo synth voice, with some tricks up its sleave.

## Controls

| Pin Name | Pin Location | Function  | Comment                      |
| -------- | ------------ | --------- | ---------------------------- |
| CV_1     | C5           | Attack    | Controls envelope attack     |
| CV_2     | C4           | Decay     | Controls envelope decay      |
| CV_3     | C3           | Cutoff    | Controls cutoff of filter    |
| CV_4     | C2           | Resonance | Controls resonance of filter |

### Shift mode

| Pin Name | Pin Location | Function     | Comment                            |
| -------- | ------------ | ------------ | ---------------------------------- |
| CV_1     | C5           | Octave       | Moves the notes up or down octaves |
| CV_2     | C4           | Shape        | Controls shape of waveform         |
| CV_3     | C3           | Chorus Depth | Controls depth of chorus           |
| CV_4     | C2           | Chorus Rate  | Controls speed of chorus           |

## Calibration

The hardware needs to be calibrated to correctly track v/oct pitch.

To re-calibrate your hardware:

- Press and hold shift for 5 seconds to start the calibration. The LED will light and then start to blink.
- First give it a 1v (C1) signal to cv_5 and cv_6 and then press the shift button.
- The LED will start to flash more quickly and is ready for the next step.
- Give it a 3v (C3) signal to cv_5 and cv_6 and press the button again.
- It will now return to normal mode with new calibration settings.

## Development

1. Install the [Daisy Toolchain](https://github.com/electro-smith/DaisyWiki/wiki/1.-Setting-Up-Your-Development-Environment#1-install-the-toolchain)
1. Clone the repository `git clone --recursive https://github.com/cicadasound/daisy-pitch-verb.git`
1. Change into the directory `cd daisy-pitch-verb`
1. Setup dependencies `./build_libs.sh`
1. Compile the firmware `make`
1. Load onto patch.init module with [Daisy Web Programmer](https://electro-smith.github.io/Programmer/)
