from PIL import Image, ImageDraw, ImageFont
import os

# Configuration
WIDTH = 128
HEIGHT = 128
BG_COLOR = (0, 0, 0)
FG_COLOR = (255, 255, 255) # Lit pixels

OUTPUT_DIR = os.path.dirname(os.path.abspath(__file__))

def get_font(size):
    try:
        # Try a monospace-like font if possible, else arial
        return ImageFont.truetype("cour.ttf", size) 
    except IOError:
        try:
             return ImageFont.truetype("arial.ttf", size)
        except IOError:
             return ImageFont.load_default()

def create_base_image():
    return Image.new('RGB', (WIDTH, HEIGHT), BG_COLOR)

def draw_metronome_screen():
    # Matches drawMetronomeScreen in main.cpp
    img = create_base_image()
    draw = ImageDraw.Draw(img)
    
    # BPM (Logisoso42 approx)
    font_bpm = get_font(42)
    draw.text((20, 15), "120", font=font_bpm, fill=FG_COLOR)
    
    # "BPM" Label (ProFont12)
    font_small = get_font(12)
    draw.text((95, 50), "BPM", font=font_small, fill=FG_COLOR)
    
    # Beat Visual (Disc at 64, 90)
    cx, cy = 64, 90
    r = 10
    draw.ellipse((cx-r, cy-r, cx+r, cy+r), outline=FG_COLOR, width=1)
    
    # Beat Counter "1/4"
    draw.text((45, 105), "1/4", font=font_small, fill=FG_COLOR)
    
    # Volume Bar (Bottom)
    vol_w = 64 # 50% volume
    draw.rectangle((0, 124, vol_w, 128), fill=FG_COLOR)
    
    img.save(os.path.join(OUTPUT_DIR, "screen_metronome.png"))

def draw_menu_screen():
    # Matches drawMenuScreen
    img = create_base_image()
    draw = ImageDraw.Draw(img)
    
    font_header = get_font(12)
    font_item = get_font(10)
    
    # Header
    draw.text((0, 0), "-- MENU --", font=font_header, fill=FG_COLOR)
    draw.line((0, 12, 128, 12), fill=FG_COLOR)
    
    # Items
    items = ["Metric: 4/4", "Taptronic", "Tuner", "Load Preset", "Save Preset", "Exit"]
    selection = 1 # Select Taptronic to highlight it
    
    y = 30
    h = 14
    
    for i, item in enumerate(items):
        if i == selection:
            # Selected: Inverted box
            draw.rectangle((0, y - 9 + (i*h), 128, y + 2 + (i*h)), fill=FG_COLOR)
            draw.text((4, y + (i*h) - 8), item, font=font_item, fill=BG_COLOR)
        else:
            draw.text((4, y + (i*h) - 8), item, font=font_item, fill=FG_COLOR)
            
    img.save(os.path.join(OUTPUT_DIR, "screen_menu.png"))

def draw_set_bpm_screen():
    # Reusing filename for "Time Sig" screen as "Set BPM" is gone from menu
    # Matches drawTimeSigScreen in main.cpp
    img = create_base_image()
    draw = ImageDraw.Draw(img)
    
    font_small = get_font(10)
    draw.text((0, 2), "--- TIME SIG ---", font=font_small, fill=FG_COLOR)
    
    # Big Number
    font_large = get_font(42)
    draw.text((40, 25), "4", font=font_large, fill=FG_COLOR)
    
    # Arrows (Triangle)
    # Left: 10,50 -> 25,40 -> 25,60
    draw.polygon([(10, 50), (25, 40), (25, 60)], outline=FG_COLOR, fill=FG_COLOR)
    # Right: 118,50 -> 103,40 -> 103,60
    draw.polygon([(118, 50), (103, 40), (103, 60)], outline=FG_COLOR, fill=FG_COLOR)
    
    # Bottom text
    draw.text((30, 80), "4 Beats/Bar", font=font_small, fill=FG_COLOR)
    
    img.save(os.path.join(OUTPUT_DIR, "screen_set_bpm.png")) # Keep filename for README compat

def draw_tap_tempo_screen():
    # Matches drawTapScreen (Taptronic)
    img = create_base_image()
    draw = ImageDraw.Draw(img)
    
    font_bold = get_font(12) # ProFont12
    draw.text((30, 2), "TAPTRONIC", font=font_bold, fill=FG_COLOR)
    draw.line((0, 14, 128, 14), fill=FG_COLOR)
    
    cx, cy = 64, 60
    
    # Heart Outline (Lines from code)
    # (cx, cy + 30) -> (cx - 30, cy - 10)
    # (cx - 30, cy - 10) -> (cx - 15, cy - 25)
    # ...
    points = [
        (cx, cy + 30),
        (cx - 30, cy - 10),
        (cx - 15, cy - 25),
        (cx, cy - 10), # Center Join
        (cx + 15, cy - 25),
        (cx + 30, cy - 10),
        (cx, cy + 30)
    ]
    draw.line(points, fill=FG_COLOR, width=1)
    
    # Filled Inner (Triggered State)
    # Discs at cx-15, cy-5, r=8
    r = 8
    draw.ellipse((cx - 15 - r, cy - 5 - r, cx - 15 + r, cy - 5 + r), fill=FG_COLOR)
    draw.ellipse((cx + 15 - r, cy - 5 - r, cx + 15 + r, cy - 5 + r), fill=FG_COLOR)
    # Triangle Bottom
    draw.polygon([(cx - 21, cy + 1), (cx + 21, cy + 1), (cx, cy + 22)], fill=FG_COLOR)
    
    # Text
    font_small = get_font(10)
    draw.text((10, 110), "Sens: 50%", font=font_small, fill=FG_COLOR)
    draw.text((70, 110), "TAP NOW!", font=font_small, fill=FG_COLOR)
    
    img.save(os.path.join(OUTPUT_DIR, "screen_tap_tempo.png"))

if __name__ == "__main__":
    draw_metronome_screen()
    draw_menu_screen()
    draw_set_bpm_screen()
    draw_tap_tempo_screen()
    print("Mockups generated in " + OUTPUT_DIR)
