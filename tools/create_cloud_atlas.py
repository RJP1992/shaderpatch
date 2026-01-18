#!/usr/bin/env python3
"""
Create cloud texture atlas for Shader Patch clouds_simple system.
Combines 4 cloud textures into a 2x2 atlas.
"""

from PIL import Image
import os

# Source cloud textures (pick 4 best)
CLOUD_DIR = r"C:\BF2_ModTools\custom_assets\Mods\SWBF3 Legacy\Source Assets\misctex\particle"
OUTPUT_DIR = r"C:\Users\tehpa\source\repos\shaderpatch\assets\core\textures"

# Cloud textures to use (in order: top-left, top-right, bottom-left, bottom-right)
CLOUD_FILES = [
    "cloud_1.png",   # Puffy cumulus
    "cloud2.png",    # Dense dramatic
    "cloud5.png",    # Light wispy
    "cloud7.png",    # Detailed cumulus
]

ATLAS_SIZE = 512  # Output atlas size (512x512, each cell 256x256)
CELL_SIZE = ATLAS_SIZE // 2

def create_atlas():
    # Create output atlas (RGBA)
    atlas = Image.new('RGBA', (ATLAS_SIZE, ATLAS_SIZE), (0, 0, 0, 0))

    positions = [
        (0, 0),                    # Top-left
        (CELL_SIZE, 0),            # Top-right
        (0, CELL_SIZE),            # Bottom-left
        (CELL_SIZE, CELL_SIZE),    # Bottom-right
    ]

    for i, (cloud_file, pos) in enumerate(zip(CLOUD_FILES, positions)):
        cloud_path = os.path.join(CLOUD_DIR, cloud_file)
        print(f"Loading {cloud_file}...")

        # Load and convert to RGBA
        img = Image.open(cloud_path).convert('RGBA')

        # Resize to cell size
        img = img.resize((CELL_SIZE, CELL_SIZE), Image.LANCZOS)

        # For grayscale clouds, use luminance as alpha
        # Check if it's essentially grayscale
        r, g, b, a = img.split()

        # If alpha is all white (255), the image doesn't have real alpha
        # Use the grayscale value as alpha instead
        if a.getextrema() == (255, 255):
            # Convert RGB to grayscale for alpha
            gray = img.convert('L')
            # Create new image with white RGB and grayscale alpha
            img = Image.merge('RGBA', (r, g, b, gray))

        # Paste into atlas
        atlas.paste(img, pos)
        print(f"  Placed at {pos}")

    # Save atlas
    output_path = os.path.join(OUTPUT_DIR, "_SP_BUILTIN_cloud_atlas.png")
    atlas.save(output_path, 'PNG')
    print(f"\nSaved atlas to: {output_path}")

    # Create .tex config file
    tex_path = output_path + ".tex"
    with open(tex_path, 'w') as f:
        f.write("Type: image\n")
        f.write("Uncompressed: yes\n")
        f.write("NoMips: yes\n")
        f.write("sRGB: no\n")
        f.write("PremultiplyAlpha: no\n")
        f.write("_SP_DirectTexture: yes\n")
    print(f"Created config: {tex_path}")

if __name__ == "__main__":
    create_atlas()
    print("\nDone! Rebuild textures.lvl to include the new atlas.")
