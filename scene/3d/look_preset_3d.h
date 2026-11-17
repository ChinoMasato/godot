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

#include "core/math/rect2.h"
#include "core/object/ref_counted.h"
#include "core/templates/vector.h"

class Camera3D;
class Compositor;
class CameraAttributesPractical;
class DirectionalLight3D;
class OmniLight3D;
class Environment;
class ImageTexture3D;
class ShaderMaterial;
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

// With p_clouds, the sky is a cloud sky (create_cloud_sky) instead of the
// physical sky.
Ref<Environment> create_environment(Quality p_quality, Atmosphere p_atmosphere = ATMOSPHERE_CLEAR, bool p_clouds = false);
// A sky with a static cloud layer, colored for the atmosphere.
Ref<ShaderMaterial> create_cloud_sky(Atmosphere p_atmosphere);
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

// Camera attributes for the WorldEnvironment: a slight blur of the far
// distance at high quality (none otherwise). Exposure stays in the Environment.
Ref<CameraAttributesPractical> create_camera_attributes(Quality p_quality);

// Lenses (vertical field of view). Wide angles stress depth, telephoto stresses
// scale; slightly toward telephoto reads calm and natural. Godot's Camera3D
// default (75) is a wide angle.
enum Lens {
	LENS_WIDE, // 70 degrees.
	LENS_STANDARD, // 45 degrees.
	LENS_TELEPHOTO, // 25 degrees.
};

// Eye heights for placing a camera: people placed on the same ground then
// have their heads on the horizon line at any distance (standing).
enum EyeLevel {
	EYE_LEVEL_STANDING, // 1.6 m.
	EYE_LEVEL_LOW, // 0.8 m, a seated or child's view.
};

float get_lens_fov(Lens p_lens);
float get_eye_height(EyeLevel p_eye_level);
// Sets the lens. A non-zero shift (fraction of the view height, positive is up)
// moves the framing without tilting the camera, so vertical lines stay parallel.
void configure_camera(Camera3D *p_camera, Lens p_lens, float p_shift = 0.0);

// Soft shadow filter quality for the sun at a quality level (B-714). It is a
// project setting, not part of the scene: high quality uses the high filter,
// which with a smaller sun removes the grain of wide soft shadows.
int get_shadow_filter_quality(Quality p_quality);
// Sets that filter quality in the project settings (in memory; saving them is
// up to the caller) and, when a renderer runs, for the current run.
void apply_shadow_filter_quality(Quality p_quality);

// Post effects for the WorldEnvironment: edge contrast and vignette
// (UZUPostEffect) at high and standard quality; none at low quality, where the
// Compatibility renderer does not run compositor effects.
Ref<Compositor> create_compositor(Quality p_quality);

// Composition guides for a frame, as line segments (pairs of points): the two
// rule-of-thirds lines each way, the two diagonals and a small center cross.
Vector<Vector2> get_composition_guide_lines(const Rect2 &p_frame);

} // namespace LookPreset3D
