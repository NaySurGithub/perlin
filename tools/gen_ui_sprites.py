import sys, zipfile, io
from PIL import Image

jar, out = sys.argv[1], sys.argv[2]
z = zipfile.ZipFile(jar)
base = "assets/minecraft/textures/gui/"

def load(path):
    return Image.open(io.BytesIO(z.read(base + path))).convert("RGBA")

def save(name, img):
    img.save(out + "/" + name)

save("ui_inventory.png", load("container/inventory.png").crop((0, 0, 176, 166)))
for src, dst in (("tab_items", "ui_creative_items"), ("tab_item_search", "ui_creative_search"), ("tab_inventory", "ui_creative_survival")):
    save(dst + ".png", load("container/creative_inventory/" + src + ".png").crop((0, 0, 195, 136)))

tabs = Image.new("RGBA", (26 * 7, 32 * 4), (0, 0, 0, 0))
for row, name in enumerate(("tab_top_unselected", "tab_top_selected", "tab_bottom_unselected", "tab_bottom_selected")):
    for col in range(7):
        tabs.paste(load("sprites/container/creative_inventory/%s_%d.png" % (name, col + 1)), (26 * col, 32 * row))
save("ui_creative_tabs.png", tabs)

misc = Image.new("RGBA", (24, 15), (0, 0, 0, 0))
misc.paste(load("sprites/container/creative_inventory/scroller.png"), (0, 0))
misc.paste(load("sprites/container/creative_inventory/scroller_disabled.png"), (12, 0))
save("ui_creative_misc.png", misc)

icons = Image.new("RGBA", (16 * 5, 16), (0, 0, 0, 0))
for i, name in enumerate(("helmet", "chestplate", "leggings", "boots", "shield")):
    icons.paste(load("sprites/container/slot/%s.png" % name), (16 * i, 0))
save("ui_slot_icons.png", icons)
