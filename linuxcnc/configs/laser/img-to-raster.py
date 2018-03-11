#!/usr/bin/python

import sys, os
from PIL import Image

# Size of the greatest side of the photo in MM
size = 40
point_size = 0.2
feed = 3000
offset = 5.0

if len(sys.argv) >= 2:
  file_name = sys.argv[1]
  if len(sys.argv) >= 3:
    point_size = float(sys.argv[2])
    if len(sys.argv) >= 4:
      feed = int(sys.argv[3])
else:
  print("Usage: %s file [point feed]\n")
  sys.exit

(base,ext) = os.path.splitext(file_name)
ngc = base+".ngc"

f_gcode = open(ngc, "w");
img = Image.open(file_name,'r')
img_bw = img.convert('L')
print("Image size: %dx%d" % img_bw.size);
if img_bw.size[0] > img_bw.size[1]:
  rel = float(img_bw.size[1]) / float(img_bw.size[0])
  new_width = float(size) / float(point_size)
  new_height = new_width * rel
else:
  rel = float(img_bw.size[0]) / float(img_bw.size[1])
  new_height = float(size) / float(point_size)
  new_width = new_height * rel

new_width = int(new_width)
new_height = int(new_height)
print("New Image size: %dx%d" % (new_width, new_height));
img_bw = img_bw.resize((new_width, new_height), Image.ANTIALIAS)

f_gcode.write("G21\nG64\n")
f_gcode.write("F%d\n" % feed)
f_gcode.write("M68 E0 Q0\n")
f_gcode.write("G0 X0 Y0\n")
f_gcode.write("M3 S1\n")

dir = 1
off = 0
pos_y = 0.0
pos_x = 0.0
for x in range(0, new_width - 1, 1):
  old_power = 0
  f_gcode.write("(row %d of %d)\n" % ( x + 1, new_width ))
  f_gcode.write("M67 E0 Q0\n")
  if dir == 1:
    pos_y = offset
    f_gcode.write("Y%.3f\n" % ( pos_y ))
  else:
    pos_y -= offset
    f_gcode.write("Y%.3f\n" % (pos_y))
  for y in range(0, new_height - 1, 1):
    if dir == 1:
      pt_x = x
      pt_y = y
      pos_y += point_size
    else:
      pt_x = x
      pt_y = new_height - y - 1
      pos_y -= point_size
    power = 256 - img_bw.getpixel((pt_x, pt_y))
    if old_power != power:
      f_gcode.write("M67 E0 Q%d\n" % (power))
      old_power = power
    f_gcode.write("Y%.3f\n" % ( pos_y ))
  if dir == 1:
    dir = 0
    pos_y += offset
  else:
    dir = 1
    pos_y = 0
  f_gcode.write("M67 E0 Q0\n")
  f_gcode.write("Y%.3f\n" % ( pos_y ))
  pos_x += point_size
  f_gcode.write("G0 X%.3f Y%.3f\n" % ( pos_x, pos_y ))

f_gcode.write("M67 E0 Q0\n")
f_gcode.write("M5\nM2\n")
f_gcode.close

print("GCode file: %s" % ngc)
