# C-Synth — Project Todolist

> **Legend:** `[ ]` not started · `[~]` in progress · `[x]` done · `[!]` blocked

---

## Status: What Already Exists

| Module | File(s) | State |
|--------|---------|-------|
| App skeleton (SDL3 callbacks) | `app.c / app.h` | [x] |
| Logger (JSONL + ANSI, SDL mutex) | `log.c / log.h` | [x] |
| HashMap (djb2 chaining) | `hashmap.c / hashmap.h` | [x] |
| ArgParse (long/short/positional, `_Generic` macro) | `argparse.c / argparse.h` | [x] |
| MIDI message types (pack/unpack, PortMidi bridge) | `midi.h` | [x] |
| CMake (git metadata, version header, SDL3, PortMidi) | `CMakeLists.txt` | [x] |
| Synth engine | `synth.c / synth.h` | [ ] |
| UI framework | — | [~] |
| Render backends | — | [~] |

---

## Phase 1 — UI Framework

### 1.1 Core Abstraction Layer

- [x] Define `ui_renderer_t` interface struct with function pointers:
  - `init(width, height, title)` → `bool`
  - `begin_frame()` / `end_frame()`
  - `draw_rect(x, y, w, h, color)`
  - `draw_text(x, y, str, color)`
  - `draw_line(x1, y1, x2, y2, color)`
  - `draw_image(x, y, w, h, texture_handle)`
  - `create_texture(pixels, w, h)` → `ui_texture_t`
  - `destroy_texture(ui_texture_t)`
  - `shutdown()`
- [x] Define `ui_color_t` as packed `uint32_t` (RGBA8) with helper macros `UI_RGBA(r,g,b,a)`, `UI_RGB(r,g,b)`
- [x] Define `ui_rect_t`, `ui_vec2_t`, `ui_font_t` as shared types across backends
- [x] Write `ui_renderer_registry`: runtime registration of named backends (e.g. `"sdl"`, `"opengl"`, `"vulkan"`)
- [x] CLI flag `--renderer <name>` wired through argparse → `App_t`

### 1.2 SDL GPU / Software Backend (Primary)

- [x] Implement SDL3 GPU backend (`ui_backend_sdl.c`):
  - Use `SDL_GPUDevice` + `SDL_GPURenderPass` (SDL3's new GPU API)
  - Rectangle / line / text batching into a single draw call per frame
  - Fall back to `SDL_Renderer` (software) if GPU device creation fails
- [x] Embed a minimal bitmap font (e.g. 8×8 or Terminus) compiled as a C array → `ui_font_builtin.h`
- [x] Glyph atlas upload as GPU texture on init
- [ ] Support for vector based fonts
- [ ] font registry to manage fonts
- [ ] Frame timing / vsync toggle (`--vsync on|off`)

### 1.3 OpenGL Backend

- [ ] Implement `ui_backend_opengl.c`:
  - OpenGL 3.3 core profile (GLAD or manual loader — no extra lib deps preferred)
  - Orthographic projection UBO updated per frame
  - Single VBO with dynamic upload for rect/line/text geometry
  - One uber-shader: vertex + fragment, driven by a `mode` uniform (rect / text / textured)
- [ ] `SDL_GL_CreateContext` managed by `app.c`; context passed to backend
- [ ] Texture atlas for font glyphs shared with SDL backend via `ui_font_t`

### 1.4 Vulkan Backend (Stretch)

- [ ] Implement `ui_backend_vulkan.c`:
  - Instance / device / swapchain creation via SDL3 Vulkan helpers
  - Single render pass, one pipeline per draw mode
  - Staging buffer → device-local vertex buffer per frame
  - Descriptor set for font atlas sampler
- [ ] Abstract synchronisation (semaphores, fences) behind `begin_frame` / `end_frame`
- [ ] Validation layers auto-enabled in Debug builds via `VK_LAYER_KHRONOS_validation`
- [ ] CMake option `ENABLE_VULKAN` (default OFF)

### 1.5 Widget System

- [x] `ui_ctx_t` — retained state context (focus, hover, active widget ID)
- [x] Widget ID system: `ui_id_t` = `uint64_t` hash of string label + parent scope
- [~] Basic widgets:
  - [x] `ui_button(ctx, label)` → `bool` (clicked)
  - [x] `ui_label(ctx, text)`
  - [x] `ui_slider_f(ctx, label, value*, min, max)` → `bool` (changed)
  - [~] `ui_slider_i(ctx, label, value*, min, max)` → `bool`
  - [x] `ui_knob(ctx, label, value*, min, max)` — rotary control, mouse drag
  - [x] `ui_toggle(ctx, label, value*)` — on/off LED button
  - [ ] `ui_dropdown(ctx, label, items[], count, selected*)` → `bool`
  - [ ] `ui_text_input(ctx, label, buf, buf_len)` → `bool`
  - [ ] `ui_separator(ctx)`
  - [ ] `ui_scope(ctx, label)` / `ui_scope_end(ctx)` — group / panel
- [~] Layout engine:
  - [~] `ui_layout_row(ctx, cols, widths[])` — fixed column row
  - [~] `ui_layout_push_id(ctx, id)` / `ui_layout_pop_id(ctx)` — scope nesting
  - Horizontal and vertical flow modes
  - Auto-sizing with min/max constraints
- [x] Mouse input routing from `SDL_AppEvent` → `ui_ctx_t` (`mouse_x`, `mouse_y`, `mouse_buttons`, `scroll`)
- [ ] Keyboard input routing for text fields and keyboard shortcuts
- [ ] `ui_waveform_display(ctx, samples, count)` — oscilloscope widget (line-draw via renderer)
- [ ] `ui_spectrum_display(ctx, bins, count)` — FFT magnitude bars

### 1.6 Theming

- [ ] `ui_theme_t` struct: color palette (background, surface, accent, text, disabled, danger)
- [ ] Built-in themes: `UI_THEME_DARK`, `UI_THEME_LIGHT`, `UI_THEME_SYNTHWAVE`
- [ ] `ui_set_theme(ctx, theme*)` — hot-switchable at runtime
- [ ] Font size / DPI scale factor in theme

---

## Phase 2 — Audio Engine

### 2.1 Audio Output

- [ ] Open SDL3 audio device (`SDL_OpenAudioDeviceStream`) in `app.c`
- [ ] Define audio callback signature: `audio_callback(userdata, stream, len)`
- [ ] `audio_ctx_t`: sample rate, buffer size, channel count, format (F32LE)
- [ ] Atomic double-buffer for synth → audio thread handoff (lock-free ring buffer)
- [ ] CLI flags: `--sample-rate`, `--buffer-size`, `--channels`

### 2.2 Oscillator

- [ ] `osc_t` struct: phase accumulator (double), frequency, amplitude, waveform type
- [ ] Waveform types:
  - [ ] Sine
  - [ ] Square (with pulse width)
  - [ ] Sawtooth (up/down)
  - [ ] Triangle
  - [ ] Wavetable (user-defined, linear interpolated)
  - [ ] Noise (white, pink via filtering)
  - [ ] Additive (harmonic series with per-partial amplitudes)
  - [ ] FM pair (carrier + modulator ratio, mod index)
- [ ] Anti-aliasing: PolyBLEP for discontinuous waveforms (saw, square)
- [ ] `osc_tick(osc*, sample_rate)` → `float` (single sample output)
- [ ] Phase reset / sync input

### 2.3 Envelope (ADSR / AHDSR)

- [ ] `env_t` struct: attack, hold, decay, sustain, release (all in seconds)
- [ ] State machine: IDLE → ATTACK → HOLD → DECAY → SUSTAIN → RELEASE → IDLE
- [ ] Configurable curve shapes: linear, exponential, logarithmic
- [ ] Velocity sensitivity scaling
- [ ] `env_gate(env*, bool on)` — trigger / release
- [ ] `env_tick(env*, sample_rate)` → `float` [0,1]

### 2.4 Filter

- [ ] `filter_t` struct: type, cutoff (Hz), resonance (Q)
- [ ] Filter types:
  - [ ] Biquad lowpass / highpass / bandpass / notch (direct form II)
  - [ ] State-variable filter (SVF) — simultaneous LP/HP/BP outputs
  - [ ] Moog-style ladder filter (4-pole, nonlinear tanh saturation)
  - [ ] Comb filter
- [ ] Coefficient recalculation on parameter change (debounced, not every sample)
- [ ] Filter envelope (separate ADSR modulating cutoff)
- [ ] `filter_tick(filter*, float in)` → `float`

### 2.5 LFO

- [ ] `lfo_t` struct: rate (Hz), depth, waveform, phase offset, target
- [ ] Sync modes: free, tempo-sync (BPM-relative)
- [ ] Targets: OSC pitch, OSC PWM, filter cutoff, amplitude, panning
- [ ] `lfo_tick(lfo*, sample_rate)` → `float` [-1, 1] (to be scaled by depth at modulation site)

### 2.6 Effects Chain

- [ ] `fx_chain_t`: ordered array of `fx_node_t*` (up to 16 slots)
- [ ] `fx_node_t` interface: `process(node, in_L, in_R, out_L*, out_R*)`, `reset()`, `set_param(id, val)`
- [ ] Effects to implement:
  - [ ] **Overdrive / Soft Clip** — tanh saturation, drive knob
  - [ ] **Distortion / Hard Clip** — threshold, asymmetry
  - [ ] **Bitcrusher** — bit depth reduction, sample rate reduction
  - [ ] **Chorus** — multi-tap delay + LFO modulated delay time
  - [ ] **Flanger** — short feedback delay + LFO
  - [ ] **Phaser** — all-pass chain + LFO modulated pole frequency
  - [ ] **Delay** — stereo ping-pong, tap tempo, feedback, mix
  - [ ] **Reverb** — Schroeder/Freeverb comb+allpass network; or convolution (stretch)
  - [ ] **Compressor** — RMS/peak detection, attack, release, ratio, knee, makeup gain
  - [ ] **EQ** — 8-band parametric (biquad peaking/shelving per band)
  - [ ] **Stereo Widener** — mid-side matrix + side gain
  - [ ] **Waveshaper** — arbitrary transfer curve (spline editor in UI)
- [ ] Wet/dry mix per effect node
- [ ] Bypass flag per node

### 2.7 Voice Management (Polyphony)

- [ ] `voice_t`: OSC + ENV + FILTER + per-voice LFO state
- [ ] Voice pool: up to 16 voices (configurable)
- [ ] Voice steal policies: oldest, quietest, same-note
- [ ] Monophonic mode with portamento / glide
- [ ] Unison mode: N detuned voices per note, stereo spread

---

## Phase 3 — MIDI Integration

- [ ] Wire PortMidi `Pm_Read` back into `SDL_AppIterate` (currently commented out)
- [ ] MIDI input device selection (CLI flag + runtime dropdown in UI)
- [ ] Note On/Off → voice allocator
- [ ] Pitch bend → per-voice pitch offset
- [ ] Mod wheel (CC1) → configurable modulation target
- [ ] Sustain pedal (CC64) → envelope hold
- [ ] MIDI learn: right-click any knob → "MIDI Learn" → wiggle CC → binding stored
- [ ] MIDI learn bindings persisted to config file (JSON)

---

## Phase 4 — Preset / Patch System

- [ ] `patch_t` struct: snapshot of all synth parameters
- [ ] Serialize / deserialize to JSON (no deps: hand-written or minified jsmn)
- [ ] Preset browser widget in UI (list + search)
- [ ] Factory presets embedded as C string literals
- [ ] Undo/redo stack (ring buffer of `patch_t`, 64 levels deep)

---

## Phase 5 — Waveform Editor

- [ ] Drawable wavetable editor widget (canvas click/drag → amplitude)
- [ ] Harmonic editor: slider per harmonic partial → inverse FFT → wavetable
- [ ] Import WAV file as wavetable (SDL_LoadWAV or libsndfile)
- [ ] Wavetable morphing: crossfade between two stored wavetables
- [ ] Save/load custom wavetables to disk (binary + JSON header)

---

## Phase 6 — Sequencer / Arpeggiator (Stretch)

- [ ] Step sequencer: 16–64 steps, per-step note/velocity/gate
- [ ] Arpeggiator: up/down/random/chord modes, rate (BPM sync)
- [ ] BPM tap tempo
- [ ] MIDI clock in/out (sync to DAW)

---

## Infrastructure / Ongoing

- [ ] Fix `App_t.config` field: currently `HashMap` by value but hashmap API uses pointers — change to `HashMap*`
- [ ] `hashmap_free` does not free `ArgParseResult*` values — add a `hashmap_free_values(map, free_fn)` helper
- [ ] `log.c`: `fopen_s` and `gmtime_s` are MSVC/C11 Annex K; add POSIX fallback (`fopen`, `gmtime_r`) for GCC/Clang
- [ ] Replace `sprintf` in `app.c` with `snprintf` (buffer overflow risk in title string)
- [ ] Add `--fullscreen` flag; current code hardcodes `SDL_WINDOW_FULLSCREEN`
- [ ] Unit tests: CMake `enable_testing()` + CTest; at minimum cover hashmap, argparse, MIDI pack/unpack, filter coefficients
- [ ] Address sanitizer target in CMake (`-fsanitize=address,undefined`) for Debug builds
- [ ] CI: GitHub Actions workflow — build matrix (Linux GCC, Linux Clang, Windows MSVC, macOS Clang)
- [ ] `clang-tidy` integration in CMake (optional target)
- [ ] README with build instructions, dependency list, architecture diagram

---

## Dependency Map

```
SDL3           → windowing, events, audio, GPU/OpenGL context, Vulkan surface
PortMidi       → MIDI I/O (already wired)
Vulkan SDK     → Vulkan backend (optional, ENABLE_VULKAN=ON)
GLAD / gl.h    → OpenGL backend (optional, ENABLE_OPENGL=ON)
libm           → math (sin, fmod, pow — already linked on UNIX)
```

No other external dependencies. Everything else (JSON, fonts, wavetables) is self-contained.
