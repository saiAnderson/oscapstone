from pathlib import Path
import struct  

PROJECT_ROOT = Path(__file__).resolve().parents[1]

KERNEL_PATH = PROJECT_ROOT / "build/kernel/kernel8.img"
PTY_FILE = PROJECT_ROOT / "build/uart_pty.txt"

if not PTY_FILE.exists():
    raise SystemExit("PTY file not found. Run 'make run' first.")

uart_path = PTY_FILE.read_text().strip()

print("UART PTY:", uart_path)

# rb = read binary
with open("../build/kernel/kernel8.img", "rb") as f: 
    kernel = f.read()

kernel_size = len(kernel)
print(type(kernel))
print(f"{kernel_size} bytes")
print(hex(kernel_size))

# 將 Python int 轉成 4-byte little-endian unsigned integer
size_bytes = struct.pack("<I", kernel_size)
print(size_bytes)


# wb = write binary
# buffering=0 = 不額外使用 Python buffering
with open(uart_path, "wb", buffering=0) as tty:
    print("send kernel size:", kernel_size)
    tty.write(size_bytes)
    tty.write(kernel)
