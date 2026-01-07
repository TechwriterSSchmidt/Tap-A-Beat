from PIL import Image, ImageDraw, ImageFont
import os

# Configuration
WIDTH = 128
HEIGHT = 128
BG_COLOR = (0, 0, 0)
FG_COLOR = (255, 255, 255)
ACCENT_COLOR = (200, 200, 200)

OUTPUT_DIR = os.path.dirname(os.path.abspath(__file__))

def get_font(size):
    try:
        # Try standard Windows font
        return ImageFont.truetype("arial.ttf", size)
    except IOError:
        return ImageFont.load_default()

def create_base_image():
    return Image.new('RGB', (WIDTH, HEIGHT), BG_COLOR)

def draw_metronome_screen():
    img = create_base_image()
    draw = ImageDraw.Draw(img)
    
    # Status Bar
    font_small = get_font(10)
    draw.text((2, 0), "[~] [Spkr]", font=font_small, fill=FG_COLOR)
    
    # BPM
    font_large = get_font(40)
    text_bpm = "120"
    # Centering text manually-ish or using textbbox if available (older PIL might not have it)
    # Using roughly estimated centering for robustness
    draw.text((30, 30), text_bpm, font=font_large, fill=FG_COLOR)
    
    font_med = get_font(12)
    draw.text((50, 70), "BPM", font=font_med, fill=ACCENT_COLOR)
    
    # Beat Indicator
    draw.text((35, 90), ">> O <<", font=font_med, fill=FG_COLOR)
    
    # Bottom Info
    draw.text((20, 110), "4/4  Vol:10", font=font_small, fill=FG_COLOR)
    
    img.save(os.path.join(OUTPUT_DIR, "screen_metronome.png"))

def draw_menu_screen():
    img = create_base_image()
    draw = ImageDraw.Draw(img)
    
    font_header = get_font(14)
    font_item = get_font(11)
    
    # Header
    draw.rectangle([0, 0, 128, 16], outline=FG_COLOR, width=1)
    draw.text((35, 1), "- MENU -", font=font_header, fill=FG_COLOR)
    
    # Items
    items = [
        "> Speed: 120 bpm",
        "  Metric: 4/4",
        "  Tap / Thresh",
        "  Tuner",
        "  Volume: 10",
        "  Exit"
    ]
    
    y = 25
    for item in items:
        draw.text((5, y), item, font=font_item, fill=FG_COLOR)
        y += 15
        
    img.save(os.path.join(OUTPUT_DIR, "screen_menu.png"))

def draw_set_bpm_screen():
    img = create_base_image()
    draw = ImageDraw.Draw(img)
    
    font_header = get_font(14)
    
    # Header
    draw.rectangle([0, 0, 128, 16], fill=FG_COLOR)
    draw.text((30, 1), "- SET BPM -", font=font_header, fill=BG_COLOR)
    
    # Value
    font_huge = get_font(48)
    draw.text((25, 40), "120", font=font_huge, fill=FG_COLOR)
    
    # Footer
    font_small = get_font(10)
    draw.text((30, 110), "(Click to Set)", font=font_small, fill=ACCENT_COLOR)
    
    img.save(os.path.join(OUTPUT_DIR, "screen_set_bpm.png"))

def draw_tap_tempo_screen():
    img = create_base_image()
    draw = ImageDraw.Draw(img)
    
    font_header = get_font(14)
    font_small = get_font(10)
    
    # Header
    draw.text((25, 2), "TAP TEMPO", font=font_header, fill=FG_COLOR)
    draw.line((0, 18, 128, 18), fill=FG_COLOR)

    # Heart shape points
    # A simple heart polygon
    heart_points = [
        (64, 90), # Bottom tip
        (30, 50), # Left lower
        (30, 30), # Left upper
        (45, 25), # Left bump top
        (64, 40), # Top center dip
        (83, 25), # Right bump top
        (98, 30), # Right upper
        (98, 50), # Right lower
    ]
    
    # Draw Heart Outline (Threshold)
    draw.polygon(heart_points, outline=FG_COLOR)
    
    # Simulate Fill (Signal Level) - Filled slightly smaller/clipped
    # We'll just draw a filled polygon but clipped to the bottom half to simulate "filling up"
    # For simplicty in mockup, let's just fill a smaller heart inside or fill the bottom half
    
    fill_level_y = 60 # y coordinate to clip at
    
    # Draw a filled rectangle masked by the heart shape? 
    # PIL is a bit basic. Let's just draw lines inside the heart for the "fill"
    for y in range(60, 90, 2):
        draw.line((50, y, 78, y), fill=ACCENT_COLOR) # Rough fill approximation
        
    draw.text((45, 65), "##", font=font_small, fill=FG_COLOR) # Visual noise for fill

    # Sensitivity
    draw.text((20, 110), "Sensitivity: 50%", font=font_small, fill=FG_COLOR)
    
    img.save(os.path.join(OUTPUT_DIR, "screen_tap_tempo.png"))

if __name__ == "__main__":
    draw_metronome_screen()
    draw_menu_screen()
    draw_set_bpm_screen()
    draw_tap_tempo_screen()
    print("Mockups generated in " + OUTPUT_DIR)
