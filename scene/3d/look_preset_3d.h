/**************************************************************************/
/*  look_preset_3d.h                                                      */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

#include "core/object/ref_counted.h"

class DirectionalLight3D;
class OmniLight3D;
class Environment;
class ImageTexture3D;
class StandardMaterial3D;
struct Color;

// Ready-to-use 3D "look" for new scenes (U.ZU Engine, BACKLOG B-701).
//
// Builds an Environment and configures a DirectionalLight3D with tuned values:
// filmic tonemapping, sky-based ambient and reflected light, ambient occlusion,
// glow, distance fog (aerial perspective) and split shadows, for clear air,
// humid air or golden hour. Godot's class defaults leave all of these
// off, which is why a fresh 3D scene looks flat.
//
// This is engine-internal (not registered with ClassDB). Class defaults are not
// changed, so existing scenes keep their look; the editor uses this to add a
// WorldEnvironment and a sun on request.
namespace LookPreset3D {

enum Quality {
	QUALITY_HIGH, // Forward+ on a dedicated GPU.
	QUALITY_STANDARD, // Forward+ on integrated GPUs, or Mobile.
	QUALITY_LOW, // Compatibility (Web) renderer: only features it supports.
};

// Air and time of day (aerial perspective).
enum Atmosphere {
	ATMOSPHERE_CLEAR, // Dry, clear air: the distance turns blue.
	ATMOSPHERE_HUMID, // Humid air: the distance fades into a white haze.
	ATMOSPHERE_GOLDEN_HOUR, // Low, warm sun near sunrise or sunset.
	ATMOSPHERE_NIGHT, // Weak, cool moonlight; light comes from lamps.
};

// Returns the quality that suits the given rendering method name
// ("forward_plus", "mobile", "gl_compatibility").
Quality get_default_quality(const String &p_rendering_method);

Ref<Environment> create_environment(Quality p_quality, Atmosphere p_atmosphere = ATMOSPHERE_CLEAR);
void configure_sun(DirectionalLight3D *p_sun, Quality p_quality, Atmosphere p_atmosphere = ATMOSPHERE_CLEAR);

// Color grade applied after tonemapping (display-referred sRGB in and out):
// moderate saturation in the mid-tones, less in the shadows and highlights,
// a gentle S-curve, slightly cool shadows and warm highlights.
Color grade_color(const Color &p_color);
Ref<ImageTexture3D> create_color_grade();

// Materials for things whose look depends on how light passes through them.
enum MaterialKind {
	MATERIAL_SKIN, // Subsurface scattering (skin mode).
	MATERIAL_FOLIAGE, // Backlight through thin leaves, double-sided.
	MATERIAL_GLASS, // Transparent, glossy.
	MATERIAL_WATER, // Transparent water color with glossy reflections.
	MATERIAL_LAMP, // Emissive, warm-colored light source.
};

Ref<StandardMaterial3D> create_material(MaterialKind p_kind);

// Three-point lighting with the sun as the key light: a fill light from the
// side in the color of the sky, and a rim light from behind. Neither casts
// shadows nor affects the sky.
void configure_fill_light(DirectionalLight3D *p_light, Atmosphere p_atmosphere = ATMOSPHERE_CLEAR);
void configure_rim_light(DirectionalLight3D *p_light, Atmosphere p_atmosphere = ATMOSPHERE_CLEAR);
// A warm lamp whose light only reaches nearby.
void configure_lamp(OmniLight3D *p_lamp, Quality p_quality);

} // namespace LookPreset3D
