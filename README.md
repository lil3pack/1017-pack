# 1017 PACK

Pack de deux plugins audio VST3/AU pour producteurs trap, construits en JUCE.

- **TRAP HOUSE** — hard clipper avec soft knee + harmonics pour donner chaleur et volume aux prods et masters.
- **LEMONADE** — chaîne vocale 4 presets (Lead / Adlib / Ghost / Zay) pour adlibs et backs.

## Stack

- **Framework** : JUCE 7.0.10+ (recommandé dernière stable)
- **Build system** : CMake 3.22+
- **Formats** : VST3, AU (macOS), Standalone
- **C++** : C++20

## Setup rapide

### 1. Cloner JUCE quelque part sur ta machine

```bash
git clone --depth 1 --branch master https://github.com/juce-framework/JUCE.git ~/JUCE
```

### 2. Configurer le build

Depuis le dossier `1017Pack/` :

```bash
cmake -B build -DJUCE_DIR=~/JUCE -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
```

### 3. Récupérer les plugins

Après build, les `.vst3` (et `.component` sur macOS) sont dans :

```
build/TrapHouse/TrapHouse_artefacts/Release/VST3/TRAP HOUSE.vst3
build/Lemonade/Lemonade_artefacts/Release/VST3/LEMONADE.vst3
```

Sur macOS ils sont auto-copiés dans `~/Library/Audio/Plug-Ins/VST3/` et `Components/`.
Sur Windows, copier manuellement dans `C:\Program Files\Common Files\VST3\`.

### 4. FL Studio

Rescan les plugins : Options → File settings → Manage plugins → Find plugins.

## Structure

```
1017Pack/
├── CMakeLists.txt          # Root, déclare les deux sous-projets
├── TrapHouse/              # Plugin clipper
│   ├── CMakeLists.txt
│   └── Source/
│       ├── PluginProcessor.{h,cpp}
│       ├── PluginEditor.{h,cpp}
│       └── dsp/
│           └── Clipper.h
├── Lemonade/               # Plugin vocal chains (stub)
│   ├── CMakeLists.txt
│   └── Source/
└── Shared/                 # Look & feel commun "1017"
    └── LookAndFeel1017.h
```

## Roadmap MVP

- [x] Setup projet JUCE/CMake
- [x] DSP clipper : hard clip + soft knee + harmonics + oversampling 4x
- [x] Paramètres AudioProcessorValueTreeState (auto-safe, preset-safe)
- [x] UI squelette (knobs + meters, theming plus tard)
- [ ] Look & Feel custom 1017 (purple + gold depuis les mockups)
- [ ] Oscilloscope waveform dans TRAP HOUSE
- [ ] DSP LEMONADE (pitch shift, reverb, comp, de-ess) via `juce::dsp`
- [ ] Preset manager avec fichiers `.xml`
- [ ] Code signing + installer (Pro Tools AAX = licence PACE si ciblé plus tard)

## Notes DSP

### TRAP HOUSE

- Oversampling 4x (FIR polyphase, latence compensée via `setLatencySamples`).
- Formule de clipping : `y = (1 - k) * hard_clip(x) + k * soft_clip(x)` où k = soft knee amount.
- Harmonics : générateur Chebyshev (2e + 3e harmonique) en parallèle, dosage via knob.
- Auto-gain makeup optionnel (post-clip normalisation RMS).

### LEMONADE

- 4 chains preset-hardcodées initialement, chaque chain = `juce::dsp::ProcessorChain`.
- Pitch shifter : `juce::dsp::PhaseVocoder` (ou SoundTouch si latence acceptable).
- Reverb : `juce::dsp::Reverb`.
- Comp : VCA-style, ratio fixe par chain.

## Ressources

- [JUCE docs](https://docs.juce.com/master/index.html)
- [JUCE tutorials](https://juce.com/learn/tutorials)
- [The Audio Programmer YouTube](https://www.youtube.com/@TheAudioProgrammer) — meilleur canal gratuit pour JUCE
