#!/bin/sh
# Host tests. Rendered screens land in test/out/ (PPM, plus PNG if Pillow is installed).
set -e
cd "$(dirname "$0")"
mkdir -p build out
S=../src
g++ -std=c++17 -Wall -Wextra -O1 -g -fsanitize=address,undefined \
    -I$S/camera -I$S/gfx -I$S/ui \
    test_host.cc $S/camera/json.cc $S/gfx/gfx.cc $S/gfx/fonts_gen.cc $S/ui/format.cc $S/ui/ui.cc \
    -o build/test_host
./build/test_host out
if python3 -c "import PIL" 2>/dev/null; then
    python3 -c "
import glob
from PIL import Image
for p in glob.glob('out/*.ppm'):
    Image.open(p).save(p[:-4] + '.png')
"
fi
