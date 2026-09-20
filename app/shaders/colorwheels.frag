// Olive - Non-Linear Video Editor
// Color Wheels / Lift-Gamma-Gain Fragment Shader

uniform sampler2D tex_in;

uniform vec3 lift_in;
uniform vec3 gamma_in;
uniform vec3 gain_in;
uniform vec3 offset_in;

in vec2 ove_texcoord;
out vec4 frag_color;

void main(void) {
  vec4 color = texture(tex_in, ove_texcoord);

  if (color.a > 0.0) {
    // Unassociate alpha for color manipulation
    vec3 rgb = color.rgb / color.a;

    // Apply Lift, Gain, Offset
    // ASC-CDL / standard lift-gamma-gain grading:
    // rgb = (rgb * gain + lift * (1.0 - rgb)) + offset
    vec3 graded = (rgb * gain_in + lift_in * (vec3(1.0) - rgb)) + offset_in;
    graded = max(vec3(0.0), graded);

    // Apply Gamma (power function per-channel)
    vec3 safe_gamma = max(vec3(0.001), gamma_in);
    graded = pow(graded, vec3(1.0) / safe_gamma);

    // Re-associate alpha
    color.rgb = graded * color.a;
  }

  frag_color = color;
}
