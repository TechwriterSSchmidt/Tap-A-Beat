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
    # Matches drawMetronomeScreen in main.cpp (STOPPED State)
    img = create_base_image()
    draw = ImageDraw.Draw(img)
    
    # BPM (Logisoso42 approx)
    font_bpm = get_font(42)
    draw.text((20, 15), "120", font=font_bpm, fill=FG_COLOR)
    
    # "BPM" Label (ProFont12)
    font_small = get_font(12)
    draw.text((95, 50), "BPM", font=font_small, fill=FG_COLOR)
    
    # Beat Visual (Circle Outline at 64, 90)
    cx, cy = 64, 90
    r = 10
    draw.ellipse((cx-r, cy-r, cx+r, cy+r), outline=FG_COLOR, width=1)
    
    # Text "Click: Play"
    font_tiny = get_font(10)
    # Centered approx
    draw.text((40, 110), "Click: Play", font=font_tiny, fill=FG_COLOR)
    
    # Volume Bar (Bottom)
    vol_w = 64 # 50% volume
    draw.rectangle((0, 124, vol_w, 128), fill=FG_COLOR)
    
    img_resized = img.resize((512, 512), Image.NEAREST)
    img_resized.save(os.path.join(OUTPUT_DIR, "screen_metronome.png"))

def draw_quick_menu_screen():
    # Matches drawQuickMenuScreen in main.cpp
    # Overlay on top of Metronome Screen (approx)
    
    # First draw base metronome
    img = create_base_image()
    draw = ImageDraw.Draw(img)
    font_bpm = get_font(42)
    draw.text((20, 15), "120", font=font_bpm, fill=FG_COLOR)
    
    # Overlay Box
    # u8g2.drawBox(10, 20, 108, 90); -> filled black (0)
    draw.rectangle((10, 20, 118, 110), fill=BG_COLOR, outline=None)
    
    # u8g2.drawFrame(10, 20, 108, 90); -> white (1)
    draw.rectangle((10, 20, 118, 110), outline=FG_COLOR, width=1)
    
    # u8g2.drawFrame(12, 22, 104, 86); -> white (1)
    draw.rectangle((12, 22, 116, 108), outline=FG_COLOR, width=1)
    
    # Title Box
    # u8g2.drawBox(30, 16, 68, 10); -> white
    draw.rectangle((30, 16, 98, 26), fill=FG_COLOR)
    
    # Title Text "QUICK MENU"
    font_title = get_font(10)
    # Draw black text on white box
    draw.text((36, 16), "QUICK MENU", font=font_title, fill=BG_COLOR)
    
    # Items
    items = ["Metric", "Subdiv", "Preset"]
    values = ["[4/4]", "1/4", "#1"]
    
    font_item = get_font(10)
    y_start = 45
    h = 20
    selection = 0
    
    for i, item in enumerate(items):
        y = y_start + (i * h)
        
        # Cursor
        if i == selection:
            draw.text((20, y), ">", font=font_item, fill=FG_COLOR)
            
        draw.text((30, y), item, font=font_item, fill=FG_COLOR)
        
        # Value
        draw.text((75, y), values[i], font=font_item, fill=FG_COLOR)
        
    # Footer
    font_tiny = get_font(8)
    draw.text((25, 105), "Click: Edit/Save", font=font_tiny, fill=FG_COLOR)
    
    img_resized = img.resize((512, 512), Image.NEAREST)
    img_resized.save(os.path.join(OUTPUT_DIR, "screen_quick_menu.png"))

def draw_menu_screen():
    # Matches drawMenuScreen
    img = create_base_image()
    draw = ImageDraw.Draw(img)
    
    font_header = get_font(12)
    font_item = get_font(10)
    
    # Header
    draw.text((0, 0), "-- MENU --", font=font_header, fill=FG_COLOR)
    draw.line((0, 12, 128, 12), fill=FG_COLOR)
    
    # Items from code: Metric, Subdiv, Tap Tempo, Trainer, Timer, Tuner, Presets, Exit
    items = ["Metric", "Subdiv", "Tap Tempo", "Trainer"] # Just the first page view gets rendered
    selection = 0 
    
    y = 30
    h = 14
    
    for i, item in enumerate(items):
        if i == selection:
            # Selected: Inverted box
            draw.rectangle((0, y - 9 + (i*h), 128, y + 2 + (i*h)), fill=FG_COLOR)
            draw.text((4, y + (i*h) - 8), item, font=font_item, fill=BG_COLOR)
        else:
            draw.text((4, y + (i*h) - 8), item, font=font_item, fill=FG_COLOR)
            
    # Resize for higher quality display in README (2x)
    img_resized = img.resize((512, 512), Image.NEAREST)
    img_resized.save(os.path.join(OUTPUT_DIR, "screen_menu.png"))

def draw_presets_menu_screen():
    # Matches drawPresetsMenuScreen
    img = create_base_image()
    draw = ImageDraw.Draw(img)
    
    font_header = get_font(12)
    font_item = get_font(10)
    
    # Header
    draw.text((0, 0), "- PRESETS -", font=font_header, fill=FG_COLOR)
    draw.line((0, 12, 128, 12), fill=FG_COLOR)
    
    # Items
    items = ["Load Preset", "Save Preset", "Back"]
    selection = 0
    
    y = 30
    h = 18 
    
    for i, item in enumerate(items):
        if i == selection:
            draw.rectangle((10, y - 10 + (i*h), 118, y + 4 + (i*h)), fill=FG_COLOR)
            # Text Centering (Approx)
            text_w = len(item) * 6 
            x = (128 - text_w) // 2
            draw.text((x, y + (i*h) - 8), item, font=font_item, fill=BG_COLOR)
        else:
            text_w = len(item) * 6 
            x = (128 - text_w) // 2
            draw.text((x, y + (i*h) - 8), item, font=font_item, fill=FG_COLOR)

    img_resized = img.resize((512, 512), Image.NEAREST)
    img_resized.save(os.path.join(OUTPUT_DIR, "screen_presets_menu.png"))

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
    draw.polygon([(10, 50), (25, 40), (25, 60)], outline=FG_COLOR, fill=FG_COLOR)
    draw.polygon([(118, 50), (103, 40), (103, 60)], outline=FG_COLOR, fill=FG_COLOR)
    
    # Bottom text
    draw.text((30, 80), "4 Beats/Bar", font=font_small, fill=FG_COLOR)
    
    img_resized = img.resize((512, 512), Image.NEAREST)
    img_resized.save(os.path.join(OUTPUT_DIR, "screen_set_bpm.png"))

def draw_tap_tempo_screen():
    # Matches drawTapScreen (Taptronic)
    img = create_base_image()
    draw = ImageDraw.Draw(img)
    
    font_bold = get_font(12) 
    draw.text((30, 2), "TAPTRONIC", font=font_bold, fill=FG_COLOR)
    draw.line((0, 14, 128, 14), fill=FG_COLOR)
    
    cx, cy = 64, 60
    
    # Heart Outline
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
    r = 8
    draw.ellipse((cx - 15 - r, cy - 5 - r, cx - 15 + r, cy - 5 + r), fill=FG_COLOR)
    draw.ellipse((cx + 15 - r, cy - 5 - r, cx + 15 + r, cy - 5 + r), fill=FG_COLOR)
    draw.polygon([(cx - 21, cy + 1), (cx + 21, cy + 1), (cx, cy + 22)], fill=FG_COLOR)
    
    # Text
    font_small = get_font(10)
    draw.text((10, 110), "Sens: 50%", font=font_small, fill=FG_COLOR)
    draw.text((70, 110), "TAP NOW!", font=font_small, fill=FG_COLOR)
    
    img_resized = img.resize((512, 512), Image.NEAREST)
    img_resized.save(os.path.join(OUTPUT_DIR, "screen_tap_tempo.png"))

if __name__ == "__main__":
    draw_metronome_screen()
    draw_menu_screen()
    draw_presets_menu_screen()
    draw_set_bpm_screen()
    draw_tap_tempo_screen()
    draw_quick_menu_screen()
    print("Mockups generated in " + OUTPUT_DIR)
