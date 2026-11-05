# Test GUI

Small PyQt5 GUI to exercise the wheel over its serial protocol (see `docs/PROTOCOL.md`).

    pip install -r requirements.txt
    python main.py

Speaks protocol v2 (every command acknowledged). Per-slot user offsets are not available in the
protocol yet (only slot 0), so the GUI offers them for slot 0 only.
