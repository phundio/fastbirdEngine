#!/usr/bin/env python
# -----------------------------------------------------------------------------
# This source file is part of fastbird engine
# For the latest info, see http://www.jungwan.net/
# 
# Copyright (c) 2013-2015 Jungwan Byun
# 
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
# 
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
# -----------------------------------------------------------------------------
# Auothor : fastbird(jungwan82@naver.com)
# HOW TO USE
# 1. Put this file in the plug-in folder
#     Maybe C:\Users\yourname\.gimp-2.8/plug-ins for Windows.
#     Or you can check in the dialog which you can open from the menu 
#        Edit->Preferences->Folders->Plug-Ins
# 2. If you are running Gimp, Restart it.
# 3. Now you can find the new menu in Edit->Export Layers Info..

from gimpfu import *
import gimp
import xml.etree.ElementTree as ET

# Configuration

# Relative to the folder which running the game executable file game.exe
gXMLImagePath = "data/textures/gameui.dds"
gXMLPath = "D:\\projects\\FBGame1\\data\\textures\\gameui.xml"
gDDSPath = "D:\\projects\\FBGame1\\data\\textures\\gameui.dds"

def indent(elem, level=0):
    i = "\n" + level*"\t"
    if len(elem):
        if not elem.text or not elem.text.strip():
            elem.text = i + "\t"
            if not elem.tail or not elem.tail.strip():
                elem.tail = i				
        
        for d in elem:
            indent(d, level+1)
            
        if not elem.tail or not elem.tail.strip():
            elem.tail = i
    else:
        if level and (not elem.tail or not elem.tail.strip()):
            elem.tail = i
                
def write_layer(layer, elem, id):
    subElem = ET.SubElement(elem, "region")
    subElem.set("ID", "{}".format(id))
    subElem.set("name", layer.name)
    subElem.set("x", "{}".format(layer.offsets[0]))
    subElem.set("y", "{}".format(layer.offsets[1]))
    subElem.set("width", "{}".format(layer.width))
    subElem.set("height", "{}".format(layer.height))
        
        
def export_layers_info(image, drawable, filepath, ddspath, ddspathInXml):
    rootElem = ET.Element("TextureAtlas")
    rootElem.set("file", ddspathInXml)
    id = 1;
    for layer in image.layers:
        write_layer(layer, rootElem, id)
        id += 1
    indent(rootElem)
    tree = ET.ElementTree(rootElem)
    tree.write(filepath)
    
    dplimg = image.duplicate()
    dplimg.merge_visible_layers(1) # EXPAND-AS-NECESSARY (0), CLIP-TO-IMAGE (1), CLIP-TO-BOTTOM-LAYER (2)
    
    #Compression format (0 = None, 1 = BC1/DXT1, 2 = BC2/DXT3, 3 = BC3/DXT5, 4 = BC3n/DXT5nm,
    #5 = BC4/ATI1N, 6 = BC5/ATI2N, 7 = RXGB (DXT5), 8 = Alpha Exponent (DXT5), 9 = YCoCg (DXT5), 10 = YCoCg scaled (DXT5))
    compression_format = 0    
    mipmaps = 0
    savetype = 0
    format = 0
    transparent_index = -1    
    mipmap_filter=0
    mipmap_wrap = 0
    gamma_correct=0
    gamma = 2.2
    srgb = 0
    perceptual_metric = 0
    preserve_alpha_coverage = 0
    alpha_test_threshold = 0.5
    dplimg.active_layer.resize_to_image_size()
    gimp.pdb.file_dds_save(dplimg, dplimg.active_layer, ddspath, ddspath, compression_format, mipmaps, savetype, format, transparent_index, mipmap_filter, mipmap_wrap, gamma_correct, srgb, gamma, perceptual_metric, preserve_alpha_coverage, alpha_test_threshold)
    gimp.delete(dplimg)
    
	
register(
  "python_fu_export_layers_info",
  "Export layers information for fastbird-engine", "Id, name, position and size will be imported.",
  "fastbird", "fastbird", "2014",
  "Export Layers Info..",
  "*",
  [
    (PF_IMAGE, "image", "Image", None),
    (PF_DRAWABLE, "drawable", "Drawable", None),
    (PF_FILE , "filepath", "File Path", gXMLPath),
    (PF_FILE , "ddspath", "DDS File Path", gDDSPath),
	(PF_STRING , "ddspathInXml", "DDS File Path In Xml", gXMLImagePath),
  ],
  [],
  export_layers_info, menu="<Image>/Tools")

main()