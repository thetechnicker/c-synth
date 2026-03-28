// ---- texture.vert ---- (vertex layout: vec2 pos, vec2 uv, vec4 col — stride 32 B)
 
    #version 450
    layout(set=1, binding=0) uniform PC { float inv_w; float inv_h; } pc;
    layout(location = 0) in  vec2 a_pos;
    layout(location = 1) in  vec2 a_uv;
    layout(location = 2) in  vec4 a_col;
    layout(location = 0) out vec2 v_uv;
    layout(location = 1) out vec4 v_col;
    void main() {
        gl_Position = vec4(a_pos.x * pc.inv_w * 2.0 - 1.0,
                           1.0 - a_pos.y * pc.inv_h * 2.0, 0.0, 1.0);
        v_uv  = a_uv;
        v_col = a_col;
    }
