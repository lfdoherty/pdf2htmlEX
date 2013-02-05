/*
 * install.cc
 *
 * maintaining all known styles
 *
 * Copyright (C) 2012,2013 Lu Wang <coolwanglu@gmail.com>
 */

#include <iostream>
#include <cmath>
#include <algorithm>

#include <GlobalParams.h>

#include "Param.h"
#include "HTMLRenderer.h"
#include "util/namespace.h"
#include "util/math.h"
#include "util/misc.h"

namespace pdf2htmlEX {

using std::abs;
using std::cerr;
using std::endl;

const FontInfo * HTMLRenderer::install_font(GfxFont * font)
{
    assert(sizeof(long long) == 2*sizeof(int));
                
    long long fn_id = (font == nullptr) ? 0 : hash_ref(font->getID());

    auto iter = font_name_map.find(fn_id);
    if(iter != font_name_map.end())
        return &(iter->second);

    long long new_fn_id = font_name_map.size(); 

    auto cur_info_iter = font_name_map.insert(make_pair(fn_id, FontInfo())).first;

    FontInfo & new_font_info = cur_info_iter->second;
    new_font_info.id = new_fn_id;
    new_font_info.use_tounicode = true;

    if(font == nullptr)
    {
        new_font_info.ascent = 0;
        new_font_info.descent = 0;
        new_font_info.is_type3 = false;

        export_remote_default_font(new_fn_id);

        return &(new_font_info);
    }

    new_font_info.ascent = font->getAscent();
    new_font_info.descent = font->getDescent();
    new_font_info.is_type3 = (font->getType() == fontType3);

    if(param->debug)
    {
        cerr << "Install font: (" << (font->getID()->num) << ' ' << (font->getID()->gen) << ") -> " << "f" << hex << new_fn_id << dec << endl;
    }

    if(font->getType() == fontType3) {
        cerr << "Type 3 fonts are unsupported and will be rendered as Image" << endl;
        export_remote_default_font(new_fn_id);
        return &(cur_info_iter->second);
    }
    if(font->getWMode()) {
        cerr << "Writing mode is unsupported and will be rendered as Image" << endl;
        export_remote_default_font(new_fn_id);
        return &(cur_info_iter->second);
    }

    auto * font_loc = font->locateFont(xref, gTrue);
    if(font_loc != nullptr)
    {
        switch(font_loc -> locType)
        {
            case gfxFontLocEmbedded:
                install_embedded_font(font, cur_info_iter->second);
                break;
            case gfxFontLocExternal:
                install_external_font(font, cur_info_iter->second);
                break;
            case gfxFontLocResident:
                install_base_font(font, font_loc, cur_info_iter->second);
                break;
            default:
                cerr << "TODO: other font loc" << endl;
                export_remote_default_font(new_fn_id);
                break;
        }      
        delete font_loc;
    }
    else
    {
        export_remote_default_font(new_fn_id);
    }
      
    return &(cur_info_iter->second);
}

void HTMLRenderer::install_embedded_font(GfxFont * font, FontInfo & info)
{
    auto path = dump_embedded_font(font, info.id);

    if(path != "")
    {
        embed_font(path, font, info);
        export_remote_font(info, param->font_suffix, font);
    }
    else
    {
        export_remote_default_font(info.id);
    }
}

void HTMLRenderer::install_base_font(GfxFont * font, GfxFontLoc * font_loc, FontInfo & info)
{
    string psname(font_loc->path->getCString());
    string basename = psname.substr(0, psname.find('-'));

    GfxFontLoc * localfontloc = font->locateFont(xref, gFalse);
    if(param->embed_base_font)
    {
        if(localfontloc != nullptr)
        {
            embed_font(localfontloc->path->getCString(), font, info);
            export_remote_font(info, param->font_suffix, font);
            delete localfontloc;
            return;
        }
        else
        {
            cerr << "Cannot embed base font: f" << hex << info.id << dec << ' ' << psname << endl;
            // fallback to exporting by name
        }

    }

    string cssfont;
    auto iter = BASE_14_FONT_CSS_FONT_MAP.find(basename);
    if(iter == BASE_14_FONT_CSS_FONT_MAP.end())
    {
        cerr << "PS Font: " << basename << " not found in the base 14 font map" << endl;
        cssfont = "";
    }
    else
        cssfont = iter->second;

    // still try to get an idea of read ascent/descent
    if(localfontloc != nullptr)
    {
        // fill in ascent/descent only, do not embed
        embed_font(string(localfontloc->path->getCString()), font, info, true);
        delete localfontloc;
    }
    else
    {
        info.ascent = font->getAscent();
        info.descent = font->getDescent();
    }

    export_local_font(info, font, psname, cssfont);
}

void HTMLRenderer::install_external_font(GfxFont * font, FontInfo & info)
{
    string fontname(font->getName()->getCString());

    // resolve bad encodings in GB
    auto iter = GB_ENCODED_FONT_NAME_MAP.find(fontname); 
    if(iter != GB_ENCODED_FONT_NAME_MAP.end())
    {
        fontname = iter->second;
        cerr << "Warning: workaround for font names in bad encodings." << endl;
    }

    GfxFontLoc * localfontloc = font->locateFont(xref, gFalse);

    if(param->embed_external_font)
    {
        if(localfontloc != nullptr)
        {
            embed_font(string(localfontloc->path->getCString()), font, info);
            export_remote_font(info, param->font_suffix, font);
            delete localfontloc;
            return;
        }
        else
        {
            cerr << "Cannot embed external font: f" << hex << info.id << dec << ' ' << fontname << endl;
            // fallback to exporting by name
        }
    }

    // still try to get an idea of read ascent/descent
    if(localfontloc != nullptr)
    {
        // fill in ascent/descent only, do not embed
        embed_font(string(localfontloc->path->getCString()), font, info, true);
        delete localfontloc;
    }
    else
    {
        info.ascent = font->getAscent();
        info.descent = font->getDescent();
    }

    export_local_font(info, font, fontname, "");
}
    
long long HTMLRenderer::install_transform_matrix(const double * tm)
{
    Matrix m;
    memcpy(m.m, tm, sizeof(m.m));

    auto iter = transform_matrix_map.lower_bound(m);
    if((iter != transform_matrix_map.end()) && (tm_equal(m.m, iter->first.m, 4)))
        return iter->second;

    long long new_tm_id = transform_matrix_map.size();
    transform_matrix_map.insert(make_pair(m, new_tm_id));
    export_transform_matrix(new_tm_id, tm);
    return new_tm_id;
}

long long HTMLRenderer::install_fill_color(const GfxRGB * rgb)
{
    // transparent
    if (rgb == nullptr) {
        return -1;
    }
    
    const GfxRGB & c = *rgb;
    auto iter = fill_color_map.find(c);
    if(iter != fill_color_map.end())
        return iter->second;

    long long new_color_id = fill_color_map.size();
    fill_color_map.insert(make_pair(c, new_color_id));
    export_fill_color(new_color_id, rgb);
    return new_color_id;
}

long long HTMLRenderer::install_stroke_color(const GfxRGB * rgb)
{
    // transparent
    if (rgb == nullptr) {
        return -1;
    }
    
    const GfxRGB & c = *rgb;
    auto iter = stroke_color_map.find(c);
    if(iter != stroke_color_map.end())
        return iter->second;

    long long new_color_id = stroke_color_map.size();
    stroke_color_map.insert(make_pair(c, new_color_id));
    export_stroke_color(new_color_id, rgb);
    return new_color_id;
}

long long HTMLRenderer::install_whitespace(double ws_width, double & actual_width)
{
    // ws_width is already mulitpled by draw_scale
    auto iter = whitespace_map.lower_bound(ws_width - param->h_eps);
    if((iter != whitespace_map.end()) && (abs(iter->first - ws_width) <= param->h_eps))
    {
        actual_width = iter->first;
        return iter->second;
    }

    actual_width = ws_width;
    long long new_ws_id = whitespace_map.size();
    whitespace_map.insert(make_pair(ws_width, new_ws_id));
    export_whitespace(new_ws_id, ws_width);
    return new_ws_id;
}

long long HTMLRenderer::install_left(double left)
{
    auto iter = left_map.lower_bound(left - param->h_eps);
    if((iter != left_map.end()) && (abs(iter->first - left) <= param->h_eps))
    {
        return iter->second;
    }

    long long new_left_id = left_map.size();
    left_map.insert(make_pair(left, new_left_id));
    export_left(new_left_id, left);
    return new_left_id;
}

} // namespace pdf2htmlEX
