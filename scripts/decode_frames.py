import argparse
from PIL import Image
import struct
import os
import glob

def convert_frames(input_folder, output_folder):
    os.makedirs(output_folder, exist_ok=True)

    files = sorted(glob.glob(os.path.join(input_folder, "*.gif")))
    if not files:
        print(f"No GIF files found in {input_folder}")
        return

    for path in files:
        basename = os.path.splitext(os.path.basename(path))[0]
        out_path = os.path.join(output_folder, basename + ".raw")

        img = Image.open(path).convert("RGB")
        w, h = img.size

        with open(out_path, "wb") as f:
            for y in range(h):
                for x in range(w):
                    r, g, b = img.getpixel((x, y))
                    rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
                    f.write(struct.pack(">H", rgb565))

        print(f"Converted: {os.path.basename(path)} -> {os.path.basename(out_path)}")

    print(f"\nDone. {len(files)} files converted to {output_folder}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Convert GIF frames to raw RGB565 binary files for ESP32 TFT display")
    parser.add_argument("input", help="Input folder containing GIF files")
    parser.add_argument("output", help="Output folder for raw RGB565 files")
    args = parser.parse_args()

    convert_frames(args.input, args.output)