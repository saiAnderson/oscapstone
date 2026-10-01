import struct  

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
with open("/dev/pts/7", "wb", buffering=0) as tty:
    print("send kernel size:", kernel_size)
    tty.write(size_bytes)
    tty.write(kernel)
