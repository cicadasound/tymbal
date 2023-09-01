# Tymbal

A stereo synth voice, with some tricks up its sleave.

## Controls

| Pin Name | Pin Location | Function | Comment                                          |
| -------- | ------------ | -------- | ------------------------------------------------ |
| CV_1     | C5           | Mix      | Controls balance between wet and dry signals     |
| CV_2     | C4           | Time     | Controls the decay of reverb                     |
| CV_3     | C3           | Damping  | Controls the damping on the reverb               |
| CV_4     | C2           | Pitch    | Controls the pitch shifting in musical intervals |

## Development

1. Install the [Daisy Toolchain](https://github.com/electro-smith/DaisyWiki/wiki/1.-Setting-Up-Your-Development-Environment#1-install-the-toolchain)
1. Clone the repository `git clone --recursive https://github.com/cicadasound/daisy-pitch-verb.git`
1. Change into the directory `cd daisy-pitch-verb`
1. Setup dependencies `./build_libs.sh`
1. Compile the firmware `make`
1. Load onto patch.init module with [Daisy Web Programmer](https://electro-smith.github.io/Programmer/)
