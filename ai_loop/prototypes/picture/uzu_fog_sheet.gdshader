// A sheet of drifting mist at one distance. Fades softly where it meets the picture (depth test
// against the scene), so it wraps around towers instead of cutting through them.
shader_type spatial;
render_mode unshaded, blend_mix, depth_draw_never, cull_disabled, shadows_disabled;

uniform sampler2D depth_tex : hint_depth_texture, filter_nearest;
uniform float amount = 0.45;
uniform float speed = 0.01;
uniform float seed = 0.0;
uniform float top = 0.1;       // mist sits below this height (sheet uv, -1 bottom .. 1 top)
uniform vec4 color : source_color = vec4(0.13, 0.16, 0.19, 1.0);
uniform float flash = 0.0;

// Noise from a seamless texture (Godot FastNoiseLite), not a per-pixel hash.
uniform sampler2D noise_tex : filter_linear_mipmap, repeat_enable;
float h21(vec2 p) { return texture(noise_tex, p * 0.37 + 0.5).g; }
float vn(vec2 p) { return texture(noise_tex, p / 9.0).r; }
float fbm(vec2 p) { float s = 0., a = .5; for (int i = 0; i < 5; i++) { s += a * vn(p); p = p * 2.03 + 17.1; a *= .5; } return s; }

void fragment() {
	vec2 q = UV * 2.0 - 1.0;
	q.y = -q.y;
	float n = fbm(vec2(q.x * 2.6 + TIME * speed * 10.0 + seed, q.y * 3.2 + seed * 0.3));
	n = smoothstep(0.35, 1.0, n + 0.1 * sin(TIME * 0.07 + seed));
	float band = smoothstep(top, top - 0.6, q.y) * smoothstep(-1.0, -0.6, q.y);
	// Soft contact with the picture behind or in front.
	float d = texture(depth_tex, SCREEN_UV).r;
	vec4 v = INV_PROJECTION_MATRIX * vec4(SCREEN_UV * 2.0 - 1.0, d, 1.0);
	float scene_z = -v.z / v.w;
	float my_z = -VERTEX.z;
	float soft = clamp((scene_z - my_z) / (my_z * 0.3), 0.0, 1.0);
	ALBEDO = color.rgb + vec3(0.5, 0.58, 0.78) * flash * 0.6;
	ALPHA = n * band * amount * soft;
}
