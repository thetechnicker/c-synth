"""
Simple MIDI Piano — tkinter + mido/rtmidi
Sends MIDI note_on/note_off to a virtual MIDI port.
"""

import tkinter as tk
from tkinter import messagebox
from typing import Any

import mido

# ── Layout constants ──────────────────────────────────────────────────────────
WHITE_W, WHITE_H = 52, 200
BLACK_W, BLACK_H = 32, 125
N_WHITES = 14  # two octaves of white keys
CANVAS_W = WHITE_W * N_WHITES
CANVAS_H = WHITE_H + 60  # extra space for toolbar

# C major scale: True = white, False = black
# Pattern for one octave: C C# D D# E F F# G G# A A# B
CHROMATIC = [
    True,
    False,
    True,
    False,
    True,
    True,
    False,
    True,
    False,
    True,
    False,
    True,
]

# White key order within one octave (0-based chromatic index)
WHITE_IDX = [i for i, w in enumerate(CHROMATIC) if w]  # [0,2,4,5,7,9,11]
BLACK_IDX = [i for i, w in enumerate(CHROMATIC) if not w]  # [1,3,6,8,10]

# PC keyboard → white-key slot (0 = leftmost white key of displayed range)
KEY_WHITE = {
    "a": 0,
    "s": 1,
    "d": 2,
    "f": 3,
    "g": 4,
    "h": 5,
    "j": 6,
    "k": 7,
    "l": 8,
}
# PC keyboard → black-key slot (0 = first black key of displayed range)
KEY_BLACK = {
    "w": 0,
    "e": 1,
    "t": 2,
    "y": 3,
    "u": 4,
    "o": 5,
    "p": 6,
}

HIGHLIGHT_WHITE = "#a0d8ef"
HIGHLIGHT_BLACK = "#1a6ea8"
CHANNEL = 0
VELOCITY = 100


def open_midi_output(
    preferred_name: str = "MIDI Piano", fallback_name: str = "LoopBE Internal MIDI"
) -> Any:
    try:  # try to open a virtual port (creates "MIDI Piano")
        return mido.open_output(preferred_name, virtual=True)
    except:  # virtual creation failed — try to connect to an existing LoopBE port name
        print("virtual outputs arent supported, using fallback")
        try:
            return mido.open_output(fallback_name)
        except (
            OSError,
            IOError,
            FileNotFoundError,
        ) as e:  # final fallback: open the first available output if any
            outputs = mido.get_output_names()
            if outputs:
                return mido.open_output(outputs[0])
            raise RuntimeError(
                f"Could not open preferred virtual port '{preferred_name}' "
                f"or fallback '{fallback_name}'. Available outputs: {outputs}"
            ) from e


class MidiPiano:
    def __init__(self, root: tk.Tk):
        self.root = root
        self.octave = 4  # base octave (C4 = MIDI 60)
        self.port = None
        self.active = {}  # midi_note → canvas item id

        root.title("MIDI Piano")
        root.resizable(False, False)

        self._open_port()
        self._build_toolbar()
        self._build_canvas()
        self._bind_keys()

    # ── MIDI port ─────────────────────────────────────────────────────────────
    def _open_port(self):
        try:
            self.port = open_midi_output()
        except Exception as exc:
            messagebox.showerror(
                "MIDI Error",
                f"Could not open virtual MIDI port:\n{exc}\n\n"
                "On Windows install loopMIDI first.",
            )

    # ── Toolbar ───────────────────────────────────────────────────────────────
    def _build_toolbar(self):
        bar = tk.Frame(self.root, bg="#2b2b2b", pady=6)
        bar.pack(fill="x")

        tk.Label(
            bar,
            text="MIDI Piano",
            fg="white",
            bg="#2b2b2b",
            font=("Helvetica", 13, "bold"),
        ).pack(side="left", padx=12)

        # Octave controls
        ctrl = tk.Frame(bar, bg="#2b2b2b")
        ctrl.pack(side="right", padx=12)

        tk.Button(
            ctrl,
            text="◀  Oct−",
            command=self.octave_down,
            bg="#444",
            fg="white",
            relief="flat",
            padx=8,
        ).pack(side="left", padx=2)

        self.oct_label = tk.Label(
            ctrl,
            text=f"Octave: {self.octave}",
            fg="#f0c040",
            bg="#2b2b2b",
            font=("Helvetica", 11, "bold"),
            width=10,
        )
        self.oct_label.pack(side="left", padx=4)

        tk.Button(
            ctrl,
            text="Oct+  ▶",
            command=self.octave_up,
            bg="#444",
            fg="white",
            relief="flat",
            padx=8,
        ).pack(side="left", padx=2)

    # ── Canvas / piano drawing ────────────────────────────────────────────────
    def _build_canvas(self):
        self.canvas = tk.Canvas(
            self.root,
            width=CANVAS_W,
            height=WHITE_H,
            bg="#1a1a1a",
            highlightthickness=0,
        )
        self.canvas.pack()

        # Maps: midi_note → canvas item id (built once, reused)
        self.white_items: dict[int, int] = {}
        self.black_items: dict[int, int] = {}

        self._draw_keys()
        self.canvas.bind("<ButtonPress-1>", self._mouse_press)
        self.canvas.bind("<ButtonRelease-1>", self._mouse_release)

    def _note_for_white(self, slot: int) -> int:
        """Return MIDI note for white-key slot (0 = leftmost displayed key)."""
        oct_offset, key_in_oct = divmod(slot, 7)
        return (self.octave + oct_offset) * 12 + WHITE_IDX[key_in_oct]

    def _note_for_black(self, slot: int) -> int:
        """Return MIDI note for black-key slot (0 = first black key shown)."""
        oct_offset, key_in_oct = divmod(slot, 5)
        return (self.octave + oct_offset) * 12 + BLACK_IDX[key_in_oct]

    def _draw_keys(self):
        self.canvas.delete("all")
        self.white_items.clear()
        self.black_items.clear()

        # White keys
        for slot in range(N_WHITES):
            x0 = slot * WHITE_W
            item = self.canvas.create_rectangle(
                x0,
                0,
                x0 + WHITE_W - 2,
                WHITE_H - 2,
                fill="white",
                outline="#555",
                tags=("white", f"w{slot}"),
            )
            self.white_items[self._note_for_white(slot)] = item

            # Key label
            note = self._note_for_white(slot)
            names = ["C", "D", "E", "F", "G", "A", "B"]
            label = names[slot % 7] + str(self.octave + slot // 7)
            self.canvas.create_text(
                x0 + WHITE_W // 2,
                WHITE_H - 18,
                text=label,
                font=("Helvetica", 7),
                fill="#888",
                tags=("label",),
            )

        # PC key hints for first octave white keys
        hints_w = list(KEY_WHITE.keys())[:7]
        for i, hint in enumerate(hints_w):
            self.canvas.create_text(
                i * WHITE_W + WHITE_W // 2,
                WHITE_H - 30,
                text=hint.upper(),
                font=("Helvetica", 8, "bold"),
                fill="#aaa",
                tags=("label",),
            )

        # Black keys (drawn on top)
        # positions relative to white-key slots
        BLACK_OFFSETS = [0, 1, 3, 4, 5]  # after which white key in octave
        for oct in range(2):
            for bi, wo in enumerate(BLACK_OFFSETS):
                slot = oct * 5 + bi
                white_slot = oct * 7 + wo
                x0 = white_slot * WHITE_W + WHITE_W - BLACK_W // 2
                item = self.canvas.create_rectangle(
                    x0,
                    0,
                    x0 + BLACK_W,
                    BLACK_H,
                    fill="#111",
                    outline="#333",
                    tags=("black", f"b{slot}"),
                )
                self.black_items[self._note_for_black(slot)] = item

        # PC key hints for first octave black keys
        hints_b = list(KEY_BLACK.keys())[:5]
        BLACK_OFFSETS_HINT = [0, 1, 3, 4, 5]
        for i, hint in enumerate(hints_b):
            wo = BLACK_OFFSETS_HINT[i]
            x0 = wo * WHITE_W + WHITE_W - BLACK_W // 2
            self.canvas.create_text(
                x0 + BLACK_W // 2,
                BLACK_H - 14,
                text=hint.upper(),
                font=("Helvetica", 7, "bold"),
                fill="#ccc",
                tags=("label",),
            )

    # ── Note on/off ───────────────────────────────────────────────────────────
    def note_on(self, midi_note: int):
        if midi_note in self.active or not self.port:
            return
        self.port.send(
            mido.Message("note_on", channel=CHANNEL, note=midi_note, velocity=VELOCITY)
        )
        # Highlight
        if midi_note in self.white_items:
            item = self.white_items[midi_note]
            self.canvas.itemconfig(item, fill=HIGHLIGHT_WHITE)
            self.active[midi_note] = item
        elif midi_note in self.black_items:
            item = self.black_items[midi_note]
            self.canvas.itemconfig(item, fill=HIGHLIGHT_BLACK)
            self.active[midi_note] = item

    def note_off(self, midi_note: int):
        if not self.port:
            return
        self.port.send(
            mido.Message("note_off", channel=CHANNEL, note=midi_note, velocity=0)
        )
        if midi_note in self.active:
            item = self.active.pop(midi_note)
            orig = "white" if midi_note in self.white_items else "#111"
            self.canvas.itemconfig(
                item, fill="white" if midi_note in self.white_items else "#111"
            )

    # ── Mouse interaction ─────────────────────────────────────────────────────
    def _item_to_note(self, item_id: int):
        for note, iid in self.black_items.items():
            if iid == item_id:
                return note
        for note, iid in self.white_items.items():
            if iid == item_id:
                return note
        return None

    def _mouse_press(self, event):
        # Check black keys first (they sit on top)
        items = self.canvas.find_overlapping(
            event.x - 1, event.y - 1, event.x + 1, event.y + 1
        )
        for item in reversed(items):
            tags = self.canvas.gettags(item)
            if "black" in tags:
                note = self._item_to_note(item)
                if note is not None:
                    self.root.focus_set()
                    self.note_on(note)
                    self._last_mouse_note = note
                    return
        for item in reversed(items):
            tags = self.canvas.gettags(item)
            if "white" in tags:
                note = self._item_to_note(item)
                if note is not None:
                    self.root.focus_set()
                    self.note_on(note)
                    self._last_mouse_note = note
                    return
        self._last_mouse_note = None

    def _mouse_release(self, event):
        note = getattr(self, "_last_mouse_note", None)
        if note is not None:
            self.note_off(note)
            self._last_mouse_note = None

    # ── Keyboard interaction ──────────────────────────────────────────────────
    def _bind_keys(self):
        self.root.bind("<KeyPress>", self._key_press)
        self.root.bind("<KeyRelease>", self._key_release)

    def _key_press(self, event):
        k = event.keysym.lower()
        if k == "z":
            self.octave_down()
            return
        if k == "x":
            self.octave_up()
            return
        if k in KEY_WHITE:
            self.note_on(self._note_for_white(KEY_WHITE[k]))
        elif k in KEY_BLACK:
            self.note_on(self._note_for_black(KEY_BLACK[k]))

    def _key_release(self, event):
        k = event.keysym.lower()
        if k in KEY_WHITE:
            self.note_off(self._note_for_white(KEY_WHITE[k]))
        elif k in KEY_BLACK:
            self.note_off(self._note_for_black(KEY_BLACK[k]))

    # ── Octave control ────────────────────────────────────────────────────────
    def octave_down(self):
        if self.octave > 0:
            self._release_all()
            self.octave -= 1
            self._refresh()

    def octave_up(self):
        if self.octave < 7:
            self._release_all()
            self.octave += 1
            self._refresh()

    def _release_all(self):
        for note in list(self.active):
            self.note_off(note)

    def _refresh(self):
        self.oct_label.config(text=f"Octave: {self.octave}")
        self._draw_keys()

    # ── Cleanup ───────────────────────────────────────────────────────────────
    def on_close(self):
        self._release_all()
        if self.port:
            self.port.close()
        self.root.destroy()


# ── Entry point ───────────────────────────────────────────────────────────────
if __name__ == "__main__":
    root = tk.Tk()
    app = MidiPiano(root)
    root.protocol("WM_DELETE_WINDOW", app.on_close)
    root.mainloop()
